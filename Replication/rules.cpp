#include "common.hpp"

struct NodeInfo {
    int id;
    std::string host;   // IP/DNS
    uint16_t port;      // control port (election/heartbeat)
};

static NodeInfo CLUSTER[] = {
    {1, "100.93.100.112", 8001}, // node1
    {2, "100.125.32.90", 8002}, // node2
    {3, "100.96.196.71", 8003}, // node3
    {4, "100.99.168.81", 8004}, // node4
};

static constexpr uint16_t CLIENT_PORT   = 7001;   // leader client API
static constexpr uint16_t REPL_PORT     = 7002;   // leader->followers replication

// follower read-only ports: 7101, 7102, 7103, ...
static constexpr uint16_t FOLLOWER_READ_BASE = 7100;
static inline uint16_t FOLLOWER_READ_PORT(int id){
    return (uint16_t)(FOLLOWER_READ_BASE + id);
}

static constexpr int HEARTBEAT_INTERVAL_MS = 400;
static constexpr int HEARTBEAT_TIMEOUT_MS  = 1500;
static constexpr int ELECTION_TIMEOUT_MS   = 1200;

enum class NodeState { FOLLOWER, CANDIDATE, LEADER };

struct ClusterState {
    std::atomic<NodeState> state{NodeState::FOLLOWER};
    std::atomic<int> leaderId{0};
    std::atomic<bool> alive{true};
    std::mutex cout_mx;
};

static inline void log_msg(ClusterState& st, int self, const std::string& m){
    std::lock_guard<std::mutex> g(st.cout_mx);
    std::cout << "[node " << self << "] " << m << std::endl;
}

static inline const NodeInfo* getNode(int id){
    for(auto& n: CLUSTER) if(n.id==id) return &n;
    return nullptr;
}
