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
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #pragma comment(lib, "Ws2_32.lib")
  using sock_t = SOCKET;

  static inline bool net_init_once() {
    static bool inited = false;
    static std::mutex m;
    std::lock_guard<std::mutex> g(m);
    if (inited) return true;
    WSADATA w{};
    int r = WSAStartup(MAKEWORD(2,2), &w);
    inited = (r == 0);
    return inited;
  }
  static inline void net_close(sock_t s) { closesocket(s); }
  static constexpr sock_t NET_INVALID = INVALID_SOCKET;

#else
  #include <sys/types.h>
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <netdb.h>
  #include <unistd.h>
  using sock_t = int;

  static inline bool net_init_once() { return true; }
  static inline void net_close(sock_t s) { close(s); }
  static constexpr sock_t NET_INVALID = -1;
#endif

/* ===================== TCP helper functions ===================== */

static inline sock_t tcp_listen(uint16_t port, int backlog = 16) {
  net_init_once();

  sock_t s = socket(AF_INET, SOCK_STREAM, 0);
  if (s == NET_INVALID) return NET_INVALID;

  // allow fast rebind after restarts
  int yes = 1;
#ifdef _WIN32
  setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof(yes));
  #ifdef SO_REUSEPORT
    setsockopt(s, SOL_SOCKET, SO_REUSEPORT, (const char*)&yes, sizeof(yes));
  #endif
#else
  setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
  #ifdef SO_REUSEPORT
    setsockopt(s, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof(yes));
  #endif
#endif

  sockaddr_in addr{};
  addr.sin_family      = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);   // bind to 0.0.0.0
  addr.sin_port        = htons(port);

  if (bind(s, (sockaddr*)&addr, sizeof(addr)) < 0) {
    perror("bind");
    net_close(s);
    return NET_INVALID;
  }
  if (listen(s, backlog) < 0) {
    perror("listen");
    net_close(s);
    return NET_INVALID;
  }
  return s;
}

static inline sock_t tcp_accept(sock_t ls) {
  sockaddr_in caddr{};
#ifdef _WIN32
  int len = (int)sizeof(caddr);
#else
  socklen_t len = sizeof(caddr);
#endif
  sock_t cs = accept(ls, (sockaddr*)&caddr, &len);
  return cs;
}

static inline sock_t tcp_connect(const std::string& host, uint16_t port) {
  net_init_once();

  addrinfo hints{}, *res = nullptr;
  hints.ai_family   = AF_INET;
  hints.ai_socktype = SOCK_STREAM;

  std::string ps = std::to_string(port);
  if (getaddrinfo(host.c_str(), ps.c_str(), &hints, &res) != 0) {
    return NET_INVALID;
  }

  sock_t s = NET_INVALID;
  for (addrinfo* rp = res; rp != nullptr; rp = rp->ai_next) {
    s = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
    if (s == NET_INVALID) continue;
#ifdef _WIN32
    if (connect(s, rp->ai_addr, (int)rp->ai_addrlen) == 0) break;
#else
    if (connect(s, rp->ai_addr, rp->ai_addrlen) == 0) break;
#endif
    net_close(s);
    s = NET_INVALID;
  }
  freeaddrinfo(res);
  return s;
}

static inline bool send_all(sock_t s, const std::string& data) {
  const char* buf = data.data();
  size_t total = 0, len = data.size();
  while (total < len) {
#ifdef _WIN32
    int sent = send(s, buf + total, (int)(len - total), 0);
#else
    ssize_t sent = send(s, buf + total, len - total, 0);
#endif
    if (sent <= 0) return false;
    total += (size_t)sent;
  }
  return true;
}

static inline bool recv_line(sock_t s, std::string& out) {
  out.clear();
  char c;
  while (true) {
#ifdef _WIN32
    int r = recv(s, &c, 1, 0);
#else
    ssize_t r = recv(s, &c, 1, 0);
#endif
    if (r <= 0) return false;
    if (c == '\n') break;
    if (c != '\r') out.push_back(c);
  }
  return true;
}

// Utility helpers

static inline std::string trim(const std::string& s) {
  size_t b = 0, e = s.size();
  while (b < e && std::isspace((unsigned char)s[b])) ++b;
  while (e > b && std::isspace((unsigned char)s[e - 1])) --e;
  return s.substr(b, e - b);
}

static inline std::vector<std::string> split(const std::string& s, char delim) {
  std::vector<std::string> parts;
  std::stringstream ss(s);
  std::string item;
  while (std::getline(ss, item, delim)) parts.push_back(item);
  return parts;
}

// WAL definitions & I/O 

enum class Op : uint8_t { SET = 1, DEL = 2 };

struct WalEntry {
  uint64_t seq{};
  Op op{Op::SET};
  std::string key, value;   // value used only for SET
};

static inline std::string op_to_str(Op o) { return (o == Op::SET) ? "SET" : "DEL"; }
static inline Op          str_to_op(const std::string& s) { return (s == "DEL") ? Op::DEL : Op::SET; }

static inline void wal_append(std::ofstream& wal, const WalEntry& e) {
  wal << e.seq << '\t' << op_to_str(e.op) << '\t' << e.key << '\t' << e.value << '\n';
  wal.flush();
}

static inline bool wal_load(const std::string& path, std::vector<WalEntry>& out) {
  std::ifstream in(path);
  if (!in.good()) return false;

  std::string line;
  while (std::getline(in, line)) {
    if (line.empty()) continue;
    std::istringstream is(line);
    std::string seqs, ops;
    WalEntry e;
    if (!std::getline(is, seqs, '\t')) continue;
    if (!std::getline(is, ops,  '\t')) continue;
    std::getline(is, e.key,   '\t');
    std::getline(is, e.value);
    e.seq = std::stoull(seqs);
    e.op  = str_to_op(ops);
    out.push_back(std::move(e));
  }
  return true;
}
