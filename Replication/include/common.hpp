#pragma once
#include <string>
#include <vector>
#include <mutex>
#include <fstream>
#include <sstream>
#include <iostream>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <cctype>
#include <cstdio>
#include <cerrno>
#include <iomanip>
#include <fcntl.h>
#include <sys/select.h>
#include "../../btree/include/logger.hpp"

using std::string;
using std::vector;

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
    static constexpr int      LISTEN_BACKLOG    = 16;   // Default backlog for tcp_listen
    static constexpr int      NET_OPT_ENABLE    = 1;    // Value to enable socket options (setsockopt)
    static constexpr size_t   RECV_CHUNK_SIZE   = 1;    // Bytes to read at a time in recv_line
    static constexpr int      BIND_READONLY_RETRIES = 35;

    static constexpr int      MAX_PORT_NUMBER = 65535;
}

// Returns current time in milliseconds since epoch (for timing/caching).
static inline uint64_t now_ms() {
    using namespace std::chrono;
    return static_cast<uint64_t>(
        duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count()
    );
}

// Sugeneruoja dabartinį laiko „timestamp" (HH:MM:SS.mmm) – naudojamas log'ams.
static inline string now_ts() {
    using namespace std::chrono;
    auto now = system_clock::now();
    auto tt  = system_clock::to_time_t(now);
    auto ms  = duration_cast<milliseconds>(now.time_since_epoch()) % Consts::MS_PER_SEC;
    std::tm tm{};
    localtime_r(&tt, &tm);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%H:%M:%S") << '.'
        << std::setfill('0') << std::setw(3) << ms.count();
    return oss.str();
}

// Saugus log funkcijos „wrapper’is“ – su užraktu, kad keli thread’ai neliptų vienas ant kito.
static inline void log_line(LogLevel lvl, const string& msg) {
    static std::mutex log_mx;
    std::lock_guard<std::mutex> guard(log_mx);
    std::cerr << "[" << now_ts() << "][" << log_level_str(lvl) << "] "
              << msg << "\n";
}


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

// Bendra pagalbinė funkcija uždėti recv/send timeout’ą ms visose platformose.
static inline void set_socket_timeouts(sock_t sock, int timeout_ms) {
  if (sock == NET_INVALID || timeout_ms <= 0) {
    return;
  }

  struct timeval tv{};
  tv.tv_sec  = timeout_ms / Consts::MS_PER_SEC;
  tv.tv_usec = (timeout_ms % Consts::MS_PER_SEC) * Consts::MS_PER_SEC;
  setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
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
  // 1. SO_REUSEADDR: Allows reuse of local addresses in TIME_WAIT
  if (setsockopt(listenSock, SOL_SOCKET, SO_REUSEADDR, &reuseFlag, sizeof(reuseFlag)) < 0) {
    log_line(LogLevel::WARN, "Failed to set SO_REUSEADDR on port " + std::to_string(port));
  }

  // 2. SO_REUSEPORT: Labai svarbu, kad būtų fast restart'as.
    #ifdef SO_REUSEPORT
    if (setsockopt(listenSock, SOL_SOCKET, SO_REUSEPORT, &reuseFlag, sizeof(reuseFlag)) < 0) {
        log_line(LogLevel::WARN, "Failed to set SO_REUSEPORT on port " + std::to_string(port));
    }
    #endif

    struct linger lin;
    lin.l_onoff = 1;
    lin.l_linger = 0;
    setsockopt(listenSock, SOL_SOCKET, SO_LINGER, &lin, sizeof(lin));

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
  socklen_t addrLen = sizeof(clientAddress);
  sock_t clientSock = accept(listenSock, (sockaddr*)&clientAddress, &addrLen);
  if (clientSock != NET_INVALID) {
    set_socket_timeouts(clientSock, Consts::SOCKET_TIMEOUT_MS); // Consts::SOCKET_TIMEOUT_MS recv/send timeout
  }
  return clientSock;
}

// Ustanodo TCP ryšį su duotu host:port ir grąžina socket’ą, jei pavyksta.
static inline sock_t tcp_connect(const string& host, uint16_t port, int timeout_ms = 2000) {
  net_init_once();

  addrinfo hints{};
  addrinfo *result = nullptr;
  hints.ai_family   = AF_INET;
  hints.ai_socktype = SOCK_STREAM;

  string portStr = std::to_string(port);

  // Išsprendžiam host vardą į IP ir gaunam galimus adresus.
  if (getaddrinfo(host.c_str(), portStr.c_str(), &hints, &result) != 0) {
    return NET_INVALID;
  }

  sock_t clientSock = NET_INVALID;

  // Bandom prisijungti prie bet kurio iš gautų adresų
  for (addrinfo* rp = result; rp != nullptr; rp = rp->ai_next) {
    clientSock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
    if (clientSock == NET_INVALID) {
      continue;
    }

    // Set socket to non-blocking mode for connect timeout
    int flags = fcntl(clientSock, F_GETFL, 0);
    fcntl(clientSock, F_SETFL, flags | O_NONBLOCK);

    // Attempt connection
    int conn_result = connect(clientSock, rp->ai_addr, rp->ai_addrlen);

    if (conn_result == 0) {
      // Immediate success - restore blocking mode
      fcntl(clientSock, F_SETFL, flags);
      break;
    }

    if (errno == EINPROGRESS) {
      // Connection in progress - wait with timeout
      fd_set write_fds;
      FD_ZERO(&write_fds);
      FD_SET(clientSock, &write_fds);

      struct timeval tv;
      tv.tv_sec = timeout_ms / 1000;
      tv.tv_usec = (timeout_ms % 1000) * 1000;

      int sel_result = select(clientSock + 1, nullptr, &write_fds, nullptr, &tv);

      if (sel_result > 0) {
        // Check if connection succeeded
        int error = 0;
        socklen_t len = sizeof(error);
        getsockopt(clientSock, SOL_SOCKET, SO_ERROR, (char*)&error, &len);

        if (error == 0) {
          // Success - restore blocking mode
          fcntl(clientSock, F_SETFL, flags);
          break;
        }
      }
    }

    // Connection failed or timed out - close and try next address
    net_close(clientSock);
    clientSock = NET_INVALID;
  }

  freeaddrinfo(result);
  return clientSock;
}

// Išsiunčia visą duotą „data“ string’ą per socket’ą (užtikrina, kad išsiųsta iki galo).
static inline bool send_all(sock_t socketHandle, const string& data) {
  const char* buffer = data.data();
  size_t totalSent   = 0;
  size_t dataLen     = data.size();

  while (totalSent < dataLen) {
    ssize_t sent = send(socketHandle, buffer + totalSent, dataLen - totalSent, 0);
    if (sent <= 0) {
      // Klaida arba ryšys nutrauktas.
      return false;
    }
    totalSent += (size_t)sent;
  }
  return true;
}

// Perskaito vieną tekstinę eilutę iš socket’o iki '\n' (be '\r\n'), patalpina į out.
static inline bool recv_line(sock_t socketHandle, string& out) {
  out.clear();
  char ch;
  while (true) {
    ssize_t received = recv(socketHandle, &ch, 1, 0);
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
static inline string trim(const string& str) {
  size_t begin = 0;
  size_t end   = str.size();

  while (begin < end && (std::isspace((unsigned char)str[begin]) != 0)) {
    ++begin;
  }

  while (end > begin && (std::isspace((unsigned char)str[end - 1]) != 0)) {
    --end;
  }

  return str.substr(begin, end - begin);
}

// Suskaldo string’ą pagal vieną simbolį (delim) į vektorių dalių.
static inline vector<string> split(const string& str, char delim) {
  vector<string> parts;
  std::stringstream iss(str);
  string item;
  while (std::getline(iss, item, delim)) {
    parts.push_back(item);
  }
  return parts;
}

/**
 * Format value with length prefix for sending over protocol
 * Example: "Hello World" -> "11 Hello World"
 */
static inline string format_length_prefixed_value(const string& value) {
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
    const vector<string>& tokens,
    size_t start_idx,
    sock_t socket,
    string& out_value
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
  string reconstructed;
  for (size_t i = start_idx + 1; i < tokens.size(); i++) {
    if (i > start_idx + 1) {
      reconstructed += " ";
    }
    reconstructed += tokens[i];
  }

  // Jeigu jau turim pilną reikšmę, tai iš karto galima grąžinti.
  if (reconstructed.length() == value_len) {
    out_value = reconstructed;
    return true;
  }

  // Jeigu turime daugiau nei tikėjomes kažkas įvyko negerai:(
  if (reconstructed.length() > value_len) {
    out_value = reconstructed.substr(0, value_len);
    return true;
  }

  size_t bytes_needed = value_len - reconstructed.length();
  out_value = reconstructed;

  // If reconstructed has content but we need more, add back the newline that recv_line() stripped
  if (bytes_needed > 0 && reconstructed.length() > 0) {
    out_value += "\n";
    bytes_needed -= 1;
  }

  if (bytes_needed > 0 && socket != NET_INVALID) {
    vector<char> buffer(bytes_needed);
    size_t total_read = 0;

    while (total_read < bytes_needed) {
      ssize_t received = recv(socket, buffer.data() + total_read,
                             bytes_needed - total_read, 0);
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
