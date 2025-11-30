#pragma once
#include <lithium_http_server.hh>
#include "symbols.hh"
#include "db_client.hpp"
#include "../../Replication/common.hpp"
#include <string>
#include <optional>
#include <memory>
#include <sstream>
#include <vector>
#include <fstream>
#include <iomanip>

using namespace li;

// Helper function to discover current leader from cluster
// Uses same logic as ./client leader command
inline bool discover_leader_from_cluster(std::string& out_host, uint16_t& out_port) {
  const std::vector<std::string> CLUSTER_NODES = {
    "127.0.0.1", "127.0.0.1", "127.0.0.1", "127.0.0.1"
  };

  // Tier 1: REDIRECT probe on follower read ports
  for (int node = 1; node <= 4; node++) {
    uint16_t follower_port = 7100 + node;
    sock_t s = tcp_connect(CLUSTER_NODES[node-1], follower_port);

    if (s != NET_INVALID) {
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
            // Set aggressive 500ms timeout to quickly detect dead leaders
            set_socket_timeouts(verify_s, 500);

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
  std::string leader_host = "127.0.0.1";  // Fallback
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

  // Health check endpoint - returns current leader connection info
  api.get("/api/health") = [db_client](http_request& req, http_response& res) {
    try {
      // Discover actual current leader (not static config)
      std::string leader_host;
      uint16_t leader_port = 7001;

      if (discover_leader_from_cluster(leader_host, leader_port)) {
        // Leader is active
        std::ostringstream json;
        json << "{"
             << "\"status\":\"ok\","
             << "\"leader_host\":\"" << leader_host << "\","
             << "\"leader_port\":" << leader_port
             << "}";

        res.set_header("Content-Type", "application/json");
        res.write(json.str());
      } else {
        // No leader available (election in progress or all nodes down)
        res.set_status(503);
        res.write_json(
          s::status = "unavailable",
          s::error = "No leader available - cluster may be in election"
        );
      }
    } catch (const std::exception& e) {
      res.set_status(500);
      res.write_json(
        s::status = "error",
        s::error = std::string("Health check failed: ") + e.what()
      );
    }
  };

  // GET /api/leader - Discover current cluster leader
  api.get("/api/leader") = [](http_request& req, http_response& res) {
    try {
      std::string leader_host;
      uint16_t leader_port = 7001;

      // Try REDIRECT probe method (same as client.cpp)
      if (discover_leader_from_cluster(leader_host, leader_port)) {
        // Build JSON manually for complex structure
        std::ostringstream json;
        json << "{"
             << "\"host\":\"" << leader_host << "\","
             << "\"port\":" << leader_port << ","
             << "\"status\":\"active\","
             << "\"timestamp\":" << get_timestamp_ms()
             << "}";

        res.set_header("Content-Type", "application/json");
        res.write(json.str());
      } else {
        res.set_status(503);
        res.write_json(
          s::error = "Leader not available",
          s::status = "unavailable"
        );
      }
    } catch (const std::exception& e) {
      res.set_status(500);
      res.write_json(s::error = std::string("Discovery error: ") + e.what());
    }
  };

  // Test simple parameterized route
  api.get("/test/{{name}}") = [](http_request& req, http_response& res) {
    auto params = req.url_parameters(s::name = std::string());
    res.write("Hello " + params.name);
  };

  // Test multi-level parameterized route
  api.get("/api/test/{{key}}") = [](http_request& req, http_response& res) {
    auto params = req.url_parameters(s::key = std::string());
    res.write("API Test Key: " + params.key);
  };

  // Simple test of /api/keys path without db_client
  api.get("/api/keys-test/{{key}}") = [](http_request& req, http_response& res) {
    auto params = req.url_parameters(s::key = std::string());
    res.write("Keys Test: " + params.key);
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
    if (path.ends_with(".js")) mime_type = "application/javascript";
    else if (path.ends_with(".css")) mime_type = "text/css";
    else if (path.ends_with(".json")) mime_type = "application/json";
    else if (path.ends_with(".png")) mime_type = "image/png";
    else if (path.ends_with(".jpg") || path.ends_with(".jpeg")) mime_type = "image/jpeg";
    else if (path.ends_with(".svg")) mime_type = "image/svg+xml";
    else if (path.ends_with(".ico")) mime_type = "image/x-icon";

    // Read and serve file
    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());

    res.set_header("Content-Type", mime_type);
    res.write(content);
  };

  return api;
}
