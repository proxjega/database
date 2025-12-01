#include "../include/follower.hpp"
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <chrono>
#include <string>
#include <sys/types.h>

static constexpr int MAX_FAILURES_BEFORE_EXIT = 5;
static constexpr int BASE_BACKOFF_MS = 1000;
static constexpr int MAX_BACKOFF_MS = 30000;

// Paprastas logger'is šitam faile – tik su "Follower" prefiksu, kad atskirt nuo kitų komponentų.
namespace {
    void FollowerLog(LogLevel lvl, const string &msg) {
        const char* tag = nullptr;
        switch (lvl) {
            case LogLevel::DEBUG: tag = "DEBUG"; break;
            case LogLevel::INFO:  tag = "INFO";  break;
            case LogLevel::WARN:  tag = "WARN";  break;
            case LogLevel::ERROR: tag = "ERROR"; break;
            default:              tag = "LOG";   break;
        }
        std::cerr << "[Follower][" << tag << "] " << msg << "\n";
    }
}

Follower::Follower(string leaderHost, uint16_t leaderPort, string dbName, uint16_t readPort)
    : leaderHost(std::move(leaderHost)), leaderPort(leaderPort), dbName(std::move(dbName)), readPort(readPort) {

    log_line(LogLevel::INFO, "[Follower] Init. Leader: " + this->leaderHost +
            ":" + std::to_string(this->leaderPort) +
             " DB: " + this->dbName + " ReadPort: " + std::to_string(this->readPort));

    this->duombaze = std::make_unique<Database>(this->dbName);
}

Follower::~Follower() {
    log_line(LogLevel::INFO, "[Follower] Shutting down...");
    this->running = false;

    if (this->readListenSocket != NET_INVALID) {
        net_close(this->readListenSocket);
        this->readListenSocket = NET_INVALID;
    }

    // 2. Close the connection to the leader to stop Sync loop (SyncWithLeaderLoop)
    if (this->currentLeaderSocket != NET_INVALID) {
        net_close(this->currentLeaderSocket);
        this->currentLeaderSocket = NET_INVALID;
    }

    // 3. Join the background thread
    if (this->readOnlyThread.joinable()) {
        this->readOnlyThread.join();
    }
}

void Follower::Run() {
    // 1. Pradedame sinchronizacija su lyderiu background'e.
    this->readOnlyThread= thread(&Follower::ServeReadOnly, this);

    // 2. Aktyvuojame Read-Only pagrindiniame thread'e.
    this->SyncWithLeader();

    // 3. Prisijungiame prie backgroud thread'o, jei Read-Only (ServeReadOnly) nebeveikia.
    if (this->readOnlyThread.joinable()) {
        this->readOnlyThread.join();
    }
}

void Follower::SyncWithLeader() {
    int failureCount = 0;
    int backoffMs = BASE_BACKOFF_MS;
    auto lastAppliedLsn = this->duombaze->getLSN();
    log_line(LogLevel::DEBUG, "LSN FROM META PAGE: " + std::to_string(lastAppliedLsn));

    while (this->running) {
        // 1. Bandome susiconnect'int su leader.
        sock_t leaderSocket = this->TryConnect();

        if (leaderSocket == NET_INVALID) {
            failureCount++;
            log_line(LogLevel::WARN,
                "connect to leader failed, sleeping " + std::to_string(backoffMs) +
                " ms before retry (failure_count=" + std::to_string(failureCount) + ")");

            std::this_thread::sleep_for(std::chrono::seconds(backoffMs));
            backoffMs = std::min(backoffMs * 2, MAX_BACKOFF_MS);
            continue;
        }

        if (failureCount >= MAX_FAILURES_BEFORE_EXIT) {
            log_line(LogLevel::ERROR, "Too many failures talking to leader (HELLO), exiting");
            std::exit(2);
        }

        // Užtikriname, kad nekabėsime, jei lyderis prapuls.
        backoffMs = BASE_BACKOFF_MS;
        set_socket_timeouts(leaderSocket, Consts::SOCKET_TIMEOUT_MS);
        this->currentLeaderSocket = leaderSocket;

        // 2. Handshake.
        if (!this->PerformHandshake(lastAppliedLsn)) {
            net_close(this->currentLeaderSocket);
            this->currentLeaderSocket = NET_INVALID;

            // failureCount++;
            std::this_thread::sleep_for(std::chrono::milliseconds(backoffMs));
            backoffMs = std::min(backoffMs * 2, MAX_BACKOFF_MS);
            continue;
        }

        // 3. Susiconnect'inome – reset'inam backoff.
        backoffMs = BASE_BACKOFF_MS;

        // 4. Atliekame replikaciją.
        bool sessionUseful = this->RunReplicationSession(lastAppliedLsn);

        // 5. Disconnect.
        net_close(this->currentLeaderSocket);
        this->currentLeaderSocket = NET_INVALID;

        // 6. Įvertiname replikacijos rezultatą.
        if (!sessionUseful) {
            failureCount++;
            log_line(LogLevel::WARN, "Disconnected without updates. Failures: " + std::to_string(failureCount));
        } else {
            failureCount = 0;
        }

        // 8. Šiek tiek pamiegame prieš reconnect bandymą.
        std::this_thread::sleep_for(std::chrono::milliseconds(backoffMs));
        backoffMs = std::min(backoffMs * 2, MAX_BACKOFF_MS);
    }
}

sock_t Follower::TryConnect() {
    log_line(LogLevel::INFO, "trying to connect to leader " + this->leaderHost + ":" + std::to_string(this->leaderPort));
    sock_t leaderSocket = tcp_connect(this->leaderHost, this->leaderPort);

    log_line(LogLevel::INFO, "Connected to Leader at " + this->leaderHost + ":" + std::to_string(this->leaderPort));
    return leaderSocket;
}

bool Follower::PerformHandshake(uint64_t &myLsn) {
    string helloMsg = "HELLO " + std::to_string(myLsn) + "\n";
    if (!send_all(this->currentLeaderSocket, helloMsg)) {
        log_line(LogLevel::WARN, "Failed to send HELLO");
        return false;
    }

    log_line(LogLevel::INFO, "Sent HELLO " + std::to_string(myLsn));
    return true;
}

bool Follower::RunReplicationSession(uint64_t &myLsn) {
    bool receivedUpdate = false;
    string line;

    while (this->running && recv_line(this->currentLeaderSocket, line)) {
        if (!this->ProccessCommandLine(line, myLsn, receivedUpdate)) {
            log_line(LogLevel::ERROR, "Replication protocol error");
            return receivedUpdate;
        }
    }

    return receivedUpdate;
}

bool Follower::ProccessCommandLine(const string &line, uint64_t &myLsn, bool &receivedUpdate) {
    auto tokens = split(trim(line), ' ');
    if (tokens.empty()) {
        return true;
    }

    try {
        string &command = tokens[0];
        bool success = false;

        if (command == "WRITE" && tokens.size() >= 5) {
            success = this->ApplySetRecord(tokens, myLsn);
        } else if (command == "DELETE" && tokens.size() >= 3) {
            success = this->ApplyDeleteRecord(tokens, myLsn);
        } else if (command == "RESET_WAL") {
            success = this->ApplyResetWAL(myLsn);
        }

        if (success) {
            send_all(this->currentLeaderSocket, "ACK " + std::to_string(myLsn) + "\n");
            receivedUpdate = true;
        }

        return true;
    } catch (const std::exception& ex) {
        FollowerLog(LogLevel::ERROR, string("exception in replication loop: ") + ex.what());
        return false;
    } catch (...) {
        FollowerLog(LogLevel::ERROR, "unknown exception in replication loop");
        return false;
    }
}

bool Follower::ApplySetRecord(const vector<string> &tokens, uint64_t &currentLsn) {
    uint64_t lsn = std::stoull(tokens[1]);
    if (lsn <= currentLsn) {
        return true;
    }

    auto key = string(tokens[2]);
    string value;

    if (!parse_length_prefixed_value(tokens, 3, this->currentLeaderSocket, value)) {
        FollowerLog(LogLevel::ERROR, "Failed to parse value in WRITE");
        return false;
    }

    WalRecord walRecord(lsn, WalOperation::SET, key, value);

    if (!this->duombaze->ApplyReplication(walRecord)) {
        FollowerLog(LogLevel::ERROR, "Failed to apply replication for LSN " + std::to_string(lsn));
        return false;
    }

    currentLsn = lsn;
    return true;
}

bool Follower::ApplyDeleteRecord(const vector<string> &tokens, uint64_t &currentLsn) {
    uint64_t lsn = std::stoull(tokens[1]);
    if (lsn <= currentLsn) {
        return true;
    }

    auto key = string(tokens[2]);

    WalRecord walRecord(lsn, WalOperation::DELETE, key);

    if (!this->duombaze->ApplyReplication(walRecord)) {
        FollowerLog(LogLevel::ERROR, "Failed to apply replication for LSN " + std::to_string(lsn));
        return false;
    }

    currentLsn = lsn;
    return true;
}

bool Follower::ApplyResetWAL(uint64_t &localLSN) {
    FollowerLog(LogLevel::WARN, "Received RESET_WAL from Leader. Clearing logs...");

    // 1. RESET!
    this->duombaze->ResetLogState();

    // 2. Reset'inam local LSN.
    localLSN = this->duombaze->getLSN();

    return true;
}

void Follower::ServeReadOnly() {
    sock_t listenSocket = tcp_listen(this->readPort);
    if (listenSocket == NET_INVALID) {
        log_line(LogLevel::ERROR, "Follower Read-Only listen failed on " + std::to_string(this->readPort));
        return;
    }

    this->readListenSocket = listenSocket;
    log_line(LogLevel::INFO, "Follower serving Read-Only on " + std::to_string(this->readPort));

    while (this->running) {
        sock_t clientSocket = tcp_accept(listenSocket);
        if (clientSocket == NET_INVALID) {
            continue;
        }

        // Nedidelis timeout, kad klientas neužkabintų ryšio amžinai
        set_socket_timeouts(clientSocket, Consts::SOCKET_TIMEOUT_MS);
        thread(&Follower::HandleClient, this, clientSocket).detach();
    }
}

void Follower::HandleGet(sock_t sock, const string &key) {
    auto result = this->duombaze->Get(key);
    string message = result.has_value() ? "VALUE " + format_length_prefixed_value(result->value) + "\n" : "NOT_FOUND\n";
    send_all(sock, message);
}

void Follower::HandleRangeQuery(sock_t sock, const vector<string> &tokens, bool forward) {
    try {
        auto startKey = string(tokens[1]);
        auto count = std::stoul(tokens[2]);

        auto results = forward
            ? this->duombaze->GetFF(startKey, count)
            : this->duombaze->GetFB(startKey, count);

        for (const auto &cell : results) {
            send_all(sock, "KEY_VALUE " + cell.key + " " + format_length_prefixed_value(cell.value));
        }
        send_all(sock, "END\n");
    } catch (const std::exception& e) {
        send_all(sock, "ERR " + std::string(e.what()) + "\n");
    }
}

void Follower::HandleRedirect(sock_t sock) {
    send_all(sock, "REDIRECT " + this->leaderHost + " " + std::to_string(this->leaderPort) + "\n");
}

void Follower::HandleClient(sock_t clientSocket) {
    try {
        string line;
        while (this->running && recv_line(clientSocket, line)) {
            auto tokens = split((line), ' ');
            if (tokens.empty()) {
                continue;
            }

            string &command = tokens[0];

            if (command == "GET" && tokens.size() >= 2) {
                HandleGet(clientSocket, tokens[1]);
            }
            else if (command == "GETFF" && tokens.size() >= 3) {
                HandleRangeQuery(clientSocket, tokens, true);
            }
            else if (command == "GETFB" && tokens.size() >= 3) {
                HandleRangeQuery(clientSocket, tokens, false);
            }
            else if (command == "SET" || command == "PUT" || command == "DEL") {
                HandleRedirect(clientSocket);
            }
            else {
                send_all(clientSocket, "ERR_READ_ONLY\n");
            }
        }
    } catch (const std::exception& ex) {
        FollowerLog(LogLevel::ERROR,
                std::string("exception in read_only client thread: ") + ex.what());
    } catch (...) {
        FollowerLog(LogLevel::ERROR, "unknown exception in read_only client thread");
    }

    net_close(clientSocket);
}

/* ========= main ========= */

int main(int argc, char** argv) {
    // Tikrinam argumentų skaičių:
    // follower <leader_host> <leader_follower_port> <db_name> <snapshot_path> <read_port> [node_id]
    if (argc < 6) {
        std::cerr << "Usage: follower <leader_host> <leader_follower_port> "
                        "<db_name> <snapshot_path> <read_port> [node_id]\n";
        return 1;
    }

    try {
        string leaderHost = argv[1];
        auto leaderPort = std::stoi(argv[2]);
        string dbName = argv[3];
        auto readPort = std::stoi(argv[5]);

        Follower follower(leaderHost, leaderPort, dbName, readPort);
        follower.Run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
