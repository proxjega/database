#pragma once
#include <cstdint>
#include <string>
#include <vector>

using std::string;
using std::vector;

/**
 * DbResponse - Response structure from database operations
 * Used by HTTP handlers to determine status codes and response bodies
 */
struct DbResponse {
    bool success;
    string value;  // For single GET operations
    string error;  // Error message if success=false
    vector<std::pair<string, string>> results;  // For range queries
    vector<string> keys;  // For key-only queries (prefix, paging)
    uint32_t totalCount;  // For paging queries
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
    string leader_host;
    uint16_t leader_port;

    /**
     * Helper: Send request and get single-line response
     * Handles connection errors and basic error responses
     */
    DbResponse send_simple_request(const string& command);

public:
    /**
     * Constructor
     * @param initial_host - Leader host (or any cluster node for auto-detection)
     * @param port - Leader client port (default 7001)
     */
    DbClient(const string& initial_host, uint16_t port);

    /**
     * GET operation
     * @param key - Key to retrieve
     * @return DbResponse with value or error
     */
    DbResponse get(const string& key);

    /**
     * SET operation
     * @param key - Key to set
     * @param value - Value to set
     * @return DbResponse indicating success or error
     */
    DbResponse set(const string& key, const string& value);

    /**
     * DELETE operation
     * @param key - Key to delete
     * @return DbResponse indicating success or error
     */
    DbResponse del(const string& key);

    /**
     * GETFF - Forward range query
     * @param key - Starting key
     * @param count - Number of keys to retrieve
     * @return DbResponse with results vector
     */
    DbResponse getff(const string& key, uint32_t count);

    /**
     * GETFB - Backward range query
     * @param key - Ending key
     * @param count - Number of keys to retrieve
     * @return DbResponse with results vector
     */
    DbResponse getfb(const string& key, uint32_t count);

    /**
     * OPTIMIZE - Rebuild database, removing deleted entries
     * @return DbResponse indicating success or error
     */
    DbResponse optimize();

    /**
     * Get keys with prefix
     * @param prefix - Prefix to search for (empty string for all keys)
     * @return DbResponse with keys vector
     */
    DbResponse getKeysPrefix(const string& prefix);

    /**
     * Get keys with paging
     * @param pageSize - Number of keys per page
     * @param pageNum - Page number (1-indexed)
     * @return DbResponse with keys vector and totalCount
     */
    DbResponse getKeysPaging(uint32_t pageSize, uint32_t pageNum);

    /**
     * Get current leader host
     * @return Leader host string
     */
    string get_leader_host() const { return leader_host; }

    /**
     * Get current leader port
     * @return Leader port
     */
    uint16_t get_leader_port() const { return leader_port; }
};
