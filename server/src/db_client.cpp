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

    net_close(s);

    // Parse response
    line = trim(line);
    auto tokens = split(line, ' ');

    if (tokens.empty()) {
        response.error = "Empty response from database";
        return response;
    }

    // Handle different response types
    if (tokens[0] == "VALUE" && tokens.size() >= 2) {
        response.success = true;
        response.value = tokens[1];
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

    return response;
}

DbResponse DbClient::get(const std::string& key) {
    return send_simple_request("GET " + key);
}

DbResponse DbClient::set(const std::string& key, const std::string& value) {
    return send_simple_request("SET " + key + " " + value);
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
            // KEY_VALUE <key> <value>
            std::string k = tokens[1];
            std::string v = tokens[2];
            // Handle values with spaces (rejoin remaining tokens)
            for (size_t i = 3; i < tokens.size(); i++) {
                v += " " + tokens[i];
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
            // KEY_VALUE <key> <value>
            std::string k = tokens[1];
            std::string v = tokens[2];
            // Handle values with spaces (rejoin remaining tokens)
            for (size_t i = 3; i < tokens.size(); i++) {
                v += " " + tokens[i];
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
