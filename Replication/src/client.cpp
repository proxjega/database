// client.cpp
// CLI testavimui. HTTP serveris naudoja atskirą DbClient biblioteką
// Palaiko SET/GET/DEL/GETFF/GETFB
//
// Komandos:
// GET    – Nuskaito vieną reikšmę iš lyderio (su REDIRECT palaikymu)
// SET    – Įrašo key-value porą į lyderį
// DEL    – Ištrina raktą per lyderį
// GETFF  – Pirmyn einanti range užklausa (n raktų pradedant nuo key)
// GETFB  – Atgal einanti range užklausa (n raktų baigiant key)
// leader – Atspausdina visus CLUSTER IP ir pažymi, kuris yra LEADER

#include "../include/common.hpp"
#include "../include/rules.hpp"
#include <algorithm>
#include <iostream>
#include <string>
#include <iomanip>
#include <vector>
#include <tuple>

#define LEADER_CLIENT_API_PORT 7001

using std::cout;
using std::string;
using std::vector;

/**
 * Pasiunčia vieną užklausą į duotą host:port ir, jei gauna REDIRECT atsakymą,
 * seka redirectą vieną kartą.
 *
 * @param initialHost  – pradinis host (dažniausiai lyderio adresas).
 * @param initialPort  – pradinis portas (dažniausiai lyderio klientų portas).
 * @param payload      – tekstinė užklausa, pvz. "GET user01" (be '\n').
 *
 * Elgsena:
 *  - Prisijungia prie initialHost:initialPort
 *  - Išsiunčia payload + '\n'
 *  - Skaito vieną eilutę atsakymo
 *    - jei eilutė prasideda "REDIRECT host port", atsidaro naujas ryšys ir kartoja užklausą
 *    - kitu atveju tiesiog išspausdina atsakymo eilutę.
 *
 * Grąžina true, jei kažkoks atsakymas sėkmingai gautas ir atspausdintas,
 * false – jei nepavyko prisijungti ar gauti atsakymo.
 */
static bool do_request_follow_redirect(const string& initialHost,
                                       uint16_t initialPort,
                                       const string& payload) {
  sock_t socketMain = tcp_connect(initialHost, initialPort);
  if (socketMain == NET_INVALID) {
    std::cerr << "ERR_CONNECT\n";
    return false;
  }

  send_all(socketMain, payload + "\n");

  string responseLine;
  if (!recv_line(socketMain, responseLine)) {
    net_close(socketMain);
    std::cerr << "ERR_NO_REPLY\n";
    return false;
  }

  auto responseParts = split(trim(responseLine), ' ');

  // REDIRECT host port
  if (!responseParts.empty() &&
      responseParts[0] == "REDIRECT" &&
      responseParts.size() >= 3) {

    const string &redirectHost = responseParts[1];
    auto redirectPort = static_cast<uint16_t>(std::stoi(responseParts[2]));

    net_close(socketMain);

    sock_t socketRedirect = tcp_connect(redirectHost, redirectPort);
    if (socketRedirect == NET_INVALID) {
      std::cerr << "ERR_CONNECT\n";
      return false;
    }

    send_all(socketRedirect, payload + "\n");

    string redirectedResponse;
    if (!recv_line(socketRedirect, redirectedResponse)) {
      net_close(socketRedirect);
      std::cerr << "ERR_NO_REPLY\n";
      return false;
    }

    // Parse length-prefixed VALUE response
    auto redirectParts = split(trim(redirectedResponse), ' ');
    if (!redirectParts.empty() && redirectParts[0] == "VALUE" && redirectParts.size() >= 2) {
      string value;
      if (parse_length_prefixed_value(redirectParts, 1, socketRedirect, value)) {
        cout << "VALUE " << value << "\n";
      } else {
        cout << redirectedResponse << "\n";
      }
    } else {
      cout << redirectedResponse << "\n";
    }
    net_close(socketRedirect);
    return true;
  }

  // Parse length-prefixed VALUE response from initial request
  if (!responseParts.empty() && responseParts[0] == "VALUE" && responseParts.size() >= 2) {
    string value;
    if (parse_length_prefixed_value(responseParts, 1, socketMain, value)) {
      cout << "VALUE " << value << "\n";
    } else {
      cout << responseLine << "\n";
    }
  } else {
    cout << responseLine << "\n";
  }
  net_close(socketMain);
  return true;
}

/**
 * Pagalbinė funkcija, kuri iš host:port tipo eilutės išskiria host ir port.
 *
 * Pvz. "100.125.32.90:7101" -> hostOut = "100.125.32.90", portOut = 7101
 */
static bool split_host_port(const string& hostPortStr,
                            string& hostOut,
                            uint16_t& portOut) {
  auto colonPos = hostPortStr.find(':');
  if (colonPos == string::npos) {
    return false;
  }

  hostOut = hostPortStr.substr(0, colonPos);
  portOut = static_cast<uint16_t>(std::stoi(hostPortStr.substr(colonPos + 1)));
  return true;
}

/**
 * Nauja helper funkcija:
 * Bando automatiškai nustatyti, kuris CLUSTER mazgas yra lyderis.
 *
 * 1) Pirmiausia per follower read-only portus (7101,7102,...) siunčia SET __probe__ 1
 *    ir ieško REDIRECT host port – iš ten paima leader host.
 * 2) Jei nepavyksta (pvz. nėra followerių), fallback – žiūri, kuris CLUSTER host
 *    klauso CLIENT_PORT (7001).
 *
 * @param leaderHostOut – čia įrašomas aptiktas lyderio host.
 * @return true, jei pavyko rasti; false – jei nepavyko.
 */
static bool detect_leader_host(string &leaderHostOut) {
  // Find leader by checking which node listens on CLIENT_PORT (7001)
  // Only leader binds to CLIENT_PORT, followers use FOLLOWER_READ_PORT
  for (auto& node : CLUSTER) {
    sock_t sock = tcp_connect(node.host, CLIENT_PORT);
    if (sock == NET_INVALID) {
      continue;
    }

    // Verify it's actually a leader by sending a test GET command
    set_socket_timeouts(sock, 1000);  // 1 second timeout
    send_all(sock, "GET __leader_check__\n");
    string line;
    if (recv_line(sock, line)) {
      auto parts = split(trim(line), ' ');
      // Leader responds with VALUE or NOT_FOUND, not ERR_READ_ONLY
      if (parts.size() > 0 && (parts[0] == "VALUE" || parts[0] == "NOT_FOUND")) {
        net_close(sock);
        leaderHostOut = node.host;
        return true;
      }
    }
    net_close(sock);
  }

  return false;
}

int main(int argc, char** argv) {
  // Cluster status command: ./client status or ./client leader (alias)
  if (argc == 2 && (string(argv[1]) == "status" || string(argv[1]) == "leader")) {
    cout << "=== Cluster Status ===\n\n";

    // Query all cluster nodes
    struct NodeStatus {
      int id;
      string role;
      uint64_t term;
      int leaderId;
      uint64_t lsn;
      long long lastHBAge;
      bool reachable;
      vector<std::tuple<int, string, uint64_t>> followers; // id, status, lsn
    };

    vector<NodeStatus> nodeStatuses;
    int detectedLeaderId = 0;
    string detectedLeaderHost;

    // Query each node's control plane port
    for (auto& node : CLUSTER) {
      uint16_t controlPort = node.port;  // Control plane port (8001-8004)
      sock_t sock = tcp_connect(node.host, controlPort);

      if (sock == NET_INVALID) {
        // Node unreachable
        NodeStatus nodeStatus;
        nodeStatus.id = node.id;
        nodeStatus.reachable = false;
        nodeStatuses.push_back(nodeStatus);
        continue;
      }

      set_socket_timeouts(sock, 2000); // 2s timeout
      send_all(sock, "CLUSTER_STATUS\n");

      NodeStatus nodeStatus;
      nodeStatus.id = node.id;
      nodeStatus.reachable = true;

      string line;
      while (recv_line(sock, line)) {
        line = trim(line);
        if (line == "END") {
          break;
        }

        auto tokens = split(line, ' ');
        if (tokens.size() >= 7 && tokens[0] == "STATUS") {
          // STATUS <nodeId> <role> <term> <leaderId> <myLSN> <lastHBAge>
          nodeStatus.role = tokens[2];
          nodeStatus.term = std::stoull(tokens[3]);
          nodeStatus.leaderId = std::stoi(tokens[4]);
          nodeStatus.lsn = std::stoull(tokens[5]);
          nodeStatus.lastHBAge = std::stoll(tokens[6]);

          if (nodeStatus.role == "LEADER") {
            detectedLeaderId = nodeStatus.id;
            detectedLeaderHost = node.host;
          }
        } else if (tokens.size() >= 4 && tokens[0] == "FOLLOWER_STATUS") {
          // FOLLOWER_STATUS <followerId> <status> <lsn>
          int followerId = std::stoi(tokens[1]);
          string status = tokens[2];
          uint64_t followerLsn = std::stoull(tokens[3]);
          nodeStatus.followers.push_back(std::make_tuple(followerId, status, followerLsn));
        }
      }

      net_close(sock);
      nodeStatuses.push_back(nodeStatus);
    }

    // Display current leader
    if (detectedLeaderId > 0) {
      cout << "Current Leader: " << detectedLeaderHost << " (Node " << detectedLeaderId << ")\n\n";
    } else {
      cout << "No leader detected - election in progress?\n\n";
    }

    // Display node status table
    cout << "Node Status:\n";
    cout << "ID | Role      | LSN   | Status | Last HB Age\n";
    cout << "---|-----------|-------|--------|------------\n";

    for (auto& nodeStatus : nodeStatuses) {
      cout << nodeStatus.id << "  | ";

      if (!nodeStatus.reachable) {
        cout << "UNREACHABLE\n";
        continue;
      }

      cout << std::left << std::setw(9) << nodeStatus.role << " | "
                << std::setw(5) << nodeStatus.lsn << " | "
                << "ALIVE  | ";

      if (nodeStatus.role == "LEADER") {
        cout << "-";
      } else {
        cout << nodeStatus.lastHBAge << "ms";
      }
      cout << "\n";
    }

    // Display replication status if leader found
    bool foundLeaderWithFollowers = false;
    for (auto& nodeStatus : nodeStatuses) {
      if (nodeStatus.role == "LEADER" && !nodeStatus.followers.empty()) {
        foundLeaderWithFollowers = true;
        cout << "\nReplication Status:\n";

        int aliveCount = 0;
        uint64_t maxLag = 0;

        for (auto &follower : nodeStatus.followers) {
          int fid = std::get<0>(follower);
          string fstatus = std::get<1>(follower);
          uint64_t flsn = std::get<2>(follower);

          // Count both ALIVE and RECENT as healthy (RECENT = recently disconnected but expected to reconnect)
          if (fstatus == "ALIVE" || fstatus == "RECENT") {
            aliveCount++;
          }

          uint64_t lag = (nodeStatus.lsn > flsn) ? (nodeStatus.lsn - flsn) : 0;
          maxLag = std::max(lag, maxLag);

          cout << "  Node " << fid << ": " << fstatus << " (LSN " << flsn << ", lag " << lag << " entries)\n";
        }

        cout << "\nHealthy Followers: " << aliveCount << "/" << nodeStatus.followers.size() << "\n";
        cout << "Max LSN Lag: " << maxLag << " entries\n";
      }
    }

    if (!foundLeaderWithFollowers && detectedLeaderId > 0) {
      cout << "\nNo follower replication data available.\n";
    }

    // Split brain detection
    int leaderCount = 0;
    vector<int> leaderIds;
    for (auto& nodeStatus : nodeStatuses) {
      if (nodeStatus.reachable && nodeStatus.role == "LEADER") {
        leaderCount++;
        leaderIds.push_back(nodeStatus.id);
      }
    }

    if (leaderCount > 1) {
      cout << "\nSPLIT BRAIN DETECTED! Multiple leaders:\n";
      for (int lid : leaderIds) {
        cout << "  Node " << lid << "\n";
      }
    }

    return 0;
  }

  // Įprastas klientas SET/GET/DEL/GETFF/GETFB režimams
  if (argc < 3) {
    std::cerr << "Usage:\n"
              << "  client <leader_host> <leader_client_port> GET <k>\n"
              << "  client <leader_host> <leader_client_port> SET <k> <v>\n"
              << "  client <leader_host> <leader_client_port> DEL <k>\n"
              << "  client <leader_host> <leader_client_port> GETFF <k> [<n>]   # (default n=10)\n"
              << "  client <leader_host> <leader_client_port> GETFB <k> [<n>]   # (default n=10)\n"
              << "  client <leader_host> <leader_client_port> GETKEYS [prefix]  # list keys with optional prefix\n"
              << "  client <leader_host> <leader_client_port> GETKEYSPAGING <pageSize> <pageNum>\n"
              << "\n"
              << "Node Selection (simplified):\n"
              << "  client node1 GET <k>      # Read from Node 1 (follower reads allowed)\n"
              << "  client node2 SET <k> <v>  # Write to Node 2 (must be leader)\n"
              << "  client node3 DEL <k>      # Delete from Node 3 (must be leader)\n"
              << "  client node4 GETFF <k>    # Range query from Node 4\n"
              << "\n"
              << "Cluster Management:\n"
              << "  client status   # show comprehensive cluster health status\n"
              << "  client leader   # alias for 'status' command\n";
    return 1;
  }

  // Parse first argument - could be node alias or host
  string firstArg = argv[1];
  string leaderHost;
  uint16_t leaderPort;
  string command;
  int commandArgOffset;

  // Check for node aliases (node1, node2, node3, node4)
  if (firstArg == "node1" || firstArg == "node2" || firstArg == "node3" || firstArg == "node4") {
    int nodeId = std::stoi(firstArg.substr(4)); // Extract number from "nodeX"
    if (nodeId < 1 || nodeId > 4) {
      std::cerr << "Invalid node ID. Must be node1, node2, node3, or node4\n";
      return 1;
    }

    // Get node info from CLUSTER array
    const auto& node = CLUSTER[nodeId - 1];
    leaderHost = node.host;
    leaderPort = LEADER_CLIENT_API_PORT;  // All nodes use port LEADER_CLIENT_API_PORT for client API
    command = (argc >= 3) ? argv[2] : "";
    commandArgOffset = 3;

    cout << "→ Connecting to Node " << nodeId << " (" << leaderHost << ":" << leaderPort << ")\n";
  } else {
    // Traditional host:port format
    if (argc < 4) {
      std::cerr << "Insufficient arguments. See usage above.\n";
      return 1;
    }
    leaderHost = argv[1];
    leaderPort = static_cast<uint16_t>(std::stoi(argv[2]));
    command = argv[3];
    commandArgOffset = 4;
  }

  // Paprastas GET – visada per lyderį (su REDIRECT palaikymu)
  if (command == "GET" && argc >= (commandArgOffset + 1)) {
    return do_request_follow_redirect(
             leaderHost, leaderPort,
             "GET " + string(argv[commandArgOffset])
           ) ? 0 : 1;
  }
  // SET – rašo į lyderį (užklausos forma: "SET key <value_len> value")
  if ((command == "SET") && argc >= (commandArgOffset + 2)) {
    string key = argv[commandArgOffset];
    string value = argv[commandArgOffset + 1];
    // Use length-prefixed format matching HTTP server implementation
    string setCommand = "SET " + key + " " + std::to_string(value.length()) + " " + value;
    return do_request_follow_redirect(
             leaderHost, leaderPort,
             setCommand
           ) ? 0 : 1;
  }
  // DEL – trina key per lyderį
  if (command == "DEL" && argc >= (commandArgOffset + 1)) {
    return do_request_follow_redirect(
             leaderHost, leaderPort,
             "DEL " + string(argv[commandArgOffset])
           ) ? 0 : 1;
  }

  // GETFF <key> <n> - Forward range query to leader
  if (command == "GETFF" && argc >= (commandArgOffset + 1)) {
    string key = argv[commandArgOffset];
    string count = (argc >= (commandArgOffset + 2)) ? argv[commandArgOffset + 1] : "10";

    sock_t sock = tcp_connect(leaderHost, leaderPort);
    if (sock == NET_INVALID) {
      std::cerr << "ERR_CONNECT\n";
      return 1;
    }

    send_all(sock, "GETFF " + key + " " + count + "\n");

    string line;
    while (recv_line(sock, line)) {
      if (line == "END") {
        break;
      }
      cout << line << "\n";
    }
    net_close(sock);
    return 0;
  }

  // GETFB <key> <n> - Backward range query to leader
  if (command == "GETFB" && argc >= (commandArgOffset + 1)) {
    string key = argv[commandArgOffset];
    string count = (argc >= (commandArgOffset + 2)) ? argv[commandArgOffset + 1] : "10";

    sock_t sock = tcp_connect(leaderHost, leaderPort);
    if (sock == NET_INVALID) {
      std::cerr << "ERR_CONNECT\n";
      return 1;
    }

    send_all(sock, "GETFB " + key + " " + count + "\n");

    string line;
    while (recv_line(sock, line)) {
      if (line == "END") {
        break;
      }
      cout << line << "\n";
    }

    if (command == "COMPACT") {
      sock_t sock = tcp_connect(leaderHost, leaderPort);
      if (sock == NET_INVALID) {
        return 1;
      }

      send_all(sock, "COMPACT\n");

      string response;
      if (recv_line(sock, response)) {
        cout << response << "\n";
      }
    }

    net_close(sock);
    return 0;
  }

  // GETKEYS [prefix] - List all keys or keys with prefix
  if (command == "GETKEYS") {
    string prefix = (argc >= (commandArgOffset + 1)) ? argv[commandArgOffset] : "";

    sock_t sock = tcp_connect(leaderHost, leaderPort);
    if (sock == NET_INVALID) {
      std::cerr << "ERR_CONNECT\n";
      return 1;
    }

    string cmd = prefix.empty() ? "GETKEYS" : ("GETKEYS " + prefix);
    send_all(sock, cmd + "\n");

    string line;
    int count = 0;
    while (recv_line(sock, line)) {
      if (line == "END") {
        break;
      }
      if (line.rfind("KEY ", 0) == 0) {
        cout << line.substr(4) << "\n";
        count++;
      } else if (line.rfind("ERR ", 0) == 0) {
        std::cerr << line << "\n";
        net_close(sock);
        return 1;
      }
    }
    cout << "Total: " << count << " keys\n";
    net_close(sock);
    return 0;
  }

  // GETKEYSPAGING <pageSize> <pageNum> - Paginated key listing
  if (command == "GETKEYSPAGING" && argc >= (commandArgOffset + 2)) {
    string pageSize = argv[commandArgOffset];
    string pageNum = argv[commandArgOffset + 1];

    sock_t sock = tcp_connect(leaderHost, leaderPort);
    if (sock == NET_INVALID) {
      std::cerr << "ERR_CONNECT\n";
      return 1;
    }

    send_all(sock, "GETKEYSPAGING " + pageSize + " " + pageNum + "\n");

    string line;
    uint64_t total = 0;
    int count = 0;
    while (recv_line(sock, line)) {
      if (line == "END") {
        break;
      }
      if (line.rfind("TOTAL ", 0) == 0) {
        total = std::stoull(line.substr(6));
      } else if (line.rfind("KEY ", 0) == 0) {
        cout << line.substr(4) << "\n";
        count++;
      } else if (line.rfind("ERR ", 0) == 0) {
        std::cerr << line << "\n";
        net_close(sock);
        return 1;
      }
    }
    cout << "Page " << pageNum << ": " << count << " keys (total: " << total << ")\n";
    net_close(sock);
    return 0;
  }

  std::cerr << "Bad args. See usage above.\n";
  return 1;
}
