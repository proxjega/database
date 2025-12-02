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
#include "../include/rules.hpp"   // kad matytume CLUSTER, CLIENT_PORT, FOLLOWER_READ_PORT
#include <iostream>
#include <string>
#include <iomanip>
#include <vector>
#include <tuple>

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
static bool do_request_follow_redirect(const std::string& initialHost,
                                       uint16_t initialPort,
                                       const std::string& payload) {
  sock_t socketMain = tcp_connect(initialHost, initialPort);
  if (socketMain == NET_INVALID) {
    std::cerr << "ERR_CONNECT\n";
    return false;
  }

  send_all(socketMain, payload + "\n");

  std::string responseLine;
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

    const std::string &redirectHost = responseParts[1];
    uint16_t redirectPort = static_cast<uint16_t>(std::stoi(responseParts[2]));

    net_close(socketMain);

    sock_t socketRedirect = tcp_connect(redirectHost, redirectPort);
    if (socketRedirect == NET_INVALID) {
      std::cerr << "ERR_CONNECT\n";
      return false;
    }

    send_all(socketRedirect, payload + "\n");

    std::string redirectedResponse;
    if (!recv_line(socketRedirect, redirectedResponse)) {
      net_close(socketRedirect);
      std::cerr << "ERR_NO_REPLY\n";
      return false;
    }

    // Parse length-prefixed VALUE response
    auto redirectParts = split(trim(redirectedResponse), ' ');
    if (!redirectParts.empty() && redirectParts[0] == "VALUE" && redirectParts.size() >= 2) {
      std::string value;
      if (parse_length_prefixed_value(redirectParts, 1, socketRedirect, value)) {
        std::cout << "VALUE " << value << "\n";
      } else {
        std::cout << redirectedResponse << "\n";
      }
    } else {
      std::cout << redirectedResponse << "\n";
    }
    net_close(socketRedirect);
    return true;
  }

  // Parse length-prefixed VALUE response from initial request
  if (!responseParts.empty() && responseParts[0] == "VALUE" && responseParts.size() >= 2) {
    std::string value;
    if (parse_length_prefixed_value(responseParts, 1, socketMain, value)) {
      std::cout << "VALUE " << value << "\n";
    } else {
      std::cout << responseLine << "\n";
    }
  } else {
    std::cout << responseLine << "\n";
  }
  net_close(socketMain);
  return true;
}

/**
 * Pagalbinė funkcija, kuri iš host:port tipo eilutės išskiria host ir port.
 *
 * Pvz. "100.125.32.90:7101" -> hostOut = "100.125.32.90", portOut = 7101
 */
static bool split_host_port(const std::string& hostPortStr,
                            std::string& hostOut,
                            uint16_t& portOut) {
  auto colonPos = hostPortStr.find(':');
  if (colonPos == std::string::npos) {
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
static bool detect_leader_host(std::string &leaderHostOut) {
  // 1) Bandome per follower read-only portus ir REDIRECT
  for (auto& node : CLUSTER) {
    uint16_t fport = FOLLOWER_READ_PORT(node.id);
    sock_t sock = tcp_connect(node.host, fport);
    if (sock == NET_INVALID) {
      continue;
    }

    // bet kokia ne-GET komanda followeriui turi grąžinti REDIRECT
    send_all(sock, "SET __probe__ 1\n");
    std::string line;
    if (!recv_line(sock, line)) {
      net_close(sock);
      continue;
    }
    net_close(sock);

    auto parts = split(trim(line), ' ');
    if (parts.size() >= 3 && parts[0] == "REDIRECT") {
      leaderHostOut = parts[1]; // REDIRECT <host> <port>
      return true;
    }
  }

  // 2) Fallback – žiūrim, kuris host'as klauso CLIENT_PORT
  for (auto& node : CLUSTER) {
    sock_t s = tcp_connect(node.host, CLIENT_PORT);
    if (s == NET_INVALID) {
      continue;
    }
    // jei prisijungėm prie 7001 – laikom, kad ten leader
    net_close(s);
    leaderHostOut = node.host;
    return true;
  }

  return false;
}

int main(int argc, char** argv) {
  // Cluster status command: ./client status or ./client leader (alias)
  if (argc == 2 && (std::string(argv[1]) == "status" || std::string(argv[1]) == "leader")) {
    std::cout << "=== Cluster Status ===\n\n";

    // Query all cluster nodes
    struct NodeStatus {
      int id;
      std::string role;
      uint64_t term;
      int leaderId;
      uint64_t lsn;
      long long lastHBAge;
      bool reachable;
      std::vector<std::tuple<int, std::string, uint64_t>> followers; // id, status, lsn
    };

    std::vector<NodeStatus> nodeStatuses;
    int detectedLeaderId = 0;
    std::string detectedLeaderHost;

    // Query each node's control plane port
    for (auto& node : CLUSTER) {
      uint16_t controlPort = node.port;  // Control plane port (8001-8004)
      sock_t s = tcp_connect(node.host, controlPort);

      if (s == NET_INVALID) {
        // Node unreachable
        NodeStatus ns;
        ns.id = node.id;
        ns.reachable = false;
        nodeStatuses.push_back(ns);
        continue;
      }

      set_socket_timeouts(s, 2000); // 2s timeout
      send_all(s, "CLUSTER_STATUS\n");

      NodeStatus ns;
      ns.id = node.id;
      ns.reachable = true;

      std::string line;
      while (recv_line(s, line)) {
        line = trim(line);
        if (line == "END") break;

        auto tokens = split(line, ' ');
        if (tokens.size() >= 7 && tokens[0] == "STATUS") {
          // STATUS <nodeId> <role> <term> <leaderId> <myLSN> <lastHBAge>
          ns.role = tokens[2];
          ns.term = std::stoull(tokens[3]);
          ns.leaderId = std::stoi(tokens[4]);
          ns.lsn = std::stoull(tokens[5]);
          ns.lastHBAge = std::stoll(tokens[6]);

          if (ns.role == "LEADER") {
            detectedLeaderId = ns.id;
            detectedLeaderHost = node.host;
          }
        } else if (tokens.size() >= 4 && tokens[0] == "FOLLOWER_STATUS") {
          // FOLLOWER_STATUS <followerId> <status> <lsn>
          int followerId = std::stoi(tokens[1]);
          std::string status = tokens[2];
          uint64_t followerLsn = std::stoull(tokens[3]);
          ns.followers.push_back(std::make_tuple(followerId, status, followerLsn));
        }
      }

      net_close(s);
      nodeStatuses.push_back(ns);
    }

    // Display current leader
    if (detectedLeaderId > 0) {
      std::cout << "Current Leader: " << detectedLeaderHost << " (Node " << detectedLeaderId << ")\n\n";
    } else {
      std::cout << "⚠️  No leader detected - election in progress?\n\n";
    }

    // Display node status table
    std::cout << "Node Status:\n";
    std::cout << "ID | Role      | LSN   | Status | Last HB Age\n";
    std::cout << "---|-----------|-------|--------|------------\n";

    for (auto& ns : nodeStatuses) {
      std::cout << ns.id << "  | ";

      if (!ns.reachable) {
        std::cout << "UNREACHABLE\n";
        continue;
      }

      std::cout << std::left << std::setw(9) << ns.role << " | "
                << std::setw(5) << ns.lsn << " | "
                << "ALIVE  | ";

      if (ns.role == "LEADER") {
        std::cout << "-";
      } else {
        std::cout << ns.lastHBAge << "ms";
      }
      std::cout << "\n";
    }

    // Display replication status if leader found
    bool foundLeaderWithFollowers = false;
    for (auto& ns : nodeStatuses) {
      if (ns.role == "LEADER" && !ns.followers.empty()) {
        foundLeaderWithFollowers = true;
        std::cout << "\nReplication Status:\n";

        int aliveCount = 0;
        uint64_t maxLag = 0;

        for (auto& f : ns.followers) {
          int fid = std::get<0>(f);
          std::string fstatus = std::get<1>(f);
          uint64_t flsn = std::get<2>(f);

          // Count both ALIVE and RECENT as healthy (RECENT = recently disconnected but expected to reconnect)
          if (fstatus == "ALIVE" || fstatus == "RECENT") aliveCount++;

          uint64_t lag = (ns.lsn > flsn) ? (ns.lsn - flsn) : 0;
          if (lag > maxLag) maxLag = lag;

          std::cout << "  Node " << fid << ": " << fstatus << " (LSN " << flsn << ", lag " << lag << " entries)\n";
        }

        std::cout << "\nHealthy Followers: " << aliveCount << "/" << ns.followers.size() << "\n";
        std::cout << "Max LSN Lag: " << maxLag << " entries\n";
      }
    }

    if (!foundLeaderWithFollowers && detectedLeaderId > 0) {
      std::cout << "\nNo follower replication data available.\n";
    }

    // Split brain detection
    int leaderCount = 0;
    std::vector<int> leaderIds;
    for (auto& ns : nodeStatuses) {
      if (ns.reachable && ns.role == "LEADER") {
        leaderCount++;
        leaderIds.push_back(ns.id);
      }
    }

    if (leaderCount > 1) {
      std::cout << "\n⚠️  SPLIT BRAIN DETECTED! Multiple leaders:\n";
      for (int lid : leaderIds) {
        std::cout << "  Node " << lid << "\n";
      }
    }

    return 0;
  }

  // Įprastas klientas SET/GET/DEL/GETFF/GETFB režimams
  if (argc < 4) {
    std::cerr << "Usage:\n"
              << "  client <leader_host> <leader_client_port> GET <k>\n"
              << "  client <leader_host> <leader_client_port> SET <k> <v>\n"
              << "  client <leader_host> <leader_client_port> DEL <k>\n"
              << "  client <leader_host> <leader_client_port> GETFF <k> [<n>]   # (default n=10)\n"
              << "  client <leader_host> <leader_client_port> GETFB <k> [<n>]   # (default n=10)\n"
              << "  client <leader_host> <leader_client_port> GETKEYS [prefix]  # list keys with optional prefix\n"
              << "  client <leader_host> <leader_client_port> GETKEYSPAGING <pageSize> <pageNum>\n"
              << "  client status   # show comprehensive cluster health status\n"
              << "  client leader   # alias for 'status' command\n";
    return 1;
  }

  // Lyderio adresas (čia tas, į kurį visada kreipiamės SET/DEL ir baziniam GET)
  std::string leaderHost = argv[1];
  uint16_t leaderPort = static_cast<uint16_t>(std::stoi(argv[2]));
  std::string command = argv[3];

  // Paprastas GET – visada per lyderį (su REDIRECT palaikymu)
  if (command == "GET" && argc >= 5) {
    return do_request_follow_redirect(
             leaderHost, leaderPort,
             "GET " + std::string(argv[4])
           ) ? 0 : 1;
  }
  // SET / PUT – rašo į lyderį (užklausos forma: "SET key <value_len> value")
  if ((command == "SET" || command == "PUT") && argc >= 6) {
    std::string key = argv[4];
    std::string value = argv[5];
    // Use length-prefixed format matching HTTP server implementation
    std::string setCommand = "SET " + key + " " + std::to_string(value.length()) + " " + value;
    return do_request_follow_redirect(
             leaderHost, leaderPort,
             setCommand
           ) ? 0 : 1;
  }
  // DEL – trina key per lyderį
  if (command == "DEL" && argc >= 5) {
    return do_request_follow_redirect(
             leaderHost, leaderPort,
             "DEL " + std::string(argv[4])
           ) ? 0 : 1;
  }

  /*
  // SENA GETFF/GETFB follower'ių maršrutizavimo logika
  // Dabar GETFF/GETFB yra paprastos range užklausos siunčiamos į lyderį
  if (command == "GETFF" && argc >= 6) {
    std::string followerHost;
    uint16_t followerPort;
    if (!split_host_port(argv[5], followerHost, followerPort)) {
      std::cerr << "bad follower host:port\n";
      return 1;
    }
    return do_request_follow_redirect(
             followerHost, followerPort,
             "GET " + std::string(argv[4])
           ) ? 0 : 1;
  }
  if (command == "GETFB" && argc >= 6) {
    std::string followerHost;
    uint16_t followerPort;
    if (!split_host_port(argv[5], followerHost, followerPort)) {
      std::cerr << "bad follower host:port\n";
      return 1;
    }

    // 1) Bandome jungtis prie followerio
    sock_t followerSocket = tcp_connect(followerHost, followerPort);
    if (followerSocket != NET_INVALID) {
      send_all(followerSocket, "GET " + std::string(argv[4]) + "\n");

      std::string followerReply;
      if (recv_line(followerSocket, followerReply)) {
        net_close(followerSocket);

        if (followerReply == "NOT_FOUND") {
          // fallback į lyderį
          return do_request_follow_redirect(
                   leaderHost, leaderPort,
                   "GET " + std::string(argv[4])
                 ) ? 0 : 1;
        }
        if (followerReply.rfind("REDIRECT ", 0) == 0) {
          auto parts = split(trim(followerReply), ' ');
          if (parts.size() >= 3) {
            const std::string &redirectHost = parts[1];
            uint16_t redirectPort =
              static_cast<uint16_t>(std::stoi(parts[2]));

            return do_request_follow_redirect(
                     redirectHost, redirectPort,
                     "GET " + std::string(argv[4])
                   ) ? 0 : 1;
          }
        }
        else {
          std::cout << followerReply << "\n";
          return 0;
        }
      } else {
        net_close(followerSocket);
      }
    }

    // 2) Fallback: nepavyko – per lyderį
    return do_request_follow_redirect(
             leaderHost, leaderPort,
             "GET " + std::string(argv[4])
           ) ? 0 : 1;
  }
  */

  // GETFF <key> <n> - Forward range query to leader
  if (command == "GETFF" && argc >= 5) {
    std::string key = argv[4];
    std::string count = (argc >= 6) ? argv[5] : "10";

    sock_t s = tcp_connect(leaderHost, leaderPort);
    if (s == NET_INVALID) {
      std::cerr << "ERR_CONNECT\n";
      return 1;
    }

    send_all(s, "GETFF " + key + " " + count + "\n");

    std::string line;
    while (recv_line(s, line)) {
      if (line == "END") break;
      std::cout << line << "\n";
    }
    net_close(s);
    return 0;
  }

  // GETFB <key> <n> - Backward range query to leader
  if (command == "GETFB" && argc >= 5) {
    std::string key = argv[4];
    std::string count = (argc >= 6) ? argv[5] : "10";

    sock_t s = tcp_connect(leaderHost, leaderPort);
    if (s == NET_INVALID) {
      std::cerr << "ERR_CONNECT\n";
      return 1;
    }

    send_all(s, "GETFB " + key + " " + count + "\n");

    std::string line;
    while (recv_line(s, line)) {
      if (line == "END") break;
      std::cout << line << "\n";
    }

    if (command == "COMPACT") {
      sock_t sock = tcp_connect(leaderHost, leaderPort);
      if (sock == NET_INVALID) {
        return 1;
      }

      send_all(sock, "COMPACT\n");

      std::string response;
      if (recv_line(sock, response)) {
        std::cout << response << "\n";
      }
    }

    net_close(s);
    return 0;
  }

  // GETKEYS [prefix] - List all keys or keys with prefix
  if (command == "GETKEYS") {
    std::string prefix = (argc >= 5) ? argv[4] : "";

    sock_t s = tcp_connect(leaderHost, leaderPort);
    if (s == NET_INVALID) {
      std::cerr << "ERR_CONNECT\n";
      return 1;
    }

    std::string cmd = prefix.empty() ? "GETKEYS" : ("GETKEYS " + prefix);
    send_all(s, cmd + "\n");

    std::string line;
    int count = 0;
    while (recv_line(s, line)) {
      if (line == "END") break;
      if (line.rfind("KEY ", 0) == 0) {
        std::cout << line.substr(4) << "\n";
        count++;
      } else if (line.rfind("ERR ", 0) == 0) {
        std::cerr << line << "\n";
        net_close(s);
        return 1;
      }
    }
    std::cout << "Total: " << count << " keys\n";
    net_close(s);
    return 0;
  }

  // GETKEYSPAGING <pageSize> <pageNum> - Paginated key listing
  if (command == "GETKEYSPAGING" && argc >= 6) {
    std::string pageSize = argv[4];
    std::string pageNum = argv[5];

    sock_t s = tcp_connect(leaderHost, leaderPort);
    if (s == NET_INVALID) {
      std::cerr << "ERR_CONNECT\n";
      return 1;
    }

    send_all(s, "GETKEYSPAGING " + pageSize + " " + pageNum + "\n");

    std::string line;
    uint64_t total = 0;
    int count = 0;
    while (recv_line(s, line)) {
      if (line == "END") break;
      if (line.rfind("TOTAL ", 0) == 0) {
        total = std::stoull(line.substr(6));
      } else if (line.rfind("KEY ", 0) == 0) {
        std::cout << line.substr(4) << "\n";
        count++;
      } else if (line.rfind("ERR ", 0) == 0) {
        std::cerr << line << "\n";
        net_close(s);
        return 1;
      }
    }
    std::cout << "Page " << pageNum << ": " << count << " keys (total: " << total << ")\n";
    net_close(s);
    return 0;
  }

  std::cerr << "Bad args. See usage above.\n";
  return 1;
}
