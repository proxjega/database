// client.cpp
// Klientas, kuris kalbasi su lyderiu (arba followeriais) per TCP.
// Palaiko SET/GET/DEL ir GETFF/GETFB.
//
// GET   – skaito reikšmę iš lyderio (su REDIRECT palaikymu).
// SET   – rašo reikšmę į lyderį.
// DEL   – trina raktą per lyderį.
// GETFF – eina tiesiai į konkretų followerį (forward).
// GETFB – pirmiausiai bando followerį, jei nieko – kreipiasi į lyderį (fallback).

#include "common.hpp"
#include <iostream>
#include <string>

/**
 * Pasiunčia vieną užklausą į duotą host:port ir, jei gauna REDIRECT atsakymą,
 * seka redirectą vieną kartą.
 *
 * @param initialHost  – pradinis host (dažniausiai lyderio adresas).
 * @param initialPort  – pradinis portas (dažniausiai lyderio klientų portas) 99% - 7001.
 * @param payload      – tekstinė užklausa, pvz. "GET user01" (be '\n').
 *
 * Elgsena:
 *  - Prisijungia prie initialHost:initialPort
 *  - Išsiunčia payload + '\n'
 *  - Skaito vieną eilutę atsakymo
 *    - jei eilutė prasideda "REDIRECT host port", atsidaro naujas ryšys ir kartoja užklausą
 *    - kitu atveju tiesiog išspausdina atsakymo eilutę.
 *
 * Grąžina true, jei kažkoks atsakymas sėkmingai gautas ir atspausdintas (OK <skaicius>),
 * false – jei nepavyko prisijungti ar gauti atsakymo. (ERR_CONNECT)
 */
static bool do_request_follow_redirect(const std::string& initialHost,
                                       uint16_t initialPort,
                                       const std::string& payload) {
  // 1-as bandymas – jungiamės prie pradinių host/port
  sock_t socketMain = tcp_connect(initialHost, initialPort);
  if (socketMain == NET_INVALID) {
    std::cerr << "ERR_CONNECT\n";
    return false;
  }

  // Išsiunčiam komandą (pvz. "GET key\n" ar "SET key value\n")
  send_all(socketMain, payload + "\n");

  // Laukiam vienos atsakymo eilutės
  std::string responseLine;
  if (!recv_line(socketMain, responseLine)) {
    net_close(socketMain);
    std::cerr << "ERR_NO_REPLY\n";
    return false;
  }

  // Suskaidom atsakymą pagal tarpą, kad pamatytume ar tai REDIRECT
  auto responseParts = split(trim(responseLine), ' ');

  // Patikrinam: jei pirmas žodis "REDIRECT" ir turim bent 3 dalis –
  // [0]: "REDIRECT", [1]: naujasHost, [2]: naujasPort
  if (!responseParts.empty() &&
      responseParts[0] == "REDIRECT" &&
      responseParts.size() >= 3) {

    // Vieną kartą sekam redirectą
    std::string redirectHost = responseParts[1];
    uint16_t redirectPort = static_cast<uint16_t>(std::stoi(responseParts[2]));

    // Uždarom seną socketą
    net_close(socketMain);

    // Bandome jungtis į naują redirect adresą
    sock_t socketRedirect = tcp_connect(redirectHost, redirectPort);
    if (socketRedirect == NET_INVALID) {
      std::cerr << "ERR_CONNECT\n";
      return false;
    }

    // Pakartojame tą pačią užklausą naujam serveriui
    send_all(socketRedirect, payload + "\n");

    std::string redirectedResponse;
    if (!recv_line(socketRedirect, redirectedResponse)) {
      net_close(socketRedirect);
      std::cerr << "ERR_NO_REPLY\n";
      return false;
    }

    // Išspausdinam, ką gavom po redirect
    std::cout << redirectedResponse << "\n";
    net_close(socketRedirect);
    return true;
  } else {
    // Jokio redirect – tiesiog išspausdinam atsakymą iš pirmo serverio
    std::cout << responseLine << "\n";
    net_close(socketMain);
    return true;
  }
}

/**
 * Pagalbinė funkcija, kuri iš host:port tipo eilutės išskiria host ir port.
 *
 * Pvz. "100.125.32.90:7101" -> hostOut = "100.125.32.90", portOut = 7101
 *
 * @param hostPortStr  – įvestis "host:port".
 * @param hostOut      – išvestinis host pavadinimas / IP.
 * @param portOut      – išvestinis portas.
 *
 * @return true, jei pavyko išskaidyti; false, jei nerado ':' simbolio.
 */
static bool split_host_port(const std::string& hostPortStr,
                            std::string& hostOut,
                            uint16_t& portOut) {
  auto colonPos = hostPortStr.find(':');
  if (colonPos == std::string::npos) return false;

  hostOut = hostPortStr.substr(0, colonPos);
  portOut = static_cast<uint16_t>(std::stoi(hostPortStr.substr(colonPos + 1)));
  return true;
}

int main(int argc, char** argv) {
  // Minimalus argumentų skaičius:
  if (argc < 4) {
    std::cerr << "Usage:\n"
              << "  client <leader_host> <leader_client_port> GET <k>\n"
              << "  client <leader_host> <leader_client_port> SET <k> <v>\n"
              << "  client <leader_host> <leader_client_port> DEL <k>\n"
              << "  client <leader_host> <leader_client_port> GETFF <k> <follower_host:port>\n"
              << "  client <leader_host> <leader_client_port> GETFB <k> <follower_host:port>\n";
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
  else if ((command == "SET" || command == "PUT") && argc >= 6) {
    return do_request_follow_redirect(
             leaderHost, leaderPort,
             "SET " + std::string(argv[4]) + " " + std::string(argv[5])
           ) ? 0 : 1;
  }
  // DEL – trina key per lyderį
  else if (command == "DEL" && argc >= 5) {
    return do_request_follow_redirect(
             leaderHost, leaderPort,
             "DEL " + std::string(argv[4])
           ) ? 0 : 1;
  }
  // GETFF – "forward": eik tiesiai į nurodytą follower host:port
  else if (command == "GETFF" && argc >= 6) {
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
  // GETFB – "fallback":
  //   1) Pirma bandom gauti iš followerio
  //   2) Jei followeris grąžina NOT_FOUND arba nepavyksta gauti atsakymo,
  //      tada kreipiamės į lyderį.
  else if (command == "GETFB" && argc >= 6) {
    std::string followerHost;
    uint16_t followerPort;
    if (!split_host_port(argv[5], followerHost, followerPort)) {
      std::cerr << "bad follower host:port\n";
      return 1;
    }

    // 1) Bandome jungtis prie followerio
    sock_t followerSocket = tcp_connect(followerHost, followerPort);
    if (followerSocket != NET_INVALID) {
      // Paprastas GET į followerį
      send_all(followerSocket, "GET " + std::string(argv[4]) + "\n");

      std::string followerReply;
      if (recv_line(followerSocket, followerReply)) {
        net_close(followerSocket);

        // Jei followeris sako "NOT_FOUND" -> bandysim lyderį
        if (followerReply == "NOT_FOUND") {
          return do_request_follow_redirect(
                   leaderHost, leaderPort,
                   "GET " + std::string(argv[4])
                 ) ? 0 : 1;
        }
        // Jei followeris grąžina REDIRECT – saugumo sumetimais tą redirectą sekam
        else if (followerReply.rfind("REDIRECT ", 0) == 0) {
          auto parts = split(trim(followerReply), ' ');
          if (parts.size() >= 3) {
            std::string redirectHost = parts[1];
            uint16_t redirectPort =
              static_cast<uint16_t>(std::stoi(parts[2]));

            return do_request_follow_redirect(
                     redirectHost, redirectPort,
                     "GET " + std::string(argv[4])
                   ) ? 0 : 1;
          }
        }
        // Bet koks kitas atsakymas – laikom sėkme ir tiesiog išspausdinam
        else {
          std::cout << followerReply << "\n";
          return 0;
        }
      } else {
        // Nepavyko perskaityt atsakymo iš followerio
        net_close(followerSocket);
      }
    }

    // 2) Fallback: jei nepavyko prisijungti arba gauti tinkamo atsakymo iš followerio,
    //    bandome tą patį GET per lyderį
    return do_request_follow_redirect(
             leaderHost, leaderPort,
             "GET " + std::string(argv[4])
           ) ? 0 : 1;
  }

  // Jei komanda neatitiko nė vieno iš aukščiau – blogi argumentai
  std::cerr << "Bad args. See usage above.\n";
  return 1;
}
