#pragma once

#include <cstdint>
#include <string>
#include <mutex>
#include <atomic>
#include <iostream>

// Statinė informacija apie vieną klasterio mazgą.
struct NodeInfo {
    int     id;     // loginis mazgo ID (1..N)
    std::string host;   // IP arba DNS vardas
    uint16_t    port;   // valdymo (election/heartbeat) prievadas
};

// Fiksuotas klasterio narių sąrašas.
// Use 'static' to allow inclusion in multiple translation units.
static NodeInfo CLUSTER[] = {
    {1, "127.0.0.1", 8001},  // node1
    {2, "127.0.0.2",  8002},  // node2
    {3, "127.0.0.3",  8003},  // node3
    {4, "127.0.0.4",  8004},  // node4
};

// Lyderio atviri prievadai.
static constexpr uint16_t CLIENT_PORT   = 7001;  // klientų API (SET/GET/DEL)
static constexpr uint16_t REPL_PORT     = 7002;  // replikacija leader -> followers

// Follower'io read-only prievadai: 7101, 7102, 7103, ...
static constexpr uint16_t FOLLOWER_READ_BASE = 7100;
static inline uint16_t FOLLOWER_READ_PORT(int nodeId) {
    return static_cast<uint16_t>(FOLLOWER_READ_BASE + nodeId);
}

// Laiko parametrai (ms).
static constexpr int HEARTBEAT_INTERVAL_MS = 400;   // kas kiek siųsti heartbeat'ą
static constexpr int HEARTBEAT_TIMEOUT_MS  = 1500;  // po kiek laikyti leader'į mirusiu
static constexpr int ELECTION_TIMEOUT_MS   = 1200;  // minimalus laukimas iki rinkimų starto

// Mazgo būsena Raft stiliaus protokole.
enum class NodeState : uint8_t { FOLLOWER, CANDIDATE, LEADER };

// Bendras lokalaus mazgo klasterio stovis.
struct ClusterState {
    std::atomic<NodeState> state{NodeState::FOLLOWER}; // dabartinė rolė
    std::atomic<int>       leaderId{0};                // žinomas leader'io ID (0 jei nežinomas)
    std::atomic<bool>      alive{true};                // ar mazgas dar turi veikti
    std::mutex             cout_mx;                    // sinchronizavimui stdout log'ams
};

// Glaustas log'inimo helperis vienam mazgui.
static inline void log_msg(ClusterState& clusterState,
                           int selfNodeId,
                           const std::string& message) {
    std::lock_guard<std::mutex> lock(clusterState.cout_mx);
    std::cout << "[node " << selfNodeId << "] " << message << "\n";
}

// Suranda NodeInfo pagal mazgo ID arba grąžina nullptr, jei tokio nėra.
static inline const NodeInfo* getNode(int nodeId) {
    for (auto& node : CLUSTER) {
        if (node.id == nodeId) {
            return &node;
        }
    }
    return nullptr;
}