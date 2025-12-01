#pragma once

#include "../../btree/include/database.h"
#include "common.hpp"
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using std::string;
using std::atomic;
using std::thread;
using std::vector;

class Follower {
private:
    string leaderHost;
    uint16_t leaderPort;
    string dbName;
    uint16_t readPort;

    std::unique_ptr<Database> duombaze;
    bool running{true};

    atomic<sock_t> readListenSocket{NET_INVALID};
    atomic<sock_t> currentLeaderSocket{NET_INVALID};
    thread readOnlyThread;


    // Susije su connect'ingu prie leader.
    void SyncWithLeader(); // Veikia background thread'e.
    sock_t TryConnect();
    bool PerformHandshake(uint64_t &myLsn);

    // Replikacijos logika.
    bool RunReplicationSession(uint64_t &myLsn);
    bool ApplySetRecord(const vector<string> &tokens, uint64_t &currentLsn);
    bool ApplyDeleteRecord(const vector<string> &tokens, uint64_t &currentLsn);
    bool ProccessCommandLine(const string &line, uint64_t &myLsn, bool &receivedUpdate);

    // Susije su klientu.
    void ServeReadOnly(); // Veikia main thread'e.
    void HandleClient(sock_t clientSocket);
    void HandleGet(sock_t sock, const string &key);
    void HandleRangeQuery(sock_t sock, const vector<string> &tokens, bool forward);
    void HandleRedirect(sock_t sock);

    // Reset'inimas.
    bool ApplyResetWAL(uint64_t &localLSN);

public:
    Follower(string leaderHost, uint16_t leaderPort, string dbName, uint16_t readPort);
    ~Follower();

    void Run();
};