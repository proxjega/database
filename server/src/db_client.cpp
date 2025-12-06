#include "../include/db_client.hpp"
#include "../../Replication/include/common.hpp"

using std::string;

DbClient::DbClient(const string& initial_host, uint16_t port)
    : leader_host(initial_host), leader_port(port) {}

DbResponse DbClient::send_simple_request(const string& command) {
    DbResponse response;
    response.success = false;

    sock_t sock = tcp_connect(leader_host, leader_port);
    if (sock == NET_INVALID) {
        response.error = "Failed to connect to database leader";
        return response;
    }

    if (!send_all(sock, command + "\n")) {
        net_close(sock);
        response.error = "Failed to send request to database";
        return response;
    }

    string line;
    if (!recv_line(sock, line)) {
        net_close(sock);
        response.error = "No response from database";
        return response;
    }

    // Parse response
    line = trim(line);
    auto tokens = split(line, ' ');

    if (tokens.empty()) {
        net_close(sock);
        response.error = "Empty response from database";
        return response;
    }

    // Handle different response types
    if (tokens[0] == "VALUE" && tokens.size() >= 3) {
        // Parse length-prefixed value: "VALUE <value_len> <value>"
        if (!parse_length_prefixed_value(tokens, 1, sock, response.value)) {
            net_close(sock);
            response.error = "Failed to parse value";
            return response;
        }
        response.success = true;
    } else if (tokens[0] == "OK") {
        response.success = true;
    } else if (line == "NOT_FOUND") {
        response.error = "Key not found";
    } else if (tokens[0] == "ERR") {
        // Combine all error message tokens
        string err_msg;
        for (size_t i = 1; i < tokens.size(); i++) {
            if (i > 1) {
                err_msg += " ";
            }
            err_msg += tokens[i];
        }
        response.error = err_msg.empty() ? "Database error" : err_msg;
    } else {
        // Unknown response format
        response.error = "Unexpected response: " + line;
    }

    net_close(sock);
    return response;
}

DbResponse DbClient::get(const string& key) {
    return send_simple_request("GET " + key);
}

DbResponse DbClient::set(const string& key, const string& value) {
    string command = "SET " + key + " " + std::to_string(value.length()) + " " + value;
    return send_simple_request(command);
}

DbResponse DbClient::del(const string& key) {
    return send_simple_request("DEL " + key);
}

DbResponse DbClient::getff(const string& key, uint32_t count) {
    DbResponse response;
    response.success = false;

    sock_t sock = tcp_connect(leader_host, leader_port);
    if (sock == NET_INVALID) {
        response.error = "Failed to connect to database leader";
        return response;
    }

    string command = "GETFF " + key + " " + std::to_string(count);
    if (!send_all(sock, command + "\n")) {
        net_close(sock);
        response.error = "Failed to send request to database";
        return response;
    }

    // Read multiple KEY_VALUE lines until END
    string line;
    while (recv_line(sock, line)) {
        line = trim(line);

        if (line == "END") {
            response.success = true;
            break;
        }

        auto tokens = split(line, ' ');
        if (tokens.size() >= 3 && tokens[0] == "KEY_VALUE") {
            // KEY_VALUE <key> <value_len> <value...>
            string key = tokens[1];
            string value;

            // Parse value_len to know how many bytes to read
            size_t value_len = 0;
            try {
                value_len = std::stoull(tokens[2]);
            } catch (...) {
                response.error = "Invalid value length in KEY_VALUE";
                net_close(sock);
                return response;
            }

            // Reconstruct value from remaining tokens on this line
            string reconstructed;
            for (size_t i = 3; i < tokens.size(); i++) {
                if (i > 3) {
                    reconstructed += " ";
                }
                reconstructed += tokens[i];
            }

            // Check if we have the complete value already
            if (reconstructed.length() == value_len) {
                value = reconstructed;
            } else if (reconstructed.length() > value_len) {
                // Have more than expected, truncate
                value = reconstructed.substr(0, value_len);
            } else {
                // Need to read more bytes from socket
                // recv_line() stripped the \n, so account for it
                size_t bytes_needed = value_len - reconstructed.length();
                value = reconstructed;

                // Add back the newline that recv_line() stripped (if value continues)
                if (bytes_needed > 0 && reconstructed.length() > 0) {
                    value += "\n";
                    bytes_needed -= 1;
                }

                // Read remaining bytes directly from socket
                if (bytes_needed > 0) {
                    std::vector<char> buffer(bytes_needed);
                    size_t total_read = 0;

                    while (total_read < bytes_needed) {
                        ssize_t received = recv(sock, buffer.data() + total_read,
                                               bytes_needed - total_read, 0);
                        if (received <= 0) {
                            response.error = "Failed to read complete value";
                            net_close(sock);
                            return response;
                        }
                        total_read += received;
                    }
                    value.append(buffer.data(), bytes_needed);
                }

                // After reading the value bytes, we need to consume the trailing \n
                // that the leader added after the value
                char trailing_newline;
                recv(sock, &trailing_newline, 1, 0);
            }

            response.results.push_back({key, value});
        } else if (tokens[0] == "ERR") {
            // Error during range query
            string err_msg;
            for (size_t i = 1; i < tokens.size(); i++) {
                if (i > 1) {
                    err_msg += " ";
                }
                err_msg += tokens[i];
            }
            response.error = err_msg.empty() ? "Database error" : err_msg;
            net_close(sock);
            return response;
        }
    }

    net_close(sock);

    if (!response.success && response.error.empty()) {
        response.error = "Incomplete response from database";
    }

    return response;
}

DbResponse DbClient::getfb(const string& key, uint32_t count) {
    DbResponse response;
    response.success = false;

    sock_t sock = tcp_connect(leader_host, leader_port);
    if (sock == NET_INVALID) {
        response.error = "Failed to connect to database leader";
        return response;
    }

    string command = "GETFB " + key + " " + std::to_string(count);
    if (!send_all(sock, command + "\n")) {
        net_close(sock);
        response.error = "Failed to send request to database";
        return response;
    }

    // Read multiple KEY_VALUE lines until END
    string line;
    while (recv_line(sock, line)) {
        line = trim(line);

        if (line == "END") {
            response.success = true;
            break;
        }

        auto tokens = split(line, ' ');
        if (tokens.size() >= 3 && tokens[0] == "KEY_VALUE") {
            // KEY_VALUE <key> <value_len> <value...>
            // Note: value may contain spaces and/or newlines, so we need length-prefixed parsing
            string key = tokens[1];
            string value;

            // Parse value_len to know how many bytes to read
            size_t value_len = 0;
            try {
                value_len = std::stoull(tokens[2]);
            } catch (...) {
                response.error = "Invalid value length in KEY_VALUE";
                net_close(sock);
                return response;
            }

            // Reconstruct value from remaining tokens on this line
            string reconstructed;
            for (size_t i = 3; i < tokens.size(); i++) {
                if (i > 3) {
                    reconstructed += " ";
                }
                reconstructed += tokens[i];
            }

            // Check if we have the complete value already
            if (reconstructed.length() == value_len) {
                value = reconstructed;
            } else if (reconstructed.length() > value_len) {
                // Have more than expected, truncate
                value = reconstructed.substr(0, value_len);
            } else {
                // Need to read more bytes from socket
                // recv_line() stripped the \n, so account for it
                size_t bytes_needed = value_len - reconstructed.length();
                value = reconstructed;

                // Add back the newline that recv_line() stripped (if value continues)
                if (bytes_needed > 0 && reconstructed.length() > 0) {
                    value += "\n";
                    bytes_needed -= 1;
                }

                // Read remaining bytes directly from socket
                if (bytes_needed > 0) {
                    std::vector<char> buffer(bytes_needed);
                    size_t total_read = 0;

                    while (total_read < bytes_needed) {
                        ssize_t received = recv(sock, buffer.data() + total_read,
                                               bytes_needed - total_read, 0);
                        if (received <= 0) {
                            response.error = "Failed to read complete value";
                            net_close(sock);
                            return response;
                        }
                        total_read += received;
                    }
                    value.append(buffer.data(), bytes_needed);
                }

                // After reading the value bytes, we need to consume the trailing \n
                // that the leader added after the value
                char trailing_newline;
                recv(sock, &trailing_newline, 1, 0);
            }

            response.results.push_back({key, value});
        } else if (tokens[0] == "ERR") {
            // Error during range query
            string err_msg;
            for (size_t i = 1; i < tokens.size(); i++) {
                if (i > 1) {
                    err_msg += " ";
                }
                err_msg += tokens[i];
            }
            response.error = err_msg.empty() ? "Database error" : err_msg;
            net_close(sock);
            return response;
        }
    }

    net_close(sock);

    if (!response.success && response.error.empty()) {
        response.error = "Incomplete response from database";
    }

    return response;
}

DbResponse DbClient::getKeysPrefix(const string& prefix) {
    DbResponse response;
    sock_t sock = tcp_connect(leader_host, leader_port);
    if (sock == NET_INVALID) {
        response.success = false;
        response.error = "Failed to connect to database leader";
        return response;
    }

    // Send GETKEYS command with prefix (empty prefix gets all keys)
    string command = "GETKEYS " + prefix + "\n";
    send_all(sock, command);

    string line;
    while (recv_line(sock, line)) {
        line = trim(line);
        if (line == "END") {
            response.success = true;
            break;
        }

        auto tokens = split(line, ' ');
        if (tokens.size() >= 2 && tokens[0] == "KEY") {
            response.keys.push_back(tokens[1]);
        } else if (tokens.size() >= 1 && tokens[0].substr(0, 3) == "ERR") {
            response.error = line.substr(4);  // Skip "ERR "
            break;
        }
    }

    net_close(sock);
    return response;
}

DbResponse DbClient::getKeysPaging(uint32_t pageSize, uint32_t pageNum) {
    DbResponse response;
    sock_t sock = tcp_connect(leader_host, leader_port);
    if (sock == NET_INVALID) {
        response.success = false;
        response.error = "Failed to connect to database leader";
        return response;
    }

    // Send GETKEYSPAGING command
    string command = "GETKEYSPAGING " + std::to_string(pageSize) + " " + std::to_string(pageNum) + "\n";
    send_all(sock, command);

    string line;
    while (recv_line(sock, line)) {
        line = trim(line);
        if (line == "END") {
            response.success = true;
            break;
        }

        auto tokens = split(line, ' ');
        if (tokens.size() >= 2 && tokens[0] == "KEY") {
            response.keys.push_back(tokens[1]);
        } else if (tokens.size() >= 2 && tokens[0] == "TOTAL") {
            response.totalCount = std::stoul(tokens[1]);
        } else if (tokens.size() >= 1 && tokens[0].substr(0, 3) == "ERR") {
            response.error = line.substr(4);  // Skip "ERR "
            break;
        }
    }

    net_close(sock);
    return response;
}

DbResponse DbClient::optimize() {
    DbResponse response;
    sock_t sock = tcp_connect(leader_host, leader_port);
    if (sock == NET_INVALID) {
        response.success = false;
        response.error = "Failed to connect to database leader";
        return response;
    }

    send_all(sock, "OPTIMIZE\n");

    string line;
    if (!recv_line(sock, line)) {
        net_close(sock);
        response.error = "No response from database";
        return response;
    }

    net_close(sock);

    line = trim(line);
    if (line == "OK_OPTIMIZED") {
        response.success = true;
    } else if (line.substr(0, 3) == "ERR") {
        response.error = line.substr(4);  // Skip "ERR "
    } else {
        response.error = "Unexpected response: " + line;
    }

    return response;
}