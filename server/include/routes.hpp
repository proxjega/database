#pragma once
#include <cstdint>
#include <lithium_http_server.hh>
#include "symbols.hh"
#include "db_client.hpp"
#include "../../Replication/include/common.hpp"
#include "../../Replication/include/rules.hpp"
#include <string>
#include <optional>
#include <memory>
#include <sstream>
#include <vector>
#include <fstream>
#include <iomanip>
#include <mutex>

using namespace li;

inline bool ends_with(const std::string& str, const std::string& suffix) {
    if (suffix.size() > str.size()) return false;
    return str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// Helper function to discover current leader from cluster
// Uses same logic as ./client leader command

static inline bool parse_host_port(const std::string& host_port, std::string& host_out, uint16_t& port_out) {
  size_t colon_pos = host_port.find(':');
  if (colon_pos == std::string::npos) return false;

  host_out = host_port.substr(0, colon_pos);
  try {
    port_out = static_cast<uint16_t>(std::stoi(host_port.substr(colon_pos + 1)));
    return true;
  } catch (...) {
    return false;
  }
}

// CONTROL_PLANE_TUNNEL_MAP: Direct Tailscale connections (no translation needed)
// Used by cluster/status to query node status via direct Tailscale network
static const std::unordered_map<std::string, std::string> CONTROL_PLANE_TUNNEL_MAP = {
  {"100.117.80.126:8001", "100.117.80.126:8001"}, // Node 1 control plane (direct)
  {"100.70.98.49:8002",   "100.70.98.49:8002"},   // Node 2 control plane (direct)
  {"100.118.80.33:8003",  "100.118.80.33:8003"},  // Node 3 control plane (direct)
  {"100.116.151.88:8004", "100.116.151.88:8004"}  // Node 4 control plane (direct)
};

inline bool discover_leader_from_cluster(std::string& out_host, uint16_t& out_port) {
  // CLUSTER_NODES: Direct Tailscale connections to remote nodes on port 7001
  const std::vector<std::string> CLUSTER_NODES = {
    "100.117.80.126:7001",  // Node 1
    "100.70.98.49:7001",    // Node 2
    "100.118.80.33:7001",   // Node 3
    "100.116.151.88:7001"   // Node 4
  };

  // Tier 1: REDIRECT probe on follower client ports (direct Tailscale connection)
  for (const auto& node_host_port_str : CLUSTER_NODES) {

    std::string node_host;
    uint16_t node_port;
    if (!parse_host_port(node_host_port_str, node_host, node_port)) continue;

    // Connect directly to node via Tailscale
    sock_t s = tcp_connect(node_host, node_port);

    if (s != NET_INVALID) {
      // Set timeout on follower socket too
      set_socket_timeouts(s, 500);

      send_all(s, "SET __probe__ 1\n");  // Non-GET triggers REDIRECT

      std::string response;
      if (recv_line(s, response)) {
        auto tokens = split(trim(response), ' ');
        if (tokens.size() >= 3 && tokens[0] == "REDIRECT") {
          std::string redirect_host = tokens[1];    // Tailscale IP of leader
          uint16_t redirect_port = std::stoi(tokens[2]); // Port (7001)
          net_close(s);

          // Verify the REDIRECT target (direct connection, no translation needed)
          sock_t verify_s = tcp_connect(redirect_host, redirect_port);

          if (verify_s != NET_INVALID) {
            // Set aggressive 300ms timeout to quickly detect dead leaders
            set_socket_timeouts(verify_s, 300);

            // Send a test command to verify it responds
            send_all(verify_s, "GET __verify__\n");
            std::string verify_response;

            if (recv_line(verify_s, verify_response)) {
              auto verify_tokens = split(trim(verify_response), ' ');

              // Leader should respond, not timeout
              if (verify_tokens.size() > 0 &&
                  (verify_tokens[0] == "VALUE" || verify_tokens[0] == "NOT_FOUND")) {
                net_close(verify_s);

                // Return the leader's Tailscale address
                out_host = redirect_host;
                out_port = redirect_port;
                return true;
              }
            }
            net_close(verify_s);
          }
        }
      }
      net_close(s);
    }
  }

  // Tier 2: Direct leader verification
  // Check if node is not only open, but actually responds as a leader
  for (const auto& node_host_port_str : CLUSTER_NODES) {
    std::string node_host;
    uint16_t node_port;
    if (!parse_host_port(node_host_port_str, node_host, node_port)) continue;

    sock_t s = tcp_connect(node_host, node_port); // Direct Tailscale connection
    if (s != NET_INVALID) {
      // Set aggressive 500ms timeout to quickly detect dead/unresponsive leaders
      set_socket_timeouts(s, 500);

      // Send a test GET command to verify it's actually a functioning leader
      send_all(s, "GET __health_check__\n");

      std::string response;
      if (recv_line(s, response)) {
        auto tokens = split(trim(response), ' ');
        // Leader should respond with either VALUE or NOT_FOUND, not REDIRECT or ERR
        if (tokens.size() > 0 && (tokens[0] == "VALUE" || tokens[0] == "NOT_FOUND")) {
          out_host = node_host;
          out_port = node_port;
          net_close(s);
          return true;
        }
      }
      net_close(s);
    }
  }

  // No leader found
  return false;
}

// Helper to get current timestamp in milliseconds
inline uint64_t get_timestamp_ms() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

// Helper to get target host/port based on optional nodeId parameter
// Returns true on success, false if nodeId is invalid
// requireLeader: true = write operation (must target leader on CLIENT_PORT)
//                false = read operation (can target any node, followers use FOLLOWER_READ_PORT)
inline bool get_target_node(int nodeId, std::string& out_host, uint16_t& out_port, bool requireLeader = false) {
  if (nodeId > 0 && nodeId <= 4) {
    // Route to specific node
    const auto& node = CLUSTER[nodeId - 1];
    out_host = node.host;

    if (requireLeader) {
      // Write operation: must go to leader on CLIENT_PORT (7001)
      std::string leader_host;
      uint16_t leader_port;
      if (!discover_leader_from_cluster(leader_host, leader_port)) {
        return false;  // Failed to discover leader
      }
      if (out_host != leader_host) {
        return false;  // Specified node is not the leader
      }
      out_port = CLIENT_PORT;  // Leader uses CLIENT_PORT (7001)
    } else {
      // Read operation: determine port based on whether node is leader or follower
      std::string leader_host;
      uint16_t leader_port;

      if (discover_leader_from_cluster(leader_host, leader_port) && out_host == leader_host) {
        // This node is the leader, use CLIENT_PORT
        out_port = CLIENT_PORT;  // Leader on port 7001
      } else {
        // This node is a follower, use FOLLOWER_READ_PORT
        out_port = FOLLOWER_READ_PORT(nodeId);  // Follower read-only port (7101-7104)
      }
    }
    return true;
  } else if (nodeId == 0) {
    // Auto-discover leader (default behavior)
    if (discover_leader_from_cluster(out_host, out_port)) {
      out_port = CLIENT_PORT;  // Leader always on CLIENT_PORT
      return true;
    }
    return false;
  } else {
    // Invalid nodeId
    return false;
  }
}

/**
 * Properly escape JSON strings to handle special characters
 * Escapes: " \ \b \f \n \r \t and control characters
 */
inline std::string json_escape(const std::string& str) {
  std::ostringstream escaped;
  for (char c : str) {
    switch (c) {
      case '"':  escaped << "\\\""; break;
      case '\\': escaped << "\\\\"; break;
      case '\b': escaped << "\\b";  break;
      case '\f': escaped << "\\f";  break;
      case '\n': escaped << "\\n";  break;
      case '\r': escaped << "\\r";  break;
      case '\t': escaped << "\\t";  break;
      default:
        if ('\x00' <= c && c <= '\x1f') {
          escaped << "\\u" << std::hex << std::setw(4)
                  << std::setfill('0') << static_cast<int>(c);
        } else {
          escaped << c;
        }
    }
  }
  return escaped.str();
}

inline auto make_routes() {
  http_api api;

  // Discover leader at server startup (best-effort)
  std::string leader_host = "100.117.80.126";  // Fallback to node1 via Tailscale
  uint16_t leader_port = 7001;
  discover_leader_from_cluster(leader_host, leader_port);

  auto db_client = std::make_shared<DbClient>(leader_host, leader_port);

  // GET /api/keys/{{key}}?nodeId=N - Retrieve a single key
  // Optional nodeId parameter allows reading from specific node (follower reads supported)
  api.get("/api/get/{{key}}") = [db_client](http_request& req, http_response& res) {
    try {
      auto params = req.url_parameters(s::key = std::string());
      std::string key = params.key;
      auto query = req.get_parameters(s::nodeId = std::optional<int>());

      if (key.empty()) {
        res.set_status(400);
        res.write_json(s::error = "Key parameter is required");
        return;
      }

      // Determine target node
      int nodeId = query.nodeId.value_or(0);  // 0 = auto-discover leader
      std::string target_host;
      uint16_t target_port;

      if (!get_target_node(nodeId, target_host, target_port, false)) {
        res.set_status(400);
        res.write_json(s::error = "Invalid nodeId or failed to discover leader");
        return;
      }

      // Use per-request client if nodeId specified, otherwise use persistent connection
      DbResponse result;
      if (nodeId > 0) {
        auto temp_client = std::make_shared<DbClient>(target_host, target_port);
        result = temp_client->get(key);
      } else {
        result = db_client->get(key);
      }

      if (result.success) {
        res.write_json(s::key = key, s::value = result.value);
      } else if (result.error == "Key not found") {
        res.set_status(404);
        res.write_json(s::error = "Key not found: " + key);
      } else {
        res.set_status(500);
        res.write_json(s::error = "Database error: " + result.error);
      }
    } catch (const std::exception& e) {
      res.set_status(500);
      res.write_json(s::error = std::string("Internal error: ") + e.what());
    }
  };

  // POST /api/keys/{{key}}?nodeId=N with JSON body {"value": "..."}
  // Optional nodeId parameter - if specified, must be the leader (write operations leader-only)
  api.post("/api/set/{{key}}") = [db_client](http_request& req, http_response& res) {
    try {
      auto params = req.url_parameters(s::key = std::string());
      std::string key = params.key;
      auto query = req.get_parameters(s::nodeId = std::optional<int>());

      if (key.empty()) {
        res.set_status(400);
        res.write_json(s::error = "Key parameter is required");
        return;
      }

      // Parse JSON body
      auto body = req.post_parameters(s::value = std::string());

      if (body.value.empty()) {
        res.set_status(400);
        res.write_json(s::error = "Value is required in request body");
        return;
      }

      // Determine target node - SET must go to leader
      int nodeId = query.nodeId.value_or(0);  // 0 = auto-discover leader
      std::string target_host;
      uint16_t target_port;

      if (!get_target_node(nodeId, target_host, target_port, true)) {
        res.set_status(400);
        if (nodeId > 0) {
          res.write_json(s::error = "SET operations must target the leader. Node " + std::to_string(nodeId) + " is not the leader.");
        } else {
          res.write_json(s::error = "Failed to discover leader");
        }
        return;
      }

      // Use per-request client if nodeId specified, otherwise use persistent connection
      DbResponse result;
      if (nodeId > 0) {
        auto temp_client = std::make_shared<DbClient>(target_host, target_port);
        result = temp_client->set(key, body.value);
      } else {
        result = db_client->set(key, body.value);
      }

      if (result.success) {
        res.set_status(201);  // Created
        res.write_json(s::key = key, s::value = body.value, s::status = "created");
      } else {
        res.set_status(500);
        res.write_json(s::error = "Database error: " + result.error);
      }
    } catch (const std::exception& e) {
      res.set_status(500);
      res.write_json(s::error = std::string("Internal error: ") + e.what());
    }
  };

  // DELETE /api/keys/{{key}}?nodeId=N
  // Optional nodeId parameter - if specified, must be the leader (write operations leader-only)
  api.post("/api/del/{{key}}") = [db_client](http_request& req, http_response& res) {
    try {
      auto params = req.url_parameters(s::key = std::string());
      std::string key = params.key;
      auto query = req.get_parameters(s::nodeId = std::optional<int>());

      if (key.empty()) {
        res.set_status(400);
        res.write_json(s::error = "Key parameter is required");
        return;
      }

      // Determine target node - DEL must go to leader
      int nodeId = query.nodeId.value_or(0);  // 0 = auto-discover leader
      std::string target_host;
      uint16_t target_port;

      if (!get_target_node(nodeId, target_host, target_port, true)) {
        res.set_status(400);
        if (nodeId > 0) {
          res.write_json(s::error = "DELETE operations must target the leader. Node " + std::to_string(nodeId) + " is not the leader.");
        } else {
          res.write_json(s::error = "Failed to discover leader");
        }
        return;
      }

      // Use per-request client if nodeId specified, otherwise use persistent connection
      DbResponse result;
      if (nodeId > 0) {
        auto temp_client = std::make_shared<DbClient>(target_host, target_port);
        result = temp_client->del(key);
      } else {
        result = db_client->del(key);
      }

      if (result.success) {
        res.write_json(s::key = key, s::status = "deleted");
      } else if (result.error == "Key not found") {
        res.set_status(404);
        res.write_json(s::error = "Key not found: " + key);
      } else {
        res.set_status(500);
        res.write_json(s::error = "Database error: " + result.error);
      }
    } catch (const std::exception& e) {
      res.set_status(500);
      res.write_json(s::error = std::string("Internal error: ") + e.what());
    }
  };

  // GET /api/range/forward/{{key}}?count=10
  api.get("/api/getff/{{key}}") = [db_client](http_request& req, http_response& res) {
    try {
      auto params = req.url_parameters(s::key = std::string());
      std::string key = params.key;
      auto query = req.get_parameters(s::count = std::optional<int>(), s::nodeId = std::optional<int>());

      if (key.empty()) {
        res.set_status(400);
        res.write_json(s::error = "Key parameter is required");
        return;
      }

      uint32_t count = query.count.value_or(10);
      int nodeId = query.nodeId.value_or(0);

      std::string targetHost;
      uint16_t targetPort;

      if (!get_target_node(nodeId, targetHost, targetPort, false)) {
        res.set_status(500);
        res.write_json(s::error = "Failed to discover database leader");
        return;
      }

      DbResponse result;
      if (nodeId > 0) {
        auto tempDBClient = std::make_shared<DbClient>(targetHost, targetPort);
        result = tempDBClient->getff(key, count);
      } else {
        auto result = db_client->getff(key, count);
      }

      if (result.success) {
        // Build JSON array manually since Lithium's write_json doesn't support complex structures easily
        std::ostringstream json;
        json << "{\"results\":[";

        for (size_t i = 0; i < result.results.size(); i++) {
          if (i > 0) json << ",";
          json << "{\"key\":\"" << json_escape(result.results[i].first) << "\","
               << "\"value\":\"" << json_escape(result.results[i].second) << "\"}";
        }

        json << "],\"count\":" << result.results.size() << "}";

        res.set_header("Content-Type", "application/json");
        res.write(json.str());
      } else {
        res.set_status(500);
        res.write_json(s::error = "Database error: " + result.error);
      }
    } catch (const std::exception& e) {
      res.set_status(500);
      res.write_json(s::error = std::string("Internal error: ") + e.what());
    }
  };

  // GET /api/range/backward/{{key}}?count=10
  api.get("/api/getfb/{{key}}") = [db_client](http_request& req, http_response& res) {
    try {
      auto params = req.url_parameters(s::key = std::string());
      std::string key = params.key;
      auto query = req.get_parameters(s::count = std::optional<int>(), s::nodeId = std::optional<int>());

      if (key.empty()) {
        res.set_status(400);
        res.write_json(s::error = "Key parameter is required");
        return;
      }

      uint32_t count = query.count.value_or(10);
      int nodeId = query.nodeId.value_or(0);

      std::string targetHost;
      uint16_t targetPort;

      if (!get_target_node(nodeId, targetHost, targetPort, false)) {
        res.set_status(500);
        res.write_json(s::error = "Failed to discover database leader");
        return;
      }

      DbResponse result;
      if (nodeId > 0) {
        auto tempDBClient = std::make_shared<DbClient>(targetHost, targetPort);
        result = tempDBClient->getfb(key, count);
      } else {
        auto result = db_client->getfb(key, count);
      }

      if (result.success) {
        // Build JSON array manually
        std::ostringstream json;
        json << "{\"results\":[";

        for (size_t i = 0; i < result.results.size(); i++) {
          if (i > 0) json << ",";
          json << "{\"key\":\"" << json_escape(result.results[i].first) << "\","
               << "\"value\":\"" << json_escape(result.results[i].second) << "\"}";
        }

        json << "],\"count\":" << result.results.size() << "}";

        res.set_header("Content-Type", "application/json");
        res.write(json.str());
      } else {
        res.set_status(500);
        res.write_json(s::error = "Database error: " + result.error);
      }
    } catch (const std::exception& e) {
      res.set_status(500);
      res.write_json(s::error = std::string("Internal error: ") + e.what());
    }
  };

  // POST /api/optimize - Rebuild database, removing deleted entries
  api.post("/api/optimize") = [db_client](http_request& req, http_response& res) {
    try {
      auto query = req.get_parameters(s::nodeId = std::optional<int>());
      int nodeId = query.nodeId.value_or(0);

      std::string targetHost;
      uint16_t targetPort;

      if (!get_target_node(nodeId, targetHost, targetPort, false)) {
        res.set_status(500);
        res.write_json(s::error = "Failed to discover database leader");
        return;
      }

      DbResponse result;
      if (nodeId > 0) {
        auto tempDBClient = std::make_shared<DbClient>(targetHost, targetPort);
        result = tempDBClient->optimize();
      } else {
        auto result = db_client->optimize();
      }

      if (result.success) {
        res.write_json(s::status = "optimized");
      } else {
        res.set_status(500);
        res.write_json(s::error = "Optimize failed: " + result.error);
      }
    } catch (const std::exception& e) {
      res.set_status(500);
      res.write_json(s::error = std::string("Internal error: ") + e.what());
    }
  };

  // Test simple parameterized route
  api.get("/test/{{name}}") = [](http_request& req, http_response& res) {
    auto params = req.url_parameters(s::name = std::string());
    res.write("Hello " + params.name);
  };

  // GET /api/keys/prefix/{{prefix}} - Get keys with prefix
  api.get("/api/keys/prefix/{{prefix}}") = [db_client](http_request& req, http_response& res) {
    try {
      auto params = req.url_parameters(s::prefix = std::string());
      std::string prefix = params.prefix;
      auto query = req.get_parameters(s::nodeId = std::optional<int>());
      int nodeId = query.nodeId.value_or(0);

      std::string targetHost;
      uint16_t targetPort;

      if (!get_target_node(nodeId, targetHost, targetPort, false)) {
        res.set_status(500);
        res.write_json(s::error = "Failed to discover database leader");
        return;
      }

      DbResponse response;
      if (nodeId > 0) {
        auto tempDBClient = std::make_shared<DbClient>(targetHost, targetPort);
        response = tempDBClient->getKeysPrefix(prefix);
      } else {
        auto response = db_client->getKeysPrefix(prefix);
      }


      if (!response.success) {
        res.set_status(500);
        res.write_json(s::error = response.error);
        return;
      }

      // Build JSON array of keys
      std::ostringstream json;
      json << "{\"keys\":[";
      for (size_t i = 0; i < response.keys.size(); i++) {
        if (i > 0) json << ",";
        json << "\"" << json_escape(response.keys[i]) << "\"";
      }
      json << "]}";

      res.write(json.str());
    } catch (const std::exception& e) {
      res.set_status(500);
      res.write_json(s::error = e.what());
    }
  };

  // GET /api/keys/paging - Get keys with pagination
  api.get("/api/keys/paging") = [db_client](http_request& req, http_response& res) {
    try {
      auto query = req.get_parameters(s::pageSize = int(), s::pageNum = int(), s::nodeId = int());

      int pageSize = query.pageSize;
      int pageNum = query.pageNum;

      if (pageSize <= 0 || pageNum <= 0) {
        res.set_status(400);
        res.write_json(s::error = "pageSize and pageNum must be positive integers");
        return;
      }

      int nodeId = query.nodeId;

      std::string targetHost;
      uint16_t targetPort;

      if (!get_target_node(nodeId, targetHost, targetPort, false)) {
        res.set_status(500);
        res.write_json(s::error = "Failed to discover database leader");
        return;
      }

      DbResponse response;
      if (nodeId > 0) {
        auto tempDBClient = std::make_shared<DbClient>(targetHost, targetPort);
        response = tempDBClient->getKeysPaging(pageSize, pageNum);
      } else {
        auto response = db_client->getKeysPaging(pageSize, pageNum);
      }

      if (!response.success) {
        res.set_status(500);
        res.write_json(s::error = response.error);
        return;
      }

      // Build JSON response with keys array and totalCount
      std::ostringstream json;
      json << "{\"keys\":[";
      for (size_t i = 0; i < response.keys.size(); i++) {
        if (i > 0) json << ",";
        json << "\"" << json_escape(response.keys[i]) << "\"";
      }
      json << "],\"totalCount\":" << response.totalCount << "}";

      res.write(json.str());
    } catch (const std::exception& e) {
      res.set_status(500);
      res.write_json(s::error = e.what());
    }
  };

  // GET /api/cluster/nodes - List all cluster nodes with metadata
  // Used by Vue.js UI to populate node selection dropdown
  api.get("/api/cluster/nodes") = [](http_request& req, http_response& res) {
    try {
      // Discover leader to determine port assignments
      std::string leader_host;
      uint16_t leader_port;
      bool leader_discovered = discover_leader_from_cluster(leader_host, leader_port);

      std::ostringstream json;
      json << "{\"nodes\":[";

      for (size_t i = 0; i < sizeof(CLUSTER)/sizeof(NodeInfo); i++) {
        const auto& node = CLUSTER[i];
        if (i > 0) json << ",";

        // Determine client port: leader uses 7001, followers use 7101-7104
        uint16_t clientPort = CLIENT_PORT;
        if (leader_discovered && node.host != leader_host) {
          clientPort = FOLLOWER_READ_PORT(node.id);  // Follower read-only port
        }

        json << "{"
             << "\"id\":" << node.id << ","
             << "\"name\":\"Node " << node.id << " (" << node.host << ")\","
             << "\"host\":\"" << node.host << "\","
             << "\"clientPort\":" << clientPort << ","
             << "\"controlPort\":" << node.port
             << "}";
      }

      json << "]}";

      res.set_header("Content-Type", "application/json");
      res.write(json.str());
    } catch (const std::exception& e) {
      res.set_status(500);
      res.write_json(s::error = std::string("Failed to get cluster nodes: ") + e.what());
    }
  };

  // GET /api/cluster/status - Comprehensive cluster health status
  // This mirrors the CLI client's 'status' command functionality
  api.get("/api/cluster/status") = [](http_request& req, http_response& res) {
    try {
      // Cache to reduce control plane load
      static std::mutex cacheMutex;
      static std::string cachedResponse;
      static uint64_t cacheTimestamp = 0;
      static constexpr uint64_t CACHE_TTL_MS = 2000;  // 2 second cache

      uint64_t now = get_timestamp_ms();

      // Check cache
      {
        std::lock_guard<std::mutex> lock(cacheMutex);
        if ((now - cacheTimestamp) < CACHE_TTL_MS && !cachedResponse.empty()) {
          res.set_header("Content-Type", "application/json");
          res.set_header("X-Cache", "HIT");
          res.write(cachedResponse);
          return;
        }
      }

      // Query each node in cluster
      std::ostringstream json;
      json << "{";
      json << "\"timestamp\":" << now << ",";
      json << "\"nodes\":[";

      int leaderCount = 0;
      int leaderId = 0;
      std::string leaderHost;

      for (size_t i = 0; i < sizeof(CLUSTER)/sizeof(NodeInfo); i++) {
        const auto& node = CLUSTER[i];

        if (i > 0) json << ",";
        json << "{";
        json << "\"id\":" << node.id << ",";
        json << "\"host\":\"" << node.host << "\",";
        json << "\"port\":" << node.port << ",";

        // Query control plane - use tunnel if available
        std::string controlKey = std::string(node.host) + ":" + std::to_string(node.port);
        std::string connectHost = node.host;
        uint16_t connectPort = node.port;

        auto it = CONTROL_PLANE_TUNNEL_MAP.find(controlKey);
        if (it != CONTROL_PLANE_TUNNEL_MAP.end()) {
          // Use local tunnel endpoint
          parse_host_port(it->second, connectHost, connectPort);
        }

        sock_t s = tcp_connect(connectHost, connectPort);
        if (s == NET_INVALID) {
          json << "\"status\":\"OFFLINE\",\"role\":\"UNREACHABLE\"";
          json << "}";
          continue;
        }

        set_socket_timeouts(s, 1000);  // 1 second timeout
        send_all(s, "CLUSTER_STATUS\n");

        std::string line;
        std::string role = "UNKNOWN";
        uint64_t term = 0;
        int nodeLeaderId = 0;
        uint64_t lsn = 0;
        long long lastHBAge = -1;
        std::vector<std::tuple<int, std::string, uint64_t>> followers;

        while (recv_line(s, line)) {
          if (line == "END") break;

          auto tokens = split(trim(line), ' ');
          if (tokens.empty()) continue;

          // Format: STATUS <nodeId> <role> <term> <leaderId> <myLSN> <lastHBAge>
          if (tokens[0] == "STATUS" && tokens.size() >= 7) {
            role = tokens[2];
            term = std::stoull(tokens[3]);
            nodeLeaderId = std::stoi(tokens[4]);
            lsn = std::stoull(tokens[5]);
            lastHBAge = std::stoll(tokens[6]);
            if (role == "LEADER") {
              leaderCount++;
              leaderId = node.id;
              leaderHost = node.host;
            }
          } else if (tokens[0] == "FOLLOWER_STATUS" && tokens.size() >= 4) {
            int fid = std::stoi(tokens[1]);
            std::string fstatus = tokens[2];
            uint64_t flsn = std::stoull(tokens[3]);
            followers.push_back({fid, fstatus, flsn});
          }
        }
        net_close(s);

        json << "\"status\":\"ONLINE\",";
        json << "\"role\":\"" << role << "\",";
        json << "\"term\":" << term << ",";
        json << "\"leaderId\":" << nodeLeaderId << ",";
        json << "\"lsn\":" << lsn << ",";
        json << "\"lastHbAge\":" << lastHBAge;

        // Include followers if this node is the leader
        if (!followers.empty()) {
          json << ",\"followers\":[";
          for (size_t fi = 0; fi < followers.size(); fi++) {
            if (fi > 0) json << ",";
            json << "{\"id\":" << std::get<0>(followers[fi])
                 << ",\"status\":\"" << std::get<1>(followers[fi]) << "\""
                 << ",\"lsn\":" << std::get<2>(followers[fi]) << "}";
          }
          json << "]";
        }

        json << "}";
      }

      json << "],";
      json << "\"leader\":{";
      if (leaderId > 0) {
        json << "\"id\":" << leaderId << ",\"host\":\"" << leaderHost << "\",\"available\":true";
      } else {
        json << "\"id\":0,\"host\":\"\",\"available\":false";
      }
      json << "},";

      // Split brain detection
      json << "\"splitBrain\":" << (leaderCount > 1 ? "true" : "false");
      json << "}";

      std::string response = json.str();

      // Update cache
      {
        std::lock_guard<std::mutex> lock(cacheMutex);
        cachedResponse = response;
        cacheTimestamp = now;
      }

      res.set_header("Content-Type", "application/json");
      res.set_header("X-Cache", "MISS");
      res.write(response);
    } catch (const std::exception& e) {
      res.set_status(500);
      res.write_json(s::error = std::string("Cluster status error: ") + e.what());
    }
  };

  // Static file serving for Vue.js SPA (MUST BE LAST - catch-all route)
  api.get("/{{path...}}") = [](http_request& req, http_response& res) {
    auto url_params = req.url_parameters(s::path = std::string_view());
    std::string path(url_params.path);

    // Security: prevent directory traversal
    if (path.find("..") != std::string::npos) {
      res.set_status(403);
      res.write("Forbidden");
      return;
    }

    // Map URL to file path
    std::string file_path = "public/" + path;

    // Default to index.html for SPA routing (empty path)
    if (path.empty()) {
      file_path = "public/index.html";
    }

    // Attempt to serve file
    std::ifstream file(file_path, std::ios::binary);
    if (!file.is_open()) {
      // Fallback to index.html for Vue Router (404 becomes SPA route)
      file.open("public/index.html", std::ios::binary);
      if (!file.is_open()) {
        res.set_status(404);
        res.write("Not Found");
        return;
      }
    }

    // Determine MIME type based on file extension
    std::string mime_type = "text/html";
    if (ends_with(path, ".js")) mime_type = "application/javascript";
    else if (ends_with(path, ".css")) mime_type = "text/css";
    else if (ends_with(path, ".json")) mime_type = "application/json";
    else if (ends_with(path, ".png")) mime_type = "image/png";
    else if (ends_with(path, ".jpg") || ends_with(path, ".jpeg")) mime_type = "image/jpeg";
    else if (ends_with(path, ".svg")) mime_type = "image/svg+xml";
    else if (ends_with(path, ".ico")) mime_type = "image/x-icon";

    // Read and serve file
    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());

    res.set_header("Content-Type", mime_type);
    res.write(content);
  };

  return api;
}