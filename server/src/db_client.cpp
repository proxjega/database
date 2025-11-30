#include "../include/db_client.hpp"
#include <iostream>

DbClient::DbClient(const std::string& initial_host, uint16_t port)
    : leader_host(initial_host), leader_port(port) {}

DbResponse DbClient::send_simple_request(const std::string& command) {
    DbResponse response;
    response.success = false;

    sock_t s = tcp_connect(leader_host, leader_port);
    if (s == NET_INVALID) {
        response.error = "Failed to connect to database leader";
        return response;
    }

    if (!send_all(s, command + "\n")) {
        net_close(s);
        response.error = "Failed to send request to database";
        return response;
    }

    std::string line;
    if (!recv_line(s, line)) {
        net_close(s);
        response.error = "No response from database";
        return response;
    }

    // Parse response
    line = trim(line);
    auto tokens = split(line, ' ');

    if (tokens.empty()) {
        net_close(s);
        response.error = "Empty response from database";
        return response;
    }

    // Handle different response types
    if (tokens[0] == "VALUE" && tokens.size() >= 3) {
        // Parse length-prefixed value: "VALUE <value_len> <value>"
        if (!parse_length_prefixed_value(tokens, 1, s, response.value)) {
            net_close(s);
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
        std::string err_msg;
        for (size_t i = 1; i < tokens.size(); i++) {
            if (i > 1) err_msg += " ";
            err_msg += tokens[i];
        }
        response.error = err_msg.empty() ? "Database error" : err_msg;
    } else {
        // Unknown response format
        response.error = "Unexpected response: " + line;
    }

    net_close(s);
    return response;
}

DbResponse DbClient::get(const std::string& key) {
    return send_simple_request("GET " + key);
}

DbResponse DbClient::set(const std::string& key, const std::string& value) {
    std::string command = "SET " + key + " " + std::to_string(value.length()) + " " + value;
    return send_simple_request(command);
}

DbResponse DbClient::del(const std::string& key) {
    return send_simple_request("DEL " + key);
}

DbResponse DbClient::getff(const std::string& key, uint32_t count) {
    DbResponse response;
    response.success = false;

    sock_t s = tcp_connect(leader_host, leader_port);
    if (s == NET_INVALID) {
        response.error = "Failed to connect to database leader";
        return response;
    }

    std::string command = "GETFF " + key + " " + std::to_string(count);
    if (!send_all(s, command + "\n")) {
        net_close(s);
        response.error = "Failed to send request to database";
        return response;
    }

    // Read multiple KEY_VALUE lines until END
    std::string line;
    while (recv_line(s, line)) {
        line = trim(line);

        if (line == "END") {
            response.success = true;
            break;
        }

        auto tokens = split(line, ' ');
        if (tokens.size() >= 3 && tokens[0] == "KEY_VALUE") {
            // KEY_VALUE <key> <value_len> <value...>
            // Note: value may contain spaces and/or newlines, so we need length-prefixed parsing
            std::string k = tokens[1];
            std::string v;

            // Parse value_len to know how many bytes to read
            size_t value_len = 0;
            try {
                value_len = std::stoull(tokens[2]);
            } catch (...) {
                response.error = "Invalid value length in KEY_VALUE";
                net_close(s);
                return response;
            }

            // Reconstruct value from remaining tokens on this line
            std::string reconstructed;
            for (size_t i = 3; i < tokens.size(); i++) {
                if (i > 3) reconstructed += " ";
                reconstructed += tokens[i];
            }

            // Check if we have the complete value already
            if (reconstructed.length() == value_len) {
                v = reconstructed;
            } else if (reconstructed.length() > value_len) {
                // Have more than expected, truncate
                v = reconstructed.substr(0, value_len);
            } else {
                // Need to read more bytes from socket
                // recv_line() stripped the \n, so account for it
                size_t bytes_needed = value_len - reconstructed.length();
                v = reconstructed;

                // Add back the newline that recv_line() stripped (if value continues)
                if (bytes_needed > 0 && reconstructed.length() > 0) {
                    v += "\n";
                    bytes_needed -= 1;
                }

                // Read remaining bytes directly from socket
                if (bytes_needed > 0) {
                    std::vector<char> buffer(bytes_needed);
                    size_t total_read = 0;

                    while (total_read < bytes_needed) {
                        ssize_t received = recv(s, buffer.data() + total_read,
                                               bytes_needed - total_read, 0);
                        if (received <= 0) {
                            response.error = "Failed to read complete value";
                            net_close(s);
                            return response;
                        }
                        total_read += received;
                    }
                    v.append(buffer.data(), bytes_needed);
                }

                // After reading the value bytes, we need to consume the trailing \n
                // that the leader added after the value
                char trailing_newline;
                recv(s, &trailing_newline, 1, 0);
            }

            response.results.push_back({k, v});
        } else if (tokens[0] == "ERR") {
            // Error during range query
            std::string err_msg;
            for (size_t i = 1; i < tokens.size(); i++) {
                if (i > 1) err_msg += " ";
                err_msg += tokens[i];
            }
            response.error = err_msg.empty() ? "Database error" : err_msg;
            net_close(s);
            return response;
        }
    }

    net_close(s);

    if (!response.success && response.error.empty()) {
        response.error = "Incomplete response from database";
    }

    return response;
}

DbResponse DbClient::getfb(const std::string& key, uint32_t count) {
    DbResponse response;
    response.success = false;

    sock_t s = tcp_connect(leader_host, leader_port);
    if (s == NET_INVALID) {
        response.error = "Failed to connect to database leader";
        return response;
    }

    std::string command = "GETFB " + key + " " + std::to_string(count);
    if (!send_all(s, command + "\n")) {
        net_close(s);
        response.error = "Failed to send request to database";
        return response;
    }

    // Read multiple KEY_VALUE lines until END
    std::string line;
    while (recv_line(s, line)) {
        line = trim(line);

        if (line == "END") {
            response.success = true;
            break;
        }

        auto tokens = split(line, ' ');
        if (tokens.size() >= 3 && tokens[0] == "KEY_VALUE") {
            // KEY_VALUE <key> <value_len> <value...>
            // Note: value may contain spaces and/or newlines, so we need length-prefixed parsing
            std::string k = tokens[1];
            std::string v;

            // Parse value_len to know how many bytes to read
            size_t value_len = 0;
            try {
                value_len = std::stoull(tokens[2]);
            } catch (...) {
                response.error = "Invalid value length in KEY_VALUE";
                net_close(s);
                return response;
            }

            // Reconstruct value from remaining tokens on this line
            std::string reconstructed;
            for (size_t i = 3; i < tokens.size(); i++) {
                if (i > 3) reconstructed += " ";
                reconstructed += tokens[i];
            }

            // Check if we have the complete value already
            if (reconstructed.length() == value_len) {
                v = reconstructed;
            } else if (reconstructed.length() > value_len) {
                // Have more than expected, truncate
                v = reconstructed.substr(0, value_len);
            } else {
                // Need to read more bytes from socket
                // recv_line() stripped the \n, so account for it
                size_t bytes_needed = value_len - reconstructed.length();
                v = reconstructed;

                // Add back the newline that recv_line() stripped (if value continues)
                if (bytes_needed > 0 && reconstructed.length() > 0) {
                    v += "\n";
                    bytes_needed -= 1;
                }

                // Read remaining bytes directly from socket
                if (bytes_needed > 0) {
                    std::vector<char> buffer(bytes_needed);
                    size_t total_read = 0;

                    while (total_read < bytes_needed) {
                        ssize_t received = recv(s, buffer.data() + total_read,
                                               bytes_needed - total_read, 0);
                        if (received <= 0) {
                            response.error = "Failed to read complete value";
                            net_close(s);
                            return response;
                        }
                        total_read += received;
                    }
                    v.append(buffer.data(), bytes_needed);
                }

                // After reading the value bytes, we need to consume the trailing \n
                // that the leader added after the value
                char trailing_newline;
                recv(s, &trailing_newline, 1, 0);
            }

            response.results.push_back({k, v});
        } else if (tokens[0] == "ERR") {
            // Error during range query
            std::string err_msg;
            for (size_t i = 1; i < tokens.size(); i++) {
                if (i > 1) err_msg += " ";
                err_msg += tokens[i];
            }
            response.error = err_msg.empty() ? "Database error" : err_msg;
            net_close(s);
            return response;
        }
    }

    net_close(s);

    if (!response.success && response.error.empty()) {
        response.error = "Incomplete response from database";
    }

    return response;
}

DbResponse DbClient::optimize() {
    DbResponse response;
    sock_t s = tcp_connect(leader_host, leader_port);
    if (s == NET_INVALID) {
        response.success = false;
        response.error = "Failed to connect to database leader";
        return response;
    }

    send_all(s, "OPTIMIZE\n");

    std::string line;
    if (!recv_line(s, line)) {
        net_close(s);
        response.error = "No response from database";
        return response;
    }

    net_close(s);

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
