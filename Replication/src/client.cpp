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

    std::cout << redirectedResponse << "\n";
    net_close(socketRedirect);
    return true;
  }

  std::cout << responseLine << "\n";
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
  // Specialus režimas: ./client leader
  if (argc == 2 && std::string(argv[1]) == "leader") {
    std::string leaderHost;
    if (!detect_leader_host(leaderHost)) {
      std::cerr << "Could not detect leader\n";
      return 1;
    }

    // Išspausdinam visus CLUSTER host'us, prie leader – LEADER
    for (auto& node : CLUSTER) {
      std::cout << node.host;
      if (node.host == leaderHost) {
        std::cout << " LEADER";
      }
      std::cout << "\n";
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
              << "  client leader   # atspausdina visus CLUSTER IP ir pažymi LEADER\n";
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
  // SET / PUT – rašo į lyderį (užklausos forma: "SET key value")
  if ((command == "SET" || command == "PUT") && argc >= 6) {
    return do_request_follow_redirect(
             leaderHost, leaderPort,
             "SET " + std::string(argv[4]) + " " + std::string(argv[5])
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

  std::cerr << "Bad args. See usage above.\n";
  return 1;
}
