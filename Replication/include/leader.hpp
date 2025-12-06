#pragma once

#include "common.hpp"
#include "../../btree/include/database.h"
#include <vector>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <string>
#include <atomic>

using std::string;
using std::vector;
using std::unique_ptr;
using std::shared_ptr;
using std::mutex;
using std::condition_variable;
using std::thread;
using std::atomic;

// Informacija apie vieną follower'io ryšį leader'yje.
// Laikom socket'ą, iki kokio lsn follower'is patvirtino (ACK),
// ir ar jis dar laikomas gyvu.
struct FollowerConnection {
  int      id{0};  // Follower node ID (1-4)
  sock_t   followerSocket{NET_INVALID};
  uint64_t ackedUptoLsn{0};
  bool     isAlive{true};
  uint64_t lastSeenMs{0};  // Timestamp when follower was last seen (for status caching)
  mutex    connectionMutex;
};

// How long to consider a follower "recently seen" for status reporting (10 seconds)
static constexpr uint64_t FOLLOWER_STATUS_CACHE_MS = 10000;

class Leader {
private:
    // Configuration
    string dbName;
    uint16_t clientPort;
    uint16_t followerPort;
    int requiredAcks;
    string host;

    // State
    unique_ptr<Database> duombaze;
    vector<shared_ptr<FollowerConnection>> followers;
    bool running{true};
    atomic<bool> maintenanceMode{false}; // "Stabdome pasaulį" flag'as.

    // Synchronization
    mutex mtx;
    condition_variable conditionVariable;

    // Thread Management
    thread announceThread;
    thread followerAcceptThread;
    thread clientAcceptThread;
    thread compactionThread;

    // We store listening sockets to close them in destructor (waking up accept threads)
    atomic<sock_t> clientListenSocket{NET_INVALID};
    atomic<sock_t> followerListenSocket{NET_INVALID};

    // Thread Loops
    void AnnouncePresence();
    void AcceptFollowers();
    void HandleFollower(sock_t followerSocket);
    void ServeClients();
    void HandleClient(sock_t clientSocket);

    // Helper'iai, kad nebūtų painus kodas.
    void HandleSet(sock_t clientSocket, const vector<string> &tokens);
    void HandleDel(sock_t clientSocket, const vector<string> &tokens);
    void HandleGet(sock_t clientSocket, const string &key);
    void HandleRangeQuery(sock_t clientSocket, const vector<string> &tokens, bool forward);
    void HandleOptimize(sock_t clientSocket);
    void HandleGetKeys(sock_t clientSocket, const vector<string> &tokens);
    void HandleGetKeysPaging(sock_t clientSocket, const vector<string> &tokens);

    // Logic
    void BroadcastWalRecord(const WalRecord &walRecord);
    size_t CountAcks(uint64_t lsn);
    void WaitForAcks(uint64_t lsn);

    // "Stabdome pasaulį" logika.
    void AutoCompactLoop();
    bool IsClusterHealthy();

    // Quorum checks for distributed consensus
    int CountAliveFollowers();
    bool HasQuorum();  // Returns true if >= 2 followers alive (3+ total nodes)
    void HandleCompact(sock_t clientSocket);
    bool PerformCompaction(string &statusMsg);
    void BroadcastReset();

public:
    Leader(string dbName, uint16_t clientPort, uint16_t followerPort, int requiredAcks, string host);
    ~Leader();

    // Main loop: starts threads and blocks.
    void Run();
};