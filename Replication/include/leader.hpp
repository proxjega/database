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
  sock_t   followerSocket{NET_INVALID};
  uint64_t ackedUptoLsn{0};
  bool     isAlive{true};
  mutex    connectionMutex;
};

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
    void HandleFollower(shared_ptr<FollowerConnection> follower);
    void ServeClients();
    void HandleClient(sock_t clientSocket);

    // Helper'iai, kad nebūtų painus kodas.
    void HandleSet(sock_t clientSocket, const vector<string> &tokens);
    void HandleDel(sock_t clientSocket, const vector<string> &tokens);
    void HandleGet(sock_t clientSocket, const string &key);
    void HandleRangeQuery(sock_t clientSocket, const vector<string> &tokens, bool forward);
    void HandleOptimize(sock_t clientSocket);

    // Logic
    void BroadcastWalRecord(const WalRecord &walRecord);
    size_t CountAcks(uint64_t lsn);
    void WaitForAcks(uint64_t lsn);

    // "Stabdome pasaulį" logika.
    void AutoCompactLoop();
    bool IsClusterHealthy();
    void HandleCompact(sock_t clientSocket);
    bool PerformCompaction(string &statusMsg);
    void BroadcastReset();

public:
    Leader(string dbName, uint16_t clientPort, uint16_t followerPort, int requiredAcks, string host);
    ~Leader();

    // Main loop: starts threads and blocks.
    void Run();
};