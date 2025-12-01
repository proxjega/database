#include "../include/leader.hpp"
#include "../include/rules.hpp"
#include <chrono>
#include <cstddef>

namespace CONSTS {
  static constexpr int TRIES_FOR_COMPACT = 50;
  static constexpr int SLEEP_BEFORE_CHECKING_NODES = 100;

  static constexpr int COMPACT_INTERVAL = 60;
}

Leader::Leader(string dbName, uint16_t clientPort, uint16_t followerPort, int requiredAcks, string host)
    :dbName(std::move(dbName)), clientPort(clientPort), followerPort(followerPort), requiredAcks(requiredAcks), host(std::move(host)) {

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

  if (this->compactionThread.joinable()) {
    this->compactionThread.join();
  }

  if (this->followerAcceptThread.joinable()) {
    this->followerAcceptThread.join();
  }

  if (this->announceThread.joinable()) {
    this->announceThread.join();
  }
}

void Leader::Run() {
  // 1. Paleidžiam periodinį "[Leader] host port" (Announce)
  thread announceThread(&Leader::AnnouncePresence, this);
  announceThread.detach(); // // thread'as gyvena iki proceso pabaigos

  // 2. Paleidžiam automatinio sinchronizavimo thread'ą.
  this->compactionThread = thread(&Leader::AutoCompactLoop, this);

  // 3. Paleidžiam followerių priėmėją atskiram threade
  this->followerAcceptThread = thread(&Leader::AcceptFollowers, this);

  // 4. Pagrindinis thread'as aptarnauja klientus (SET/GET/DEL)
  // This function blocks until running_ becomes false or socket closes
  this->ServeClients();

  // 5. Jei kada nors serveClients baigtųsi, palaukiam followerAcceptThread
  if (this->followerAcceptThread.joinable()) {
      this->followerAcceptThread.join();
  }
}

void Leader::AutoCompactLoop() {
  while (this->running) {
    for (int i = 0; i < CONSTS::COMPACT_INTERVAL; ++i) {
      if (!this->running) {
        return;
      }
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    if (!this->running) {
      return;
    }

    log_line(LogLevel::INFO, "[AutoCompact] Triggering scheduled compaction...");
    string resultMsg;
    auto success = this->PerformCompaction(resultMsg);

    if (!success) {
        log_line(LogLevel::WARN, "[AutoCompact] Failed: " + resultMsg);
    }
  }
}

// Paprastas periodinis logas, kad iš logų matytųsi, kuris node yra LEADER.
void Leader::AnnouncePresence() {
  try {
    while (this->running) {
      log_line(LogLevel::INFO, string("[Leader] ") + this->host + " " + std::to_string(this->clientPort));
      std::this_thread::sleep_for(std::chrono::seconds(Consts::SLEEP_TIME_MS));
    }
  } catch (const std::exception& ex) {
    log_line(LogLevel::ERROR,
             string("Leader announce thread exception: ") + ex.what());
  } catch (...) {
    log_line(LogLevel::ERROR, "Leader announce thread unknown exception");
  }
}

// Klausosi naujų follower'io jungčių nurodytame porte ir kiekvieną naują
// follower'į prideda į followers sąrašą bei paleidžia jam follower_thread.
void Leader::AcceptFollowers() {
  sock_t listenSocket = tcp_listen(this->followerPort);
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

    thread(&Leader::HandleFollower, this, followerConnection).detach();
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

    // Čia kažkodėl siuntinėja pastoviai su kiekvienu request'u "GET __verify__". Tai ignore
    if (helloLine.rfind("GET", 0) == 0) {
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

    {
      std::lock_guard<mutex> stateLock(follower->connectionMutex);
      follower->ackedUptoLsn = lastAppliedLsn;
    }

    // 2. Persiunčiam follower'iui visus įrašus nuo jo paskutinio turimo LSN.
    auto missingRecords = this->duombaze->GetWalRecordsSince(lastAppliedLsn);
    {
      std::lock_guard<mutex> ioLock(follower->connectionMutex);

      for (const auto &walRecord : missingRecords) {
      string message = (walRecord.operation == WalOperation::SET)
          ? ("WRITE "  + std::to_string(walRecord.lsn) + " " + walRecord.key + " " + format_length_prefixed_value(walRecord.value) + "\n")
          : ("DELETE " + std::to_string(walRecord.lsn) + " " + walRecord.key + "\n");

        if (!send_all(follower->followerSocket, message)) {
          // Jei siuntimas nepavyksta – nutraukiam šitą follower'į
          follower->isAlive = false;
          return;
        }
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
    log_line(LogLevel::ERROR, string("Exception in follower_thread: ") + ex.what());
  } catch (...) {
    log_line(LogLevel::ERROR, "Unknown exception in follower_thread");
  }
}

// Išsiunčia vieną WAL įrašą visiems aktyviems follower'iams.
// Naudojama tiek naujiems įrašams (SET/DEL), tiek paleidimo metu replay'inant logbuf.
void Leader::BroadcastWalRecord(const WalRecord &walRecord) {
  string message = (walRecord.operation == WalOperation::SET)
    ? "WRITE " + std::to_string(walRecord.lsn) + " " + walRecord.key + " " + format_length_prefixed_value(walRecord.value) + "\n"
    : "DELETE " + std::to_string(walRecord.lsn) + " " + walRecord.key + "\n";

  std::lock_guard<mutex> listLock(this->mtx);

  for (auto &follower : this->followers) {
    if (follower->isAlive) {
      std::lock_guard<mutex> ioLock(follower->connectionMutex);

      if (!send_all(follower->followerSocket, message)) {
        follower->isAlive = false;
        log_line(LogLevel::WARN, "Broadcast failed to follower");
    }
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

void Leader::WaitForAcks(uint64_t lsn) {
  if (this->requiredAcks <= 0) {
    return;
  }

  std::unique_lock<mutex> lock(this->mtx);
  this->conditionVariable.wait_for(lock, std::chrono::seconds(3), [&]{
      return this->CountAcks(lsn) >= static_cast<size_t>(this->requiredAcks);
  });
}

// Klausosi klientų (SET/GET/DEL) nurodytame porte ir tvarko jų užklausas.
// Visi SET/DEL:
//  - įrašomi į WAL failą
//  - įrašomi į B+ medį
//  - išsiunčiami follower'iams (broadcastWalRecord)
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
    sock_t clientSocket = tcp_accept(this->clientListenSocket);
    if (clientSocket == NET_INVALID) {
      continue;
    }

    // Kiekvienam klientui – atskiras handler'is threade.
    thread(&Leader::HandleClient, this, clientSocket).detach();
  }
}

void Leader::HandleSet(sock_t clientSocket, const vector<string> &tokens) {
  auto key = string(tokens[1]);
  string value;

  if (!parse_length_prefixed_value(tokens, 2, clientSocket, value)) {
    send_all(clientSocket, "ERR_INVALID_VALUE_FORMAT\n");
    return;
  }

  WalRecord walRecord(0, WalOperation::SET, key, value);

  auto newLsn = this->duombaze->ExecuteLogSetWithLSN(walRecord.key, walRecord.value);

  if (newLsn > 0) {
    walRecord.lsn = newLsn;

    this->BroadcastWalRecord(walRecord);
    this->WaitForAcks(newLsn);

    send_all(clientSocket, "OK " + std::to_string(newLsn) + "\n");
  } else {
    send_all(clientSocket, "ERR_WRITE_FAILED\n");
  }
}

void Leader::HandleDel(sock_t clientSocket, const vector<string> &tokens) {
  WalRecord walRecord(0, WalOperation::DELETE, tokens[1]);

  auto newLsn = this->duombaze->ExecuteLogDeleteWithLSN(walRecord.key);

  if (newLsn > 0) {
    walRecord.lsn = newLsn;

    this->BroadcastWalRecord(walRecord);
    this->WaitForAcks(newLsn);

    send_all(clientSocket, "OK " + std::to_string(newLsn) + "\n");
  } else {
    send_all(clientSocket, "ERR_WRITE_FAILED\n");
  }
}

void Leader::HandleGet(sock_t clientSocket, const string &key) {
  auto result = this->duombaze->Get(key);
  string message = result.has_value() ? "VALUE " + format_length_prefixed_value(result->value) + "\n" : "NOT_FOUND\n";
  send_all(clientSocket, message);
}

void Leader::HandleRangeQuery(sock_t clientSocket, const vector<string> &tokens, bool forward) {
  try {
    auto startKey = string(tokens[1]);
    auto count = std::stoul(tokens[2]);

    auto results = forward
      ? this->duombaze->GetFF(startKey, count)
      : this->duombaze->GetFB(startKey, count);

    for (const auto &cell : results) {
      send_all(clientSocket, "KEY_VALUE "+ cell.key + " " + format_length_prefixed_value(cell.value) + "\n");
    }
    send_all(clientSocket, "END\n");
  } catch (const std::exception& e) {
      send_all(clientSocket, "ERR " + std::string(e.what()) + "\n");
  }
}

void Leader::HandleOptimize(sock_t clientSocket) {
  try {
    this->duombaze->Optimize();
    send_all(clientSocket, "OK_OPTIMIZED\n");
  } catch(const std::exception& e) {
      send_all(clientSocket, "ERR " + string(e.what()) + "\n");
  }
}

void Leader::HandleCompact(sock_t clientSocket) {
  string msg;
  bool success = this->PerformCompaction(msg);

  send_all(clientSocket, msg + "\n");
}

bool Leader::IsClusterHealthy() {
  std::lock_guard<mutex> lock(this->mtx);

  // 1. CLUSTER dydis iš rules.hpp.
  int totalNodesInConfig = sizeof(CLUSTER) / sizeof(NodeInfo);

  // 2. Reikalingi (Total - 1), nes lyderis nesiskaičiuoja.
  int requiredFollowers = totalNodesInConfig - 1;

  int activeFollowers = 0;
  for (auto &follower : this->followers) {
    if (follower->isAlive) {
      activeFollowers++;
    }
  }

  return activeFollowers >= requiredFollowers;
}

void Leader::BroadcastReset() {
  log_line(LogLevel::INFO, "Broadcasting RESET_WAL to followers...");

  std::lock_guard<mutex> listLock(this->mtx);

  for (auto &follower: this->followers) {
    if (follower->isAlive) {
      std::lock_guard<mutex> ioLock(follower->connectionMutex);

      // Siunčiame komandą.
      if (!send_all(follower->followerSocket, "RESET_WAL\n")) {
        log_line(LogLevel::WARN, "Failed to send RESET_WAL to a follower");
        follower->isAlive = false;
      }
    }
  }
}

bool Leader::PerformCompaction(string &statusMsg) {
  log_line(LogLevel::INFO, "Attempting Compaction...");

  // 1. Tikriname ar visos egzistuojančios replikacijos gyvos.
  if (!this->IsClusterHealthy()) {
    statusMsg = "ERR_CLUSTER_NOT_FULL_CANNOT_COMPACT";
    log_line(LogLevel::WARN, "Compaction aborted: Cluster not full.");
    return false;
  }

  // 2. Stabdome pasaulį.
  this->maintenanceMode = true;
  log_line(LogLevel::WARN, "--- STOP THE WORLD: MAINTENANCE STARTED ---");

  // 3. Laukiame kol visi follower'iai susisinchronizuos pagal lyderio LSN.
  auto currentLSN = this->duombaze->getLSN();
  log_line(LogLevel::INFO, "Waiting for followers to catch up to LSN: " + std::to_string(currentLSN));

  bool allSynced = false;
  for (int i = 0; i < CONSTS::TRIES_FOR_COMPACT; i++) {
    int syncedCount = 0;
    {
      std::lock_guard<mutex> lock(this->mtx);
      for (auto &follower: this->followers) {
        if (follower->isAlive && follower->ackedUptoLsn >= currentLSN) {
          syncedCount++;
        }
      }
    }

    if (syncedCount >= ((sizeof(CLUSTER)/sizeof(NodeInfo)) - 1)) {
      allSynced = true;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(CONSTS::SLEEP_BEFORE_CHECKING_NODES));
  }

  if (!allSynced) {
    this->maintenanceMode = false;
    statusMsg = "ERR_TIMEOUT_WAITING_FOR_SYNC";
    log_line(LogLevel::ERROR, "Compaction failed: Timeout waiting for followers.");
    return false;
  }

  // 4. Jeigu viskas OK, tai Broadcast'iname kiekvienam follower'iui, kad reset'intų WAL.
  this->BroadcastReset();

  // 5. Laukiame, kol visi follower'iai atsakys.
  std::this_thread::sleep_for(std::chrono::seconds(2));

  // 6. Pats (lyderis) reset'ina WAL.
  log_line(LogLevel::INFO, "Resetting LEADER logs...");
  this->duombaze->ResetLogState();

  // 7. Pasaulis vėl sukasi.
  this->maintenanceMode = false;
  statusMsg = "OK_COMPACTED\n";
  log_line(LogLevel::INFO, "--- COMPACTION SUCCESSFUL ---");
  return true;
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
      if (this->maintenanceMode && command != "COMPACT") {
        send_all(clientSocket, "ERR_MAINTENANCE_MODE_TRY_LATER\n");
        continue;
      }

      if (command == "SET" && tokens.size() >= 4) {
        this->HandleSet(clientSocket, tokens);
      } else if (command == "DEL" && tokens.size() == 2) {
        this->HandleDel(clientSocket, tokens);
      } else if (command == "GET" && tokens.size() >= 2) {
        this->HandleGet(clientSocket, tokens[1]);
      } else if (command == "GETFF" && tokens.size() >= 3) {
        this->HandleRangeQuery(clientSocket, tokens, true);
      } else if (command == "GETFB" && tokens.size() >= 3) {
        this->HandleRangeQuery(clientSocket, tokens, false);
      } else if (command == "OPTIMIZE") {
        this->HandleOptimize(clientSocket);
      } else if (command == "COMPACT") {
        this->HandleCompact(clientSocket);
      } else {
        send_all(clientSocket, "ERR usage: SET|GET|DEL|GETFF|GETFB|OPTIMIZE\n");
      }
    }
  } catch (const std::exception& ex) {
    log_line(LogLevel::ERROR, string("Exception in client handler: ") + ex.what());
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

    auto clientPort = std::stoi(argv[1]);
    auto followerPort = std::stoi(argv[2]);
    string dbName = argv[3];
    auto requiredAcks = std::stoi(argv[4]);

    // host'as – optional 5-as argumentas, kad galėtum atspausdinti IP arba hostname'e
    string host = (argc >= 6) ? string(argv[5]) : string("unknown-host");

    // Sukuriam Leader instance ir runninam.
    Leader leader(dbName, clientPort, followerPort, requiredAcks, host);

    leader.Run();

    return 0;
  } catch (const std::exception& ex) {
    log_line(LogLevel::ERROR, string("Fatal exception in leader main: ") + ex.what());
    return 1;
  } catch (...) {
    log_line(LogLevel::ERROR, "Unknown fatal exception in leader main");
    return 1;
  }
}
