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
             " DB:" + this->dbName + " ACKS:" + std::to_string(this->requiredAcks) +
             " (quorum enforcement: requires " + std::to_string(this->requiredAcks + 1) + "+ nodes)");

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

    // Pass socket directly to HandleFollower - it will read HELLO first to get nodeId,
    // then find/reuse or create a connection slot
    thread(&Leader::HandleFollower, this, followerSocket).detach();
  }
}

// Kiekvienam follower'iui skirtas thread'as, kuris:
// 1) Perskaito HELLO <last_lsn> iš follower'io;
// 2) Nusiunčia jam trūkstamus WAL įrašus;
// 3) Laukia iš jo ACK pranešimų.
void Leader::HandleFollower(sock_t followerSocket) {
  shared_ptr<FollowerConnection> follower = nullptr;

  try {
    // 1. HELLO iš follower'io.
    string helloLine;
    if (!recv_line(followerSocket, helloLine)) {
      net_close(followerSocket);
      return;
    }

    // Čia kažkodėl siuntinėja pastoviai su kiekvienu request'u "GET __verify__". Tai ignore
    if (helloLine.rfind("GET", 0) == 0) {
       net_close(followerSocket);
       return;
    }

    auto helloParts = split(helloLine, ' ');
    // New format: HELLO <nodeId> <lastAppliedLsn>
    // Old format: HELLO <lastAppliedLsn> (for backward compatibility)
    if (helloParts.size() < 2 || helloParts.size() > 3 || helloParts[0] != "HELLO") {
      log_line(LogLevel::WARN, "Follower sent bad HELLO: " + helloLine);
      net_close(followerSocket);
      return;
    }

    int nodeId = 0;
    uint64_t lastAppliedLsn = 0;

    try {
      if (helloParts.size() == 3) {
        // New format: HELLO <nodeId> <lastAppliedLsn>
        nodeId = std::stoi(helloParts[1]);
        lastAppliedLsn = std::stoull(helloParts[2]);
        log_line(LogLevel::INFO, "Follower HELLO: nodeId=" + std::to_string(nodeId) + " LSN=" + std::to_string(lastAppliedLsn));
      } else {
        // Old format: HELLO <lastAppliedLsn>
        lastAppliedLsn = std::stoull(helloParts[1]);
        log_line(LogLevel::INFO, "Follower HELLO (no nodeId): LSN=" + std::to_string(lastAppliedLsn));
      }
    } catch (...) {
      log_line(LogLevel::WARN, "Bad format in follower HELLO: " + helloLine);
      net_close(followerSocket);
      return;
    }

    // 2. Find or create connection slot by nodeId
    {
      std::lock_guard<mutex> lock(this->mtx);

      // Search for existing slot with this nodeId
      auto iterator = std::find_if(this->followers.begin(), this->followers.end(),
        [nodeId](const auto& follower) {
          return follower->id == nodeId && nodeId != 0;
        });

      if (iterator != this->followers.end()) {
        // Found existing slot - reuse it
        follower = *iterator;

        if (follower->isAlive) {
          // Duplicate connection! Close old socket and replace
          log_line(LogLevel::WARN, "Node " + std::to_string(nodeId) +
                   " replacing active connection");
          if (follower->followerSocket != NET_INVALID) {
            net_close(follower->followerSocket);
          }
        }

        // Update socket and mark alive
        follower->followerSocket = followerSocket;
        follower->isAlive = true;
        follower->ackedUptoLsn = lastAppliedLsn;
        follower->lastSeenMs = now_ms();
      } else {
        // No existing slot - create new one
        follower = std::make_shared<FollowerConnection>();
        follower->id = nodeId;
        follower->followerSocket = followerSocket;
        follower->isAlive = true;
        follower->ackedUptoLsn = lastAppliedLsn;
        follower->lastSeenMs = now_ms();
        this->followers.push_back(follower);

        log_line(LogLevel::INFO, "New follower slot created for node " + std::to_string(nodeId));
      }
    }

    if (!send_all(followerSocket, "OK\n")) {
       log_line(LogLevel::WARN, "Failed to send handshake ACK");
       net_close(followerSocket);
       return;
    }

    // 3. Persiunčiam follower'iui visus įrašus nuo jo paskutinio turimo LSN.
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

    // 4. Laukiam ACK.
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
          follower->lastSeenMs = now_ms();  // Update last seen on each ACK
          this->conditionVariable.notify_all();
        } catch (...) {
          log_line(LogLevel::WARN, "Bad ACK lsn from follower: " + tokens[1]);
        }
      }
    }
  } catch (const std::exception& ex) {
    log_line(LogLevel::ERROR, string("Exception in follower_thread: ") + ex.what());
    if (follower) follower->isAlive = false;
  } catch (...) {
    log_line(LogLevel::ERROR, "Unknown exception in follower_thread");
    if (follower) follower->isAlive = false;
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
  // Check quorum before accepting write operations
  if (!HasQuorum()) {
    send_all(clientSocket, "ERR_NO_QUORUM Insufficient nodes for write operation (need 3+ nodes)\n");
    log_line(LogLevel::WARN, "Rejected SET operation: no quorum (alive followers: " +
             std::to_string(CountAliveFollowers()) + ")");
    return;
  }

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

    // Verify we received enough ACKs for quorum
    size_t actualAcks = this->CountAcks(newLsn);
    if (actualAcks < static_cast<size_t>(this->requiredAcks)) {
      log_line(LogLevel::ERROR, "SET operation failed: insufficient ACKs (got " +
               std::to_string(actualAcks) + ", need " + std::to_string(this->requiredAcks) + ")");
      send_all(clientSocket, "ERR_INSUFFICIENT_ACKS Replication failed (got " +
               std::to_string(actualAcks) + " ACKs, need " + std::to_string(this->requiredAcks) + ")\n");
      return;
    }

    send_all(clientSocket, "OK " + std::to_string(newLsn) + "\n");
  } else {
    send_all(clientSocket, "ERR_WRITE_FAILED\n");
  }
}

void Leader::HandleDel(sock_t clientSocket, const vector<string> &tokens) {
  // Check quorum before accepting write operations
  if (!HasQuorum()) {
    send_all(clientSocket, "ERR_NO_QUORUM Insufficient nodes for delete operation (need 3+ nodes)\n");
    log_line(LogLevel::WARN, "Rejected DEL operation: no quorum (alive followers: " +
             std::to_string(CountAliveFollowers()) + ")");
    return;
  }

  WalRecord walRecord(0, WalOperation::DELETE, tokens[1]);

  auto newLsn = this->duombaze->ExecuteLogDeleteWithLSN(walRecord.key);

  if (newLsn > 0) {
    walRecord.lsn = newLsn;

    this->BroadcastWalRecord(walRecord);
    this->WaitForAcks(newLsn);

    // Verify we received enough ACKs for quorum
    size_t actualAcks = this->CountAcks(newLsn);
    if (actualAcks < static_cast<size_t>(this->requiredAcks)) {
      log_line(LogLevel::ERROR, "DEL operation failed: insufficient ACKs (got " +
               std::to_string(actualAcks) + ", need " + std::to_string(this->requiredAcks) + ")");
      send_all(clientSocket, "ERR_INSUFFICIENT_ACKS Replication failed (got " +
               std::to_string(actualAcks) + " ACKs, need " + std::to_string(this->requiredAcks) + ")\n");
      return;
    }

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

void Leader::HandleGetKeys(sock_t clientSocket, const vector<string>& tokens) {
  try {
    // GETKEYS [prefix]
    // If no prefix provided (tokens.size() == 1), get all keys
    // If prefix provided (tokens.size() >= 2), get keys with that prefix
    string prefix = (tokens.size() >= 2) ? tokens[1] : "";

    vector<string> keys;
    if (prefix.empty()) {
      keys = this->duombaze->GetKeys();
    } else {
      keys = this->duombaze->GetKeys(prefix);
    }

    for (const auto& key : keys) {
      send_all(clientSocket, "KEY " + key + "\n");
    }
    send_all(clientSocket, "END\n");
  } catch (const std::exception& e) {
    send_all(clientSocket, "ERR " + string(e.what()) + "\n");
  }
}

void Leader::HandleGetKeysPaging(sock_t clientSocket, const vector<string>& tokens) {
  try {
    // GETKEYSPAGING <pageSize> <pageNum>
    auto pageSize = std::stoul(tokens[1]);
    auto pageNum = std::stoul(tokens[2]);

    auto result = this->duombaze->GetKeysPaging(pageSize, pageNum);

    // Send total count first
    send_all(clientSocket, "TOTAL " + std::to_string(result.totalItems) + "\n");

    // Send each key
    for (const auto& key : result.keys) {
      send_all(clientSocket, "KEY " + key + "\n");
    }
    send_all(clientSocket, "END\n");
  } catch (const std::exception& e) {
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

int Leader::CountAliveFollowers() {
    std::lock_guard<mutex> lock(this->mtx);
    int count = 0;

    for (const auto& follower : this->followers) {
        // Count follower as alive based on connection status
        // The isAlive flag is actively maintained by the follower thread:
        // - Set to true when follower connects (HandleFollowerConnection)
        // - Set to false when recv_line fails (connection drops)
        // This is more reliable than time-based checks when there are no writes
        if (follower->isAlive) {
            count++;
        }
    }

    return count;
}

bool Leader::HasQuorum() {
    // Quorum requires >= 3 nodes total (leader + 2 followers)
    // So we need at least 2 alive followers
    return CountAliveFollowers() >= 2;
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
      } else if (command == "GETKEYS" && tokens.size() >= 1) {
        this->HandleGetKeys(clientSocket, tokens);
      } else if (command == "GETKEYSPAGING" && tokens.size() == 3) {
        this->HandleGetKeysPaging(clientSocket, tokens);
      } else if (command == "INTERNAL_FOLLOWER_STATUS") {
        // Internal command: return follower replication status for cluster health monitoring
        std::ostringstream response;
        std::lock_guard<mutex> lock(this->mtx);
        uint64_t currentTime = now_ms();

        for (auto& follower : this->followers) {
          // Report followers that are alive OR were recently seen (within cache window)
          // This handles the designed disconnect-reconnect cycle between sync sessions
          bool recentlySeen = (currentTime - follower->lastSeenMs) < FOLLOWER_STATUS_CACHE_MS;

          if (!follower->isAlive && !recentlySeen) continue;

          // Report actual connection state, but show "recently seen" followers
          string status = follower->isAlive ? "ALIVE" : "RECENT";

          response << "FOLLOWER_STATUS "
                   << follower->id << " "
                   << status << " "
                   << follower->ackedUptoLsn << "\n";
        }
        response << "END\n";
        send_all(clientSocket, response.str());
      } else {
        send_all(clientSocket, "ERR usage: SET|GET|DEL|GETFF|GETFB|GETKEYS|GETKEYSPAGING|OPTIMIZE\n");
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
