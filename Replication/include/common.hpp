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
#include <iomanip>
#include "../../btree/include/logger.hpp"

// ---------- Logging ----------

// Log lygiai – naudojami žinutės „svarbumui“ pažymėti.
enum class LogLevel : uint8_t { DEBUG, INFO, WARN, ERROR };

// Paverčia LogLevel į trumpą tekstinę žymę, pvz. "DEBUG", "INFO ", ...
static inline const char* log_level_str(LogLevel lvl) {
    switch (lvl) {
      case LogLevel::DEBUG: return "DEBUG";
      case LogLevel::INFO:  return "INFO ";
      case LogLevel::WARN:  return "WARN ";
      case LogLevel::ERROR: return "ERROR";
    }
    return "UNKWN";
}

namespace Consts {
    // Time related
    static constexpr uint32_t MS_PER_SEC        = 1000;
    static constexpr int      LOG_MS_WIDTH      = 3;    // Width for milliseconds in timestamp
    static constexpr int      SLEEP_TIME_MS     = 15;

    // Networking
    static constexpr int      SOCKET_TIMEOUT_MS = 5000;
    static constexpr int      WINSOCK_VER_MAJOR = 2;
    static constexpr int      WINSOCK_VER_MINOR = 2;
    static constexpr int      LISTEN_BACKLOG    = 16;   // Default backlog for tcp_listen
    static constexpr int      NET_OPT_ENABLE    = 1;    // Value to enable socket options (setsockopt)
    static constexpr size_t   RECV_CHUNK_SIZE   = 1;    // Bytes to read at a time in recv_line

    static constexpr int      MAX_PORT_NUMBER = 65535;
}

// Sugeneruoja dabartinį laiko „timestamp“ (HH:MM:SS.mmm) – naudojamas log’ams.
static inline std::string now_ts() {
    using namespace std::chrono;
    auto now = system_clock::now();
    auto tt  = system_clock::to_time_t(now);
    auto ms  = duration_cast<milliseconds>(now.time_since_epoch()) % Consts::MS_PER_SEC;
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &tt);
#else
    localtime_r(&tt, &tm);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm, "%H:%M:%S") << '.'
        << std::setfill('0') << std::setw(3) << ms.count();
    return oss.str();
}

// Saugus log funkcijos „wrapper’is“ – su užraktu, kad keli thread’ai neliptų vienas ant kito.
static inline void log_line(LogLevel lvl, const std::string& msg) {
    static std::mutex log_mx;
    std::lock_guard<std::mutex> guard(log_mx);
    std::cerr << "[" << now_ts() << "][" << log_level_str(lvl) << "] "
              << msg << "\n";
}

// ---------- Net/platform ----------

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #pragma comment(lib, "Ws2_32.lib")
  using sock_t = SOCKET;

  // Vienkartinis Winsock inicializavimas (Windows). Kviečiamas prieš naudojant socket’us.
  static inline bool net_init_once() {
    static bool inited = false;
    static std::mutex initMutex;
    std::lock_guard<std::mutex> guard(initMutex);
    if (inited) return true;

    WSADATA wsaData{};
    int result = WSAStartup(MAKEWORD(2,2), &wsaData);
    inited = (result == 0);
    if (!inited) {
      log_line(LogLevel::ERROR, "WSAStartup failed: " + std::to_string(result));
    }
    return inited;
  }

  // Uždaro socket’ą Windows’e.
  static inline void net_close(sock_t socketHandle) { closesocket(socketHandle); }
  static constexpr sock_t NET_INVALID = INVALID_SOCKET;

#else
  #include <sys/types.h>
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <netdb.h>
  #include <unistd.h>
  using sock_t = int;

  // Unix sistemoms papildomas init nereikalingas – tiesiog grąžina true.
  static inline bool net_init_once() { return true; }

  // Uždaro socket’ą Unix’e.
  static inline void net_close(sock_t socketHandle) { close(socketHandle); }
  static constexpr sock_t NET_INVALID = -1;
#endif

// Bendra pagalbinė funkcija uždėti recv/send timeout’ą ms visose platformose.
static inline void set_socket_timeouts(sock_t sock, int timeout_ms) {
  if (sock == NET_INVALID || timeout_ms <= 0) {
    return;
  }
#ifdef _WIN32
  int tv = timeout_ms;
  setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
  setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof(tv));
#else
  struct timeval tv{};
  tv.tv_sec  = timeout_ms / Consts::MS_PER_SEC;
  tv.tv_usec = (timeout_ms % Consts::MS_PER_SEC) * Consts::MS_PER_SEC;
  setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif
}

/* ===================== TCP helper functions ===================== */

// Sukuria TCP „listen“ socket’ą ant duoto porto ir pradeda klausytis jungčių.
static inline sock_t tcp_listen(uint16_t port, int backlog = Consts::LISTEN_BACKLOG) {
  if (!net_init_once()) {
    return NET_INVALID;
  }

  sock_t listenSock = socket(AF_INET, SOCK_STREAM, 0);
  if (listenSock == NET_INVALID) {
    log_line(LogLevel::ERROR, "socket() failed in tcp_listen");
    return NET_INVALID;
  }

  int reuseFlag = Consts::NET_OPT_ENABLE;
#ifdef _WIN32
  setsockopt(listenSock, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuseFlag, sizeof(reuseFlag));
#else
  setsockopt(listenSock, SOL_SOCKET, SO_REUSEADDR, &reuseFlag, sizeof(reuseFlag));
#endif

  sockaddr_in address{};
  address.sin_family      = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_ANY);  // klausom visų interfeisų
  address.sin_port        = htons(port);

  // Priededam socket’ą prie porto
  if (bind(listenSock, (sockaddr*)&address, sizeof(address)) < 0) {
    perror("bind");
    log_line(LogLevel::ERROR, "bind() failed on port " + std::to_string(port));
    net_close(listenSock);
    return NET_INVALID;
  }

  // Pradedam listen’inti
  if (listen(listenSock, backlog) < 0) {
    perror("listen");
    log_line(LogLevel::ERROR, "listen() failed on port " + std::to_string(port));
    net_close(listenSock);
    return NET_INVALID;
  }
  return listenSock;
}

// Priima vieną naują kliento jungtį ir grąžina kliento socket’ą (jau su timeout’ais).
static inline sock_t tcp_accept(sock_t listenSock) {
  sockaddr_in clientAddress{};
#ifdef _WIN32
  int addrLen = (int)sizeof(clientAddress);
#else
  socklen_t addrLen = sizeof(clientAddress);
#endif
  sock_t clientSock = accept(listenSock, (sockaddr*)&clientAddress, &addrLen);
  if (clientSock != NET_INVALID) {
    set_socket_timeouts(clientSock, Consts::SOCKET_TIMEOUT_MS); // 5s recv/send timeout
  }
  return clientSock;
}

// Ustanodo TCP ryšį su duotu host:port ir grąžina socket’ą, jei pavyksta.
static inline sock_t tcp_connect(const std::string& host, uint16_t port) {
  net_init_once();

  addrinfo hints{};
  addrinfo *result = nullptr;
  hints.ai_family   = AF_INET;
  hints.ai_socktype = SOCK_STREAM;

  std::string portStr = std::to_string(port);

  // Išsprendžiam host vardą į IP ir gaunam galimus adresus.
  if (getaddrinfo(host.c_str(), portStr.c_str(), &hints, &result) != 0) {
    // ČIA BUVO LOG_WARN – praleista, kad netriukšmautų.
    return NET_INVALID;
  }

  sock_t clientSock = NET_INVALID;

  // Bandom prisijungti prie bet kurio iš gautų adresų
  for (addrinfo* rp = result; rp != nullptr; rp = rp->ai_next) {
    clientSock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
    if (clientSock == NET_INVALID) {
      continue;
    }

#ifdef _WIN32
    if (connect(clientSock, rp->ai_addr, (int)rp->ai_addrlen) == 0) break;
#else
    if (connect(clientSock, rp->ai_addr, rp->ai_addrlen) == 0) {
      break;
    }
#endif

    // Jei prisijungti nepavyko – uždarom ir bandom kitą adresą.
    net_close(clientSock);
    clientSock = NET_INVALID;
  }

  freeaddrinfo(result);
  return clientSock;
}

// Išsiunčia visą duotą „data“ string’ą per socket’ą (užtikrina, kad išsiųsta iki galo).
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
    if (sent <= 0) {
      // Klaida arba ryšys nutrauktas.
      return false;
    }
    totalSent += (size_t)sent;
  }
  return true;
}

// Perskaito vieną tekstinę eilutę iš socket’o iki '\n' (be '\r\n'), patalpina į out.
static inline bool recv_line(sock_t socketHandle, std::string& out) {
  out.clear();
  char ch;
  while (true) {
#ifdef _WIN32
    int received = recv(socketHandle, &ch, 1, 0);
#else
    ssize_t received = recv(socketHandle, &ch, 1, 0);
#endif
    if (received <= 0) {
      // Klaida ar nutrūkęs ryšys.
      return false;
    }

    if (ch == '\n') {
      break;      // eilutės pabaiga
    }
    if (ch != '\r') {
      out.push_back(ch); // ignoruojam '\r'
    }
  }
  return true;
}

/* ===================== Utility helpers (string) ===================== */

// Nukerpa tarpus pradžioje ir pabaigoje nuo string’o.
static inline std::string trim(const std::string& s) {
  size_t begin = 0;
  size_t end   = s.size();

  while (begin < end && (std::isspace((unsigned char)s[begin]) != 0)) {
    ++begin;
  }

  while (end > begin && (std::isspace((unsigned char)s[end - 1]) != 0)) {
    --end;
  }

  return s.substr(begin, end - begin);
}

// Suskaldo string’ą pagal vieną simbolį (delim) į vektorių dalių.
static inline std::vector<std::string> split(const std::string& string, char delim) {
  std::vector<std::string> parts;
  std::stringstream iss(string);
  std::string item;
  while (std::getline(iss, item, delim)) {
    parts.push_back(item);
  }
  return parts;
}

/**
 * Format value with length prefix for sending over protocol
 * Example: "Hello World" -> "11 Hello World"
 */
static inline std::string format_length_prefixed_value(const std::string& value) {
  return std::to_string(value.length()) + " " + value;
}

/**
 * Parse length-prefixed value from token stream
 * Handles values that may span multiple recv() calls or contain spaces
 *
 * @param tokens - Tokenized command (e.g., ["SET", "key", "11", "Hello", "World"])
 * @param start_idx - Index where value_len starts (e.g., 2 for SET command)
 * @param socket - Socket to read remaining bytes if needed
 * @param out_value - Output parameter for parsed value
 * @return true if successfully parsed, false otherwise
 */
static inline bool parse_length_prefixed_value(
    const std::vector<std::string>& tokens,
    size_t start_idx,
    sock_t socket,
    std::string& out_value
) {
  if (start_idx >= tokens.size()) {
    log_line(LogLevel::WARN, "parse_length_prefixed_value: start_idx out of bounds");
    return false;
  }

  // Parse value length
  size_t value_len = 0;
  try {
    value_len = std::stoull(tokens[start_idx]);
  } catch (...) {
    log_line(LogLevel::WARN, "Invalid value_len: " + tokens[start_idx]);
    return false;
  }

  // Reconstruct value from remaining tokens
  std::string reconstructed;
  for (size_t i = start_idx + 1; i < tokens.size(); i++) {
    if (i > start_idx + 1) reconstructed += " ";
    reconstructed += tokens[i];
  }

  // If we already have the complete value, return it
  if (reconstructed.length() == value_len) {
    out_value = reconstructed;
    return true;
  }

  // If we have more than expected, something is wrong
  if (reconstructed.length() > value_len) {
    out_value = reconstructed.substr(0, value_len);
    return true;
  }

  // Need more bytes from socket (value was split across recv() calls or contains newlines)
  // Note: recv_line() strips the \n, so we need to account for it if value contains newlines
  size_t bytes_needed = value_len - reconstructed.length();
  out_value = reconstructed;

  // If reconstructed has content but we need more, add back the newline that recv_line() stripped
  if (bytes_needed > 0 && reconstructed.length() > 0) {
    out_value += "\n";
    bytes_needed -= 1;
  }

  if (bytes_needed > 0 && socket != NET_INVALID) {
    std::vector<char> buffer(bytes_needed);
    size_t total_read = 0;

    while (total_read < bytes_needed) {
#ifdef _WIN32
      int received = recv(socket, buffer.data() + total_read,
                          (int)(bytes_needed - total_read), 0);
#else
      ssize_t received = recv(socket, buffer.data() + total_read,
                             bytes_needed - total_read, 0);
#endif
      if (received <= 0) {
        log_line(LogLevel::ERROR, "Failed to read remaining value bytes");
        return false;
      }
      total_read += received;
    }
    out_value.append(buffer.data(), bytes_needed);
  }

  return out_value.length() == value_len;
}

/* ===================== WAL definitions & I/O ===================== */

// Paverčia Op į tekstą ("SET" arba "DEL").
static inline std::string op_to_str(WalOperation walOperation) {
  return (walOperation == WalOperation::SET) ? "SET" : "DEL";
}

// Paverčia tekstą atgal į Op (ne DEL laikome SET).
static inline WalOperation str_to_op(const std::string& opStr) {
  return (opStr == "DELETE") ? WalOperation::DELETE : WalOperation::SET;
}

// Prideda vieną įrašą į atidarytą WAL failą (tekstu, tab’ais atskyrus laukus).
static inline void wal_append(std::ofstream& walStream, const WalRecord& walRecord) {
  walStream << walRecord.lsn << '\t'
            << op_to_str(walRecord.operation) << '\t'
            << walRecord.key << '\t'
            << walRecord.value << '\n';
  walStream.flush(); // iškart išrašome – saugiau (mažiau praradimų avarijos atveju)
}

// Perskaito visą WAL failą iš 'path' į outEntries vektorių.
// Grąžina true, jei failą pavyko atidaryti ir nuskaitėm (net jei eilės buvo blogos).
static inline bool wal_load(const std::string& path, std::vector<WalRecord>& outEntries) {
  std::ifstream in(path);
  if (!in.good()) {
    return false; // jei failo nėra – tiesiog false.
  }

  std::string line;
  while (std::getline(in, line)) {
    if (line.empty()) {
      continue;
    }

    std::istringstream lineStream(line);
    std::string seqStr;
    std::string opStr;
    WalRecord walRecord;

    // Eilės formatas: seq \t op \t key \t value
    if (!std::getline(lineStream, seqStr, '\t')) {
      continue;
    }
    if (!std::getline(lineStream, opStr,  '\t')) {
      continue;
    }
    std::getline(lineStream, walRecord.key,   '\t');
    std::getline(lineStream, walRecord.value);

    try {
      walRecord.lsn = std::stoull(seqStr);
    } catch (...) {
      // Jei sekos numeris sugadintas – praleidžiam eilutę.
      log_line(LogLevel::WARN, "Bad seq in WAL line: " + line);
      continue;
    }

    walRecord.operation  = str_to_op(opStr);
    outEntries.push_back(std::move(walRecord));
  }
  return true;
}
