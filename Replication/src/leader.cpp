#include "../include/leader.hpp"
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

Leader::Leader(const string &dbName, uint16_t clientPort, uint16_t followerPort, int requiredAcks, const string &host)
    :dbName(dbName), clientPort(clientPort), followerPort(followerPort), requiredAcks(requiredAcks), host(host) {

      log_line(LogLevel::INFO, "[Leader] Starting on " + this->host +
             " ClientPort:" + std::to_string(this->clientPort) +
             " ReplPort:" + std::to_string(this->followerPort) +
             " DB:" + this->dbName + " ACKS:" + std::to_string(this->requiredAcks));

    this->duombaze = std::make_unique<Database>(this->dbName);
}

Leader::~Leader() {
  log_line(LogLevel::INFO, "[Leader] Shutting down...");
  this->running = false;

  if (this->clientListenSocket != NET_INVALID) {
    net_close(this->clientListenSocket);
    this->clientListenSocket = NET_INVALID;
  }

  if (this->followerListenSocket != NET_INVALID) {
    net_close(this->followerListenSocket);
    this->followerListenSocket = NET_INVALID;
  }

  this->conditionVariable.notify_all();

  if (this->followerAcceptThread.joinable()) {
    this->followerAcceptThread.join();
  }
}

void Leader::Run() {
  // 1. Paleidžiam periodinį "[Leader] host port" (Announce)
  std::thread announce_thr(&Leader::AnnouncePresence, this);
  announce_thr.detach(); // thread'as gyvena iki proceso pabaigos

  // 2. Paleidžiam followerių priėmėją atskiram threade
  this->followerAcceptThread = std::thread(&Leader::AcceptFollowers, this);

  // 3. Pagrindinis thread'as aptarnauja klientus (SET/GET/DEL)
  // This function blocks until running_ becomes false or socket closes
  this->ServeClients();

  // 4. Jei kada nors serveClients baigtųsi, palaukiam followerAcceptThread
  if (this->followerAcceptThread.joinable()) {
      this->followerAcceptThread.join();
  }
}

// Paprastas periodinis logas, kad iš logų matytųsi, kuris node yra LEADER.
void Leader::AnnouncePresence() {
  try {
    while (this->running) {
      log_line(LogLevel::INFO, std::string("[Leader] ") + this->host + " " + std::to_string(this->clientPort));
      std::this_thread::sleep_for(std::chrono::seconds(Consts::SLEEP_TIME_MS));
    }
  } catch (const std::exception& ex) {
    log_line(LogLevel::ERROR,
             std::string("Leader announce thread exception: ") + ex.what());
  } catch (...) {
    log_line(LogLevel::ERROR, "Leader announce thread unknown exception");
  }
}

// Klausosi naujų follower'io jungčių nurodytame porte ir kiekvieną naują
// follower'į prideda į followers sąrašą bei paleidžia jam follower_thread.
void Leader::AcceptFollowers() {
  sock_t listenSocket = tcp_accept(this->followerPort);
  if (listenSocket == NET_INVALID) {
    std::cerr << "Follower listen failed\n";
    log_line(LogLevel::ERROR, "Follower listen failed on port " + std::to_string(this->followerPort));
    return;
  }

  this->followerListenSocket = listenSocket;
  log_line(LogLevel::INFO, "Leader: listening followers on " + std::to_string(this->followerPort));

  while (this->running) {
    sock_t followerSocket = tcp_accept(listenSocket);
    if (followerSocket == NET_INVALID) {
      continue;
    }

    auto followerConnection = std::make_shared<FollowerConnection>();
    followerConnection->followerSocket = followerSocket;

    {
      std::lock_guard<mutex> lock(this->mtx);
      this->followers.push_back(followerConnection);
    }

    thread(&Leader::HandleFollower, this, followerConnection);
  }
}

// Kiekvienam follower'iui skirtas thread'as, kuris:
// 1) Perskaito HELLO <last_lsn> iš follower'io;
// 2) Nusiunčia jam trūkstamus WAL įrašus;
// 3) Laukia iš jo ACK pranešimų.
void Leader::HandleFollower(shared_ptr<FollowerConnection> follower) {
  try {
    // 1. HELLO iš follower'io.
    string helloLine;
    if (!recv_line(follower->followerSocket, helloLine)) {
      follower->isAlive = false;
      return;
    }

    auto helloParts = split(helloLine, ' ');
    if (helloParts.size() != 2 || helloParts[0] != "HELLO") {
      log_line(LogLevel::WARN, "Follower sent bad HELLO: " + helloLine);
      follower->isAlive = false;
      return;
    }

    uint64_t lastAppliedLsn = 0;
    try {
      lastAppliedLsn = std::stoull(helloParts[1]);
    } catch (...) {
      log_line(LogLevel::WARN, "Bad lsn in follower HELLO: " + helloParts[1]);
      follower->isAlive = false;
      return;
    }

    // 2. Persiunčiam follower'iui visus įrašus nuo jo paskutinio turimo LSN.
    auto missingRecords = this->duombaze->GetWalRecordsSince(lastAppliedLsn);
    for (const auto &walRecord : missingRecords) {
      string message = (walRecord.operation == WalOperation::SET)
          ? ("WRITE "  + std::to_string(walRecord.lsn) + " " + walRecord.key + " " + walRecord.value + "\n")
          : ("DELETE " + std::to_string(walRecord.lsn) + " " + walRecord.key + "\n");

        if (!send_all(follower->followerSocket, message)) {
          // Jei siuntimas nepavyksta – nutraukiam šitą follower'į
          follower->isAlive = false;
          return;
        }
    }

    if (!missingRecords.empty()) {
      follower->ackedUptoLsn = missingRecords.back().lsn;
    }

    // 3. Laukiam ACK.
    while (follower->isAlive && this->running) {
      string line;
      if (!recv_line(follower->followerSocket, line)) {
        // nutrūkęs ryšys / klaida
        follower->isAlive = false;
        break;
      }

      auto tokens = split(line, ' ');
      if (tokens.size() == 2 && tokens[0] == "ACK") {
        try {
          uint64_t ackLsn = std::stoull(tokens[1]);
          follower->ackedUptoLsn = std::max(follower->ackedUptoLsn, ackLsn);
          this->conditionVariable.notify_all();
        } catch (...) {
          log_line(LogLevel::WARN, "Bad ACK lsn from follower: " + tokens[1]);
        }
      }
    }
  } catch (const std::exception& ex) {
    log_line(LogLevel::ERROR, std::string("Exception in follower_thread: ") + ex.what());
  } catch (...) {
    log_line(LogLevel::ERROR, "Unknown exception in follower_thread");
  }
}

// Išsiunčia vieną WAL įrašą visiems aktyviems follower'iams.
// Naudojama tiek naujiems įrašams (SET/DEL), tiek paleidimo metu replay'inant logbuf.
void Leader::BroadcastWalRecord(const WalRecord &walRecord) {
  string message = (walRecord.operation == WalOperation::SET)
    ? "WRITE " + std::to_string(walRecord.lsn) + " " + walRecord.key + " " + walRecord.value + "\n"
    : "DELETE " + std::to_string(walRecord.lsn) + " " + walRecord.key + "\n";

  std::lock_guard<mutex> lock(this->mtx);
  for (auto &follower : this->followers) {
    if (!follower->isAlive) {
      continue;
    }

    if (!send_all(follower->followerSocket, message)) {
      follower->isAlive = false;
      log_line(LogLevel::WARN, "Broadcast failed to follower");
    }
  }
}

// Suskaičiuoja, kiek follower'ių turi acked_upto_lsn >= duotas lsn.
// Naudojama tam, kad patikrinti ar surinkom pakankamai ACK'ų (REQUIRED_ACKS).
size_t Leader::CountAcks(uint64_t lsn) {
  size_t ackCount = 0;

  for (auto &follower: this->followers) {
    if (follower->isAlive && follower->ackedUptoLsn >= lsn) {
      ackCount++;
    }
  }

  return ackCount;
}

// Klausosi klientų (SET/GET/DEL) nurodytame porte ir tvarko jų užklausas.
// Visi SET/DEL:
//  - įrašomi į WAL failą
//  - įrašomi į kv
//  - pridedami į logbuf
//  - išsiunčiami follower'iams (broadcast_walRecord)
//  - jei REQUIRED_ACKS > 0, laukiama ACK'ų iš follower'ių
void Leader::ServeClients() {
  sock_t listenSocket = tcp_listen(this->clientPort);
  if (listenSocket == NET_INVALID) {
    std::cerr << "Client listen failed\n";
    log_line(LogLevel::ERROR, "Client listen failed on port " + std::to_string(this->clientPort));
    return;
  }
  this->clientListenSocket = listenSocket;
  log_line(LogLevel::INFO, "Leader: listening clients on " + std::to_string(this->clientPort));

  while (this->running) {
    sock_t clientSocket = tcp_accept(listenSocket);
    if (clientSocket == NET_INVALID) {
      continue;
    }

    // Kiekvienam klientui – atskiras handler'is threade.
    thread(&Leader::HandleClient, this, clientSocket).detach();
  }
}

void Leader::HandleClient(sock_t clientSocket) {
  try {
    string requestLine;
    while (this->running && recv_line(clientSocket, requestLine)) {
      auto tokens = split(trim(requestLine), ' ');
      if (tokens.empty()) {
        continue;
      }

      string &command = tokens[0];

      if (command == "SET" && tokens.size() >= 3) {
        uint64_t newLsn = 0;
        WalRecord walRecord(0, WalOperation::SET, tokens[1], tokens[2]);

        newLsn = this->duombaze->ExecuteLogSetWithLSN(walRecord.key, walRecord.value);

        if (newLsn > 0) {
          walRecord.lsn = newLsn;
          this->BroadcastWalRecord(walRecord);

          if (this->requiredAcks > 0) {
            std::unique_lock<mutex> lock(this->mtx);
            this->conditionVariable.wait_for(lock, std::chrono::seconds(3), [&]{
              return this->CountAcks(newLsn) >= static_cast<size_t>(this->requiredAcks);
            });
          }
          send_all(clientSocket, "OK " + std::to_string(newLsn) + "\n");
        } else {
          send_all(clientSocket, "ERR_WRITE_FAILED\n");
        }
      } else if (command == "DEL" && tokens.size() == 2) {
        uint64_t newLsn = 0;
        WalRecord walRecord(0, WalOperation::DELETE, tokens[1]);

        newLsn = this->duombaze->ExecuteLogDeleteWithLSN(walRecord.key);

        if (newLsn > 0) {
          walRecord.lsn = newLsn;
          this->BroadcastWalRecord(walRecord);

          if (this->requiredAcks > 0) {
            std::unique_lock<mutex> lock(this->mtx);
            this->conditionVariable.wait_for(lock, std::chrono::seconds(3), [&]{
              return this->CountAcks(newLsn) >= static_cast<size_t>(this->requiredAcks);
            });
          }
          send_all(clientSocket, "OK " + std::to_string(newLsn) + "\n");
        } else {
          send_all(clientSocket, "ERR_WRITE_FAILED\n");
        }
      } else if (command == "GET" && tokens.size() >= 2) {
        auto result = this->duombaze->Get(tokens[1]);
        if (!result.has_value()) {
          send_all(clientSocket, "NOT_FOUND\n");
        }
        else {
          send_all(clientSocket, "VALUE " + result->value + "\n");
        }
      } else if (command == "GETFF" && tokens.size() >= 3) {
        try {
          auto startKey = string(tokens[1]);
          uint32_t count = std::stoul(tokens[2]);
          auto results = this->duombaze->GetFF(startKey, count);

          for (const auto &cell : results) {
            send_all(clientSocket, "KEY_VALUE "+ cell.key + " " + cell.value + "\n");
          }
          send_all(clientSocket, "END\n");
        } catch (const std::exception& e) {
            send_all(clientSocket, "ERR " + std::string(e.what()) + "\n");
        }
      } else if (command == "GETFB" && tokens.size() >= 3) {
        try {
          auto startKey = string(tokens[1]);
          uint32_t count = std::stoul(tokens[2]);
          auto results = this->duombaze->GetFB(startKey, count);

          for (const auto &cell : results) {
            send_all(clientSocket, "KEY_VALUE "+ cell.key + " " + cell.value + "\n");
          }
          send_all(clientSocket, "END\n");
        } catch (const std::exception& e) {
            send_all(clientSocket, "ERR " + std::string(e.what()) + "\n");
        }
      } else if (command == "OPTIMIZE") {
        try {
          this->duombaze->Optimize();
          send_all(clientSocket, "OK_OPTIMIZED\n");
        } catch(const std::exception& e) {
            send_all(clientSocket, "ERR " + std::string(e.what()) + "\n");
        }
      } else {
        send_all(clientSocket, "ERR usage: SET|GET|DEL|GETFF|GETFB|OPTIMIZE\n");
      }
    }
  } catch (const std::exception& ex) {
    log_line(LogLevel::ERROR, std::string("Exception in client handler: ") + ex.what());
  } catch (...) {
    log_line(LogLevel::ERROR, "Unknown exception in client handler");
  }

  net_close(clientSocket);
}

int main(int argc, char** argv) {
  try {
    // Argumentai:
    // leader <client_port> <follower_port> <wal_path> <required_acks> [host]
    if (argc < 5) {
      std::cerr << "Usage: leader <client_port> <follower_port> <wal_path> <required_acks> [host]\n";
      return 1;
    }

    int client_port_i   = std::stoi(argv[1]);
    int follower_port_i = std::stoi(argv[2]);
    if (client_port_i <= 0 || client_port_i > Consts::MAX_PORT_NUMBER ||
        follower_port_i <= 0 || follower_port_i > Consts::MAX_PORT_NUMBER) {
      std::cerr << "Invalid ports\n";
      return 1;
    }

    auto client_port = static_cast<uint16_t>(client_port_i);
    auto follower_port = static_cast<uint16_t>(follower_port_i);
    string dbName = argv[3];

    auto requiredAcks = std::stoi(argv[4]);
    requiredAcks = std::max(requiredAcks, 0);

    // host'as – optional 5-as argumentas, kad galėtum atspausdinti IP arba hostname'e
    string leader_host = (argc >= 6) ? string(argv[5]) : string("unknown-host");

    // Sukuriam Leader instance ir runninam.
    Leader leader(dbName, client_port, follower_port, requiredAcks, leader_host);

    leader.Run();

    return 0;
  } catch (const std::exception& ex) {
    log_line(LogLevel::ERROR, std::string("Fatal exception in leader main: ") + ex.what());
    return 1;
  } catch (...) {
    log_line(LogLevel::ERROR, "Unknown fatal exception in leader main");
    return 1;
  }
}
