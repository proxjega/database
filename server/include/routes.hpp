#pragma once
#include <lithium_http_server.hh>
#include "symbols.hh"
#include "db_client.hpp"
#include <string>
#include <optional>
#include <memory>
#include <sstream>

using namespace li;

inline auto make_routes() {
  http_api api;

  // Initialize database client (pointing to local leader)
  // In production, this should be configurable via environment variables
  auto db_client = std::make_shared<DbClient>("127.0.0.1", 7001);

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
          json << "{\"key\":\"" << result.results[i].first << "\","
               << "\"value\":\"" << result.results[i].second << "\"}";
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
          json << "{\"key\":\"" << result.results[i].first << "\","
               << "\"value\":\"" << result.results[i].second << "\"}";
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

  // Health check endpoint
  api.get("/health") = [db_client](http_request& req, http_response& res) {
    res.write_json(
      s::status = "ok",
      s::leader_host = db_client->get_leader_host(),
      s::leader_port = (int)db_client->get_leader_port()
    );
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

  return api;
}
