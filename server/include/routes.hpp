#pragma once
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
inline bool discover_leader_from_cluster(std::string& out_host, uint16_t& out_port) {
  // const std::vector<std::string> CLUSTER_NODES = {
  //   "100.117.80.126",  // node1
  //   "100.70.98.49",    // node2
  //   "100.118.80.33",   // node3
  //   "100.116.151.88"   // node4
  // };

  const std::vector<std::string> CLUSTER_NODES = {
    "127.0.0.1", "127.0.0.2", "127.0.0.3", "127.0.0.4"
  };

  // Tier 1: REDIRECT probe on follower read ports
  for (int node = 1; node <= 4; node++) {
    uint16_t follower_port = 7100 + node;
    sock_t s = tcp_connect(CLUSTER_NODES[node-1], follower_port);

    if (s != NET_INVALID) {
      // Set timeout on follower socket too
      set_socket_timeouts(s, 500);

      send_all(s, "SET __probe__ 1\n");  // Non-GET triggers REDIRECT

      std::string response;
      if (recv_line(s, response)) {
        auto tokens = split(trim(response), ' ');
        if (tokens.size() >= 3 && tokens[0] == "REDIRECT") {
          std::string redirect_host = tokens[1];
          uint16_t redirect_port = std::stoi(tokens[2]);
          net_close(s);

          // Verify the REDIRECT target is actually alive (with short timeout)
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
                out_host = redirect_host;
                out_port = redirect_port;
                return true;
              }
            }
            net_close(verify_s);
          }
          // REDIRECT target is dead, continue to next follower
        }
      }
      net_close(s);
    }
  }

  // Tier 2: Direct leader verification
  // Check if port 7001 is not only open, but actually responds as a leader
  for (const auto& host : CLUSTER_NODES) {
    sock_t s = tcp_connect(host, 7001);
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
          out_host = host;
          out_port = 7001;
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
  // std::string leader_host = "100.117.80.126";  // Fallback to node1
  std::string leader_host = "127.0.0.1";  // Fallback to node1

  uint16_t leader_port = 7001;
  discover_leader_from_cluster(leader_host, leader_port);

  auto db_client = std::make_shared<DbClient>(leader_host, leader_port);

  // GET /api/keys/{{key}} - Retrieve a single key
  api.get("/api/get/{{key}}") = [db_client](http_request& req, http_response& res) {
    try {
      auto params = req.url_parameters(s::key = std::string());
      std::string key = params.key;

      if (key.empty()) {
        res.set_status(400);
        res.write_json(s::error = "Key parameter is required");
        return;
      }

      auto result = db_client->get(key);

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

  // POST /api/keys/{{key}} with JSON body {"value": "..."}
  api.post("/api/set/{{key}}") = [db_client](http_request& req, http_response& res) {
    try {
      auto params = req.url_parameters(s::key = std::string());
      std::string key = params.key;

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

      auto result = db_client->set(key, body.value);

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

  // DELETE /api/keys/{{key}}
  api.post("/api/del/{{key}}") = [db_client](http_request& req, http_response& res) {
    try {
      auto params = req.url_parameters(s::key = std::string());
      std::string key = params.key;

      if (key.empty()) {
        res.set_status(400);
        res.write_json(s::error = "Key parameter is required");
        return;
      }

      auto result = db_client->del(key);

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
      auto query = req.get_parameters(s::count = std::optional<int>());

      if (key.empty()) {
        res.set_status(400);
        res.write_json(s::error = "Key parameter is required");
        return;
      }

      uint32_t count = query.count.value_or(10);

      auto result = db_client->getff(key, count);

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
      auto query = req.get_parameters(s::count = std::optional<int>());

      if (key.empty()) {
        res.set_status(400);
        res.write_json(s::error = "Key parameter is required");
        return;
      }

      uint32_t count = query.count.value_or(10);

      auto result = db_client->getfb(key, count);

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
      auto result = db_client->optimize();

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

      DbResponse response = db_client->getKeysPrefix(prefix);

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
      auto query = req.get_parameters(s::pageSize = int(), s::pageNum = int());

      if (query.pageSize <= 0 || query.pageNum <= 0) {
        res.set_status(400);
        res.write_json(s::error = "pageSize and pageNum must be positive integers");
        return;
      }

      DbResponse response = db_client->getKeysPaging(query.pageSize, query.pageNum);

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

        // Query control plane
        sock_t s = tcp_connect(node.host, node.port);
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