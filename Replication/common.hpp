#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <thread>
#include <mutex>
#include <atomic>
#include <fstream>
#include <sstream>
#include <iostream>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <cctype>
#include <cstdio>
#include <cerrno>
#include <algorithm>

#ifdef _WIN32
  // Windows socket API
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #pragma comment(lib, "Ws2_32.lib")
  using sock_t = SOCKET;

  // Vienkartinė (lazy) Winsock inicializacija
  static inline bool net_init_once() {
    static bool inited = false;
    static std::mutex initMutex;
    std::lock_guard<std::mutex> guard(initMutex);
    if (inited) return true;

    WSADATA wsaData{};
    int result = WSAStartup(MAKEWORD(2,2), &wsaData);
    inited = (result == 0);
    return inited;
  }

  // Bendras socket uždarymo alias
  static inline void net_close(sock_t socketHandle) { closesocket(socketHandle); }

  // „neteisingas“ / nepavykęs socket handle
  static constexpr sock_t NET_INVALID = INVALID_SOCKET;

#else
  // POSIX / Linux socket API
  #include <sys/types.h>
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <netdb.h>
  #include <unistd.h>
  using sock_t = int;

  // Linux atveju atskiros inicializacijos nereikia
  static inline bool net_init_once() { return true; }

  // Bendras socket uždarymo alias
  static inline void net_close(sock_t socketHandle) { close(socketHandle); }

  // „neteisingas“ / nepavykęs socket handle
  static constexpr sock_t NET_INVALID = -1;
#endif

/* ===================== TCP helper functions ===================== */

// Sukuria serverio socket'ą, kuris klausosi nurodyto porto
static inline sock_t tcp_listen(uint16_t port, int backlog = 16) {
  net_init_once();

  // Sukuriam TCP (SOCK_STREAM) socket'ą
  sock_t listenSock = socket(AF_INET, SOCK_STREAM, 0);
  if (listenSock == NET_INVALID) return NET_INVALID;

  // Leidžia greitai rebind'inti portą po restartų (nenorim „Address already in use“)
  int reuseFlag = 1;
#ifdef _WIN32
  setsockopt(listenSock, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuseFlag, sizeof(reuseFlag));
  #ifdef SO_REUSEPORT
    setsockopt(listenSock, SOL_SOCKET, SO_REUSEPORT, (const char*)&reuseFlag, sizeof(reuseFlag));
  #endif
#else
  setsockopt(listenSock, SOL_SOCKET, SO_REUSEADDR, &reuseFlag, sizeof(reuseFlag));
  #ifdef SO_REUSEPORT
    setsockopt(listenSock, SOL_SOCKET, SO_REUSEPORT, &reuseFlag, sizeof(reuseFlag));
  #endif
#endif

  // Bind'inam prie 0.0.0.0:<port> – priimam ryšius iš visų interfeisų
  sockaddr_in address{};
  address.sin_family      = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_ANY);
  address.sin_port        = htons(port);

  if (bind(listenSock, (sockaddr*)&address, sizeof(address)) < 0) {
    perror("bind");
    net_close(listenSock);
    return NET_INVALID;
  }

  // Pradedam klausytis
  if (listen(listenSock, backlog) < 0) {
    perror("listen");
    net_close(listenSock);
    return NET_INVALID;
  }
  return listenSock;
}

// Priima vieną kliento prisijungimą (blokuoja iki connect)
static inline sock_t tcp_accept(sock_t listenSock) {
  sockaddr_in clientAddress{};
#ifdef _WIN32
  int addrLen = (int)sizeof(clientAddress);
#else
  socklen_t addrLen = sizeof(clientAddress);
#endif
  sock_t clientSock = accept(listenSock, (sockaddr*)&clientAddress, &addrLen);
  return clientSock;
}

// Prisijungia prie <host>:<port> kaip klientas
static inline sock_t tcp_connect(const std::string& host, uint16_t port) {
  net_init_once();

  addrinfo hints{}, *result = nullptr;
  hints.ai_family   = AF_INET;
  hints.ai_socktype = SOCK_STREAM;

  std::string portStr = std::to_string(port);

  // DNS/host rezoliucija
  if (getaddrinfo(host.c_str(), portStr.c_str(), &hints, &result) != 0) {
    return NET_INVALID;
  }

  sock_t clientSock = NET_INVALID;

  // Bandom per visus gautus adresus
  for (addrinfo* rp = result; rp != nullptr; rp = rp->ai_next) {
    clientSock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
    if (clientSock == NET_INVALID) continue;

#ifdef _WIN32
    if (connect(clientSock, rp->ai_addr, (int)rp->ai_addrlen) == 0) break;
#else
    if (connect(clientSock, rp->ai_addr, rp->ai_addrlen) == 0) break;
#endif
    // jei nepavyko – uždarom ir bandome kitą
    net_close(clientSock);
    clientSock = NET_INVALID;
  }

  freeaddrinfo(result);
  return clientSock;
}

// Išsiunčia visą string'ą (kol išsiųsta visi baitai arba klaida)
static inline bool send_all(sock_t socketHandle, const std::string& data) {
  const char* buffer = data.data();
  size_t totalSent   = 0;
  size_t dataLen     = data.size();

  while (totalSent < dataLen) {
#ifdef _WIN32
    int sent = send(socketHandle, buffer + totalSent, (int)(dataLen - totalSent), 0);
#else
    ssize_t sent = send(socketHandle, buffer + totalSent, dataLen - totalSent, 0);
#endif
    if (sent <= 0) return false; // klaida arba sujungimas nutrūko
    totalSent += (size_t)sent;
  }
  return true;
}

// Perskaito vieną eilutę iki '\n' (grąžina false, jei ryšys uždarytas / klaida)
static inline bool recv_line(sock_t socketHandle, std::string& out) {
  out.clear();
  char ch;
  while (true) {
#ifdef _WIN32
    int received = recv(socketHandle, &ch, 1, 0);
#else
    ssize_t received = recv(socketHandle, &ch, 1, 0);
#endif
    if (received <= 0) return false; // nutrūko ryšys arba klaida

    if (ch == '\n') break;           // baigėsi eilutė
    if (ch != '\r') out.push_back(ch); // ignoruojam '\r' (Windows CRLF)
  }
  return true;
}

/* ===================== Utility helpers (string) ===================== */

// Nukerpa whitespace pradžioje ir pabaigoje
static inline std::string trim(const std::string& s) {
  size_t begin = 0;
  size_t end   = s.size();

  while (begin < end && std::isspace((unsigned char)s[begin])) ++begin;
  while (end > begin && std::isspace((unsigned char)s[end - 1])) --end;

  return s.substr(begin, end - begin);
}

// Padalina string'ą per nurodytą simbolį (delim)
static inline std::vector<std::string> split(const std::string& s, char delim) {
  std::vector<std::string> parts;
  std::stringstream ss(s);
  std::string item;
  while (std::getline(ss, item, delim)) parts.push_back(item);
  return parts;
}

/* ===================== WAL definitions & I/O ===================== */

// Operacijos tipas: SET arba DEL
enum class Op : uint8_t { SET = 1, DEL = 2 };

// Vienas WAL įrašas
struct WalEntry {
  uint64_t seq{};           // sekos numeris (didėjantis)
  Op op{Op::SET};           // operacijos tipas (SET/DEL)
  std::string key, value;   // key ir value; value naudojamas tik SET operacijai
};

// Konvertuoja enum -> string (rašymui į failą)
static inline std::string op_to_str(Op operation) {
  return (operation == Op::SET) ? "SET" : "DEL";
}

// Konvertuoja string -> enum (skaitymui iš failo)
static inline Op str_to_op(const std::string& opStr) {
  return (opStr == "DEL") ? Op::DEL : Op::SET;
}

// Prideda vieną įrašą į WAL failą (tekstu, viena eilute)
static inline void wal_append(std::ofstream& walStream, const WalEntry& entry) {
  // Formatas: seq \t OP \t key \t value\n
  walStream << entry.seq << '\t'
            << op_to_str(entry.op) << '\t'
            << entry.key << '\t'
            << entry.value << '\n';

  walStream.flush(); // užtikrinam, kad iškart nueitų į diską (durability)
}

// Perskaito visą WAL failą į atmintį (vektorių)
static inline bool wal_load(const std::string& path, std::vector<WalEntry>& outEntries) {
  std::ifstream in(path);
  if (!in.good()) return false;   // jei failo nėra – laikom, kad nėra ankstesnių įrašų

  std::string line;
  while (std::getline(in, line)) {
    if (line.empty()) continue;

    std::istringstream lineStream(line);
    std::string seqStr, opStr;
    WalEntry entry;

    // seq ir op yra atskirti tab'ais
    if (!std::getline(lineStream, seqStr, '\t')) continue;
    if (!std::getline(lineStream, opStr,  '\t')) continue;
    std::getline(lineStream, entry.key,   '\t');
    std::getline(lineStream, entry.value);

    entry.seq = std::stoull(seqStr);   // sekos numeris
    entry.op  = str_to_op(opStr);      // SET/DEL

    outEntries.push_back(std::move(entry));
  }
  return true;
}
