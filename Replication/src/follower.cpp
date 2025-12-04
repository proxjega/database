#include "../include/follower.hpp"
#include "../include/rules.hpp"
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <chrono>
#include <string>
#include <sys/types.h>

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

Follower::Follower(string leaderHost, uint16_t leaderPort, string dbName, uint16_t readPort, int nodeId)
    : leaderHost(std::move(leaderHost)), leaderPort(leaderPort), dbName(std::move(dbName)), readPort(readPort), nodeId(nodeId) {

    log_line(LogLevel::INFO, "[Follower] Init. NodeID: " + std::to_string(this->nodeId) +
            " Leader: " + this->leaderHost +
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
        if (failureCount >= MAX_FAILURES_BEFORE_EXIT) {
            log_line(LogLevel::ERROR, "Too many failures talking to leader (HELLO), exiting");
            std::exit(2);
        }

        // 1. Bandome susiconnect'int su leader.
        sock_t leaderSocket = this->TryConnect();

        if (leaderSocket == NET_INVALID) {
            failureCount++;
            log_line(LogLevel::WARN,
                "connect to leader failed, sleeping " + std::to_string(backoffMs) +
                " ms before retry (failure_count=" + std::to_string(failureCount) + ")");

            std::this_thread::sleep_for(std::chrono::milliseconds(backoffMs));
            backoffMs = std::min(backoffMs * 2, MAX_BACKOFF_MS);
            continue;
        }

        // Užtikriname, kad nekabėsime, jei lyderis prapuls.
        set_socket_timeouts(leaderSocket, Consts::SOCKET_TIMEOUT_MS);
        this->currentLeaderSocket = leaderSocket;

        // 2. Handshake.
        if (!this->PerformHandshake(lastAppliedLsn)) {
            net_close(this->currentLeaderSocket);
            this->currentLeaderSocket = NET_INVALID;

            failureCount++;
            std::this_thread::sleep_for(std::chrono::milliseconds(backoffMs));
            backoffMs = std::min(backoffMs * 2, MAX_BACKOFF_MS);
            continue;
        }

        // 3. Atliekame replikaciją.
        auto status = this->RunReplicationSession(lastAppliedLsn);

        // 4. Disconnect.
        net_close(this->currentLeaderSocket);
        this->currentLeaderSocket = NET_INVALID;

        // 5. Įvertiname replikacijos rezultatą.
        if (status == SessionStatus::PROTOCOL_ERROR) {
            log_line(LogLevel::ERROR, "Leader violated protocol! Incrementing failure count.");
            failureCount++;
        } else if (status == SessionStatus::CLEAN_DISCONNECT) {
            failureCount = 0;
            backoffMs = BASE_BACKOFF_MS;
        }

        // 7. Šiek tiek pamiegame prieš reconnect bandymą.
        std::this_thread::sleep_for(std::chrono::milliseconds(backoffMs));
    }
}

sock_t Follower::TryConnect() {
    log_line(LogLevel::INFO, "trying to connect to leader " + this->leaderHost + ":" + std::to_string(this->leaderPort));
    sock_t leaderSocket = tcp_connect(this->leaderHost, this->leaderPort);

    if (leaderSocket == NET_INVALID) {
        return leaderSocket;
    }

    log_line(LogLevel::INFO, "Connected to Leader at " + this->leaderHost + ":" + std::to_string(this->leaderPort));
    return leaderSocket;
}

bool Follower::PerformHandshake(uint64_t &myLsn) {
    // Format: HELLO <nodeId> <lastAppliedLsn>
    string helloMsg = "HELLO " + std::to_string(this->nodeId) + " " + std::to_string(myLsn) + "\n";
    if (!send_all(this->currentLeaderSocket, helloMsg)) {
        log_line(LogLevel::WARN, "Failed to send HELLO");
        return false;
    }

    log_line(LogLevel::INFO, "Sent HELLO nodeId=" + std::to_string(this->nodeId) + " LSN=" + std::to_string(myLsn));
    return true;
}

SessionStatus Follower::RunReplicationSession(uint64_t &myLsn) {
    string line;
    while (this->running && recv_line(this->currentLeaderSocket, line)) {
        if (!this->ProccessCommandLine(line, myLsn)) {
            log_line(LogLevel::ERROR, "Replication protocol error");
            return SessionStatus::PROTOCOL_ERROR;
        }
    }

    return SessionStatus::CLEAN_DISCONNECT;
}

bool Follower::ProccessCommandLine(const string &line, uint64_t &myLsn) {
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
    sock_t listenSocket = NET_INVALID;

    // Retry binding up to 10 times with 2-second delay
    // This handles TIME_WAIT state after process restarts during role transitions
    for (int retry = 0; retry < 10 && this->running; ++retry) {
        listenSocket = tcp_listen(this->readPort);
        if (listenSocket != NET_INVALID) {
            break;  // Success!
        }

        log_line(LogLevel::WARN,
                 "Follower read-only bind failed on port " + std::to_string(this->readPort) +
                 ", retrying in 2s (attempt " + std::to_string(retry + 1) + "/10)");

        std::this_thread::sleep_for(std::chrono::seconds(2));
    }

    if (listenSocket == NET_INVALID) {
        log_line(LogLevel::ERROR,
                 "Follower cannot bind read-only port " + std::to_string(this->readPort) +
                 " after 10 retries, exiting ServeReadOnly");
        return;  // Only return after exhausting retries
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

void Follower::HandleGetKeys(sock_t sock, const vector<string> &tokens) {
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
            send_all(sock, "KEY " + key + "\n");
        }
        send_all(sock, "END\n");
    } catch (const std::exception& e) {
        send_all(sock, "ERR " + std::string(e.what()) + "\n");
    }
}

void Follower::HandleGetKeysPaging(sock_t sock, const vector<string> &tokens) {
    try {
        // GETKEYSPAGING <pageSize> <pageNum>
        auto pageSize = std::stoul(tokens[1]);
        auto pageNum = std::stoul(tokens[2]);

        auto result = this->duombaze->GetKeysPaging(pageSize, pageNum);

        // Send total count first
        send_all(sock, "TOTAL " + std::to_string(result.totalItems) + "\n");

        // Send each key
        for (const auto& key : result.keys) {
            send_all(sock, "KEY " + key + "\n");
        }
        send_all(sock, "END\n");
    } catch (const std::exception& e) {
        send_all(sock, "ERR " + std::string(e.what()) + "\n");
    }
}

void Follower::HandleRedirect(sock_t sock) {
    send_all(sock, "REDIRECT " + this->leaderHost + " " + std::to_string(CLIENT_PORT) + "\n");
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
            else if (command == "GETKEYS" && tokens.size() >= 1) {
                HandleGetKeys(clientSocket, tokens);
            }
            else if (command == "GETKEYSPAGING" && tokens.size() == 3) {
                HandleGetKeysPaging(clientSocket, tokens);
            }
            else {
                // Reject all write operations (SET, PUT, DEL) and unsupported commands
                // Followers are read-only and support: GET, GETFF, GETFB, GETKEYS, GETKEYSPAGING
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
        int nodeId = (argc >= 7) ? std::stoi(argv[6]) : 0;  // Optional node ID

        Follower follower(leaderHost, leaderPort, dbName, readPort, nodeId);
        follower.Run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
