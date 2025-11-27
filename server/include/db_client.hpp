#pragma once
#include "../../Replication/common.hpp"
#include <string>
#include <optional>
#include <vector>

/**
 * DbResponse - Response structure from database operations
 * Used by HTTP handlers to determine status codes and response bodies
 */
struct DbResponse {
    bool success;
    std::string value;  // For single GET operations
    std::string error;  // Error message if success=false
    std::vector<std::pair<std::string, std::string>> results;  // For range queries
};

/**
 * DbClient - Lightweight TCP wrapper for HTTP server
 *
 * This class provides a simple interface for the HTTP server to communicate
 * with the replication cluster leader. It reuses TCP functions from common.hpp.
 *
 * NOT a CLI tool - returns structured data instead of printing to stdout.
 */
class DbClient {
private:
    std::string leader_host;
    uint16_t leader_port;

    /**
     * Helper: Send request and get single-line response
     * Handles connection errors and basic error responses
     */
    DbResponse send_simple_request(const std::string& command);

public:
    /**
     * Constructor
     * @param initial_host - Leader host (or any cluster node for auto-detection)
     * @param port - Leader client port (default 7001)
     */
    DbClient(const std::string& initial_host, uint16_t port);

    /**
     * GET operation
     * @param key - Key to retrieve
     * @return DbResponse with value or error
     */
    DbResponse get(const std::string& key);

    /**
     * SET operation
     * @param key - Key to set
     * @param value - Value to set
     * @return DbResponse indicating success or error
     */
    DbResponse set(const std::string& key, const std::string& value);

    /**
     * DELETE operation
     * @param key - Key to delete
     * @return DbResponse indicating success or error
     */
    DbResponse del(const std::string& key);

    /**
     * GETFF - Forward range query
     * @param key - Starting key
     * @param count - Number of keys to retrieve
     * @return DbResponse with results vector
     */
    DbResponse getff(const std::string& key, uint32_t count);

    /**
     * GETFB - Backward range query
     * @param key - Ending key
     * @param count - Number of keys to retrieve
     * @return DbResponse with results vector
     */
    DbResponse getfb(const std::string& key, uint32_t count);

    /**
     * OPTIMIZE - Rebuild database, removing deleted entries
     * @return DbResponse indicating success or error
     */
    DbResponse optimize();

    /**
     * Get current leader host
     * @return Leader host string
     */
    std::string get_leader_host() const { return leader_host; }

    /**
     * Get current leader port
     * @return Leader port
     */
    uint16_t get_leader_port() const { return leader_port; }
};
