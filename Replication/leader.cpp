#include "common.hpp"

// Informacija apie vieną follower'io ryšį leader'yje.
struct FollowerConn {
  sock_t   follower_socket{NET_INVALID};  // TCP jungtis su follower'iu
  uint64_t acked_upto_seq{0};             // iki kokio seq follower'is jau atsiuntė ACK
  bool     is_alive{true};                // ar ryšys laikomas gyvu
};

// Bendras leader'io globalus stovis.
static std::mutex mtx;
static std::condition_variable cv;
static std::unordered_map<std::string,std::string> kv;          // aktualus key-value stovis
static std::vector<WalEntry> logbuf;                            // WAL įrašų istorija atmintyje
static std::vector<std::shared_ptr<FollowerConn>> followers;    // žinomi follower'iai
static std::atomic<uint64_t> next_seq{1};                       // sekantis WAL sekos numeris
static int REQUIRED_ACKS = 0;                                   // kiek follower'ių ACK reikalaujama

// Išsiunčia vieną WAL įrašą visiems aktyviems follower'iams.
static void broadcast_entry(const WalEntry& entry) {
  std::string message;
  if (entry.op == Op::SET)
    message = "WRITE "  + std::to_string(entry.seq) + " " + entry.key + " " + entry.value + "\n";
  else
    message = "DELETE " + std::to_string(entry.seq) + " " + entry.key + "\n";

  for (auto& follower : followers) {
    if (!follower->is_alive) continue;
    if (!send_all(follower->follower_socket, message))
      follower->is_alive = false;  // jei siuntimas nepavyko – ryšį laikome mirusiu
  }
}

// Gija, kuri aptarnauja vieną follower'į:
// - priima HELLO <last_seq>
// - persiunčia backlog'ą nuo last_seq
// - priima ACK <seq> ir atnaujina follower'io acked_upto_seq.
static void follower_thread(std::shared_ptr<FollowerConn> follower) {
  std::string hello_line;
  if (!recv_line(follower->follower_socket, hello_line)) {
    follower->is_alive = false;
    return;
  }

  auto hello_parts = split(hello_line, ' ');
  if (hello_parts.size() != 2 || hello_parts[0] != "HELLO") {
    follower->is_alive = false;
    return;
  }

  uint64_t last_applied_seq = std::stoull(hello_parts[1]);

  {
    // Išsiunčiam visus WAL įrašus, kurių seq > follower'io turimo seq.
    std::unique_lock<std::mutex> lock(mtx);
    for (const auto& entry : logbuf) {
      if (entry.seq > last_applied_seq) {
        std::string message = (entry.op == Op::SET)
          ? ("WRITE "  + std::to_string(entry.seq) + " " + entry.key + " " + entry.value + "\n")
          : ("DELETE " + std::to_string(entry.seq) + " " + entry.key + "\n");
        if (!send_all(follower->follower_socket, message)) {
          follower->is_alive = false;
          return;
        }
      }
    }
  }

  // Toliau laukiame ACK pranešimų.
  while (follower->is_alive) {
    std::string recv_line_str;
    if (!recv_line(follower->follower_socket, recv_line_str)) {
      follower->is_alive = false;
      break;
    }

    auto tokens = split(recv_line_str, ' ');
    if (tokens.size() == 2 && tokens[0] == "ACK") {
      uint64_t ack_seq = std::stoull(tokens[1]);
      follower->acked_upto_seq = std::max(follower->acked_upto_seq, ack_seq);
      cv.notify_all();  // pažadinam lyderį, jei jis laukia ACK'ų
    }
  }
}

// Suskaičiuoja, kiek follower'ių yra gyvi ir turi ACK >= seq.
static size_t count_acks(uint64_t seq) {
  size_t acked_count = 0;
  for (auto& follower : followers) {
    if (follower->is_alive && follower->acked_upto_seq >= seq)
      ++acked_count;
  }
  return acked_count;
}

// Klausosi naujų follower'ių ir kiekvienam startuoja follower_thread.
static void accept_followers(uint16_t follower_port) {
  sock_t listen_socket = tcp_listen(follower_port);
  if (listen_socket == NET_INVALID) {
    std::cerr << "Follower listen failed\n";
    return;
  }

  std::cout << "Leader: listening followers on " << follower_port << "\n";

  while (true) {
    sock_t follower_socket = tcp_accept(listen_socket);
    if (follower_socket == NET_INVALID) continue;

    auto follower_conn = std::make_shared<FollowerConn>();
    follower_conn->follower_socket = follower_socket;

    {
      std::lock_guard<std::mutex> lock(mtx);
      followers.push_back(follower_conn);
    }

    std::thread(follower_thread, follower_conn).detach();
  }
}

// Aptarnauja klientus (SET/PUT/GET/DEL) ir užtikrina rašymą į follower'ius.
static void serve_clients(uint16_t client_port, const std::string& wal_path) {
  sock_t listen_socket = tcp_listen(client_port);
  if (listen_socket == NET_INVALID) {
    std::cerr << "Client listen failed\n";
    return;
  }

  std::ofstream wal_stream(wal_path, std::ios::app);
  std::cout << "Leader: listening clients on " << client_port << "\n";

  while (true) {
    sock_t client_socket = tcp_accept(listen_socket);
    if (client_socket == NET_INVALID) continue;

    // Kiekvienam klientui paleidžiame atskirą giją.
    std::thread([client_socket, &wal_stream]() {
      std::string request_line;

      while (recv_line(client_socket, request_line)) {
        auto tokens = split(trim(request_line), ' ');
        if (tokens.empty()) continue;

        // SET/PUT <key> <value>
        if ((tokens[0] == "PUT" || tokens[0] == "SET") && tokens.size() >= 3) {
          WalEntry entry;

          {
            std::lock_guard<std::mutex> lock(mtx);

            entry.seq   = next_seq++;
            entry.op    = Op::SET;
            entry.key   = tokens[1];
            entry.value = tokens[2];

            kv[entry.key] = entry.value;
            logbuf.push_back(entry);
            wal_append(wal_stream, entry);
            broadcast_entry(entry);
          }

          // Jei reikia – laukiame, kol pakankamas kiekis follower'ių patvirtins šį seq.
          if (REQUIRED_ACKS > 0) {
            std::unique_lock<std::mutex> lock(mtx);
            cv.wait_for(
              lock,
              std::chrono::seconds(3),
              [&] { return count_acks(entry.seq) >= static_cast<size_t>(REQUIRED_ACKS); }
            );
          }

          send_all(client_socket, "OK " + std::to_string(entry.seq) + "\n");
        }
        // DEL <key>
        else if (tokens[0] == "DEL" && tokens.size() >= 2) {
          WalEntry entry;

          {
            std::lock_guard<std::mutex> lock(mtx);

            entry.seq = next_seq++;
            entry.op  = Op::DEL;
            entry.key = tokens[1];

            kv.erase(entry.key);
            logbuf.push_back(entry);
            wal_append(wal_stream, entry);
            broadcast_entry(entry);
          }

          if (REQUIRED_ACKS > 0) {
            std::unique_lock<std::mutex> lock(mtx);
            cv.wait_for(
              lock,
              std::chrono::seconds(3),
              [&] { return count_acks(entry.seq) >= static_cast<size_t>(REQUIRED_ACKS); }
            );
          }

          send_all(client_socket, "OK " + std::to_string(entry.seq) + "\n");
        }
        // GET <key>
        else if (tokens[0] == "GET" && tokens.size() >= 2) {
          std::lock_guard<std::mutex> lock(mtx);
          auto kv_iter = kv.find(tokens[1]);

          if (kv_iter == kv.end())
            send_all(client_socket, "NOT_FOUND\n");
          else
            send_all(client_socket, "VALUE " + kv_iter->second + "\n");
        }
        else {
          send_all(client_socket, "ERR usage: SET <k> <v> | GET <k> | DEL <k>\n");
        }
      }

      net_close(client_socket);
    }).detach();
  }
}

// Inicijuoja leader'į:
// - perskaito esamą WAL
// - atstato kv ir logbuf
// - paleidžia follower ir client serverius.
int main(int argc, char** argv) {
  if (argc < 5) {
    std::cerr << "Usage: leader <client_port> <follower_port> <wal_path> <required_acks>\n";
    return 1;
  }

  uint16_t    client_port   = static_cast<uint16_t>(std::stoi(argv[1]));
  uint16_t    follower_port = static_cast<uint16_t>(std::stoi(argv[2]));
  std::string wal_path      = argv[3];
  REQUIRED_ACKS             = std::stoi(argv[4]);

  // Užkraunam buvusius WAL įrašus ir atstatom vidinę būseną.
  std::vector<WalEntry> previous_entries;
  wal_load(wal_path, previous_entries);

  {
    std::lock_guard<std::mutex> lock(mtx);
    uint64_t max_seq = 0;

    for (auto& entry : previous_entries) {
      if (entry.op == Op::SET)
        kv[entry.key] = entry.value;
      else
        kv.erase(entry.key);

      logbuf.push_back(entry);
      max_seq = std::max(max_seq, entry.seq);
    }

    next_seq = max_seq + 1;
  }

  std::thread follower_accept_thread(accept_followers, follower_port);
  serve_clients(client_port, wal_path);
  follower_accept_thread.join();

  return 0;
}
