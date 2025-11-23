#include "common.hpp"

// Informacija apie vieną follower'io ryšį leader'yje.
// Laikom socket'ą, iki kokio seq follower'is patvirtino (ACK),
// ir ar jis dar laikomas gyvu.
struct FollowerConn {
  sock_t   follower_socket{NET_INVALID};
  uint64_t acked_upto_seq{0};
  bool     is_alive{true};
};

// Bendri leader'io globalūs duomenys

// Užraktas kv / logbuf / followers bendram naudojimui
static std::mutex mtx;
// Naudojama laukimui, kol follower'iai atsiunčia ACK (replikacijos patvirtinimus)
static std::condition_variable cv;

// Pagrindinis key-value store lyderio atmintyje
static std::unordered_map<std::string,std::string> kv;

// WAL buferis atmintyje – visi įrašai, kurie buvo parašyti į WAL failą.
// Follower'iams, kurie prisijungia vėliau, iš čia persiunčiam trūkstamus įrašus.
static std::vector<WalEntry> logbuf;

// Visų žinomų followerių sąrašas
static std::vector<std::shared_ptr<FollowerConn>> followers;

// Sekantis sekos numeris naujiems WAL įrašams (monotoniškai didėja)
static std::atomic<uint64_t> next_seq{1};

// Kiek follower'ių ACK'ų reikia laikyti operaciją įvykusia (0 – nereikia nieko laukti)
static int REQUIRED_ACKS = 0;

// Paprastas periodinis logas, kad iš logų matytųsi, kuris node yra LEADER.
static void leader_announce_thread(const std::string& host, uint16_t client_port) {
  try {
    while (true) {
      // Kas 15 sekundžių išspausdinam, kad šitas procesas yra leader'is
      log_line(
        LogLevel::INFO,
        std::string("[Leader] ") + host + " " + std::to_string(client_port)
      );
      std::this_thread::sleep_for(std::chrono::seconds(15));
    }
  } catch (const std::exception& ex) {
    log_line(LogLevel::ERROR,
             std::string("Leader announce thread exception: ") + ex.what());
  } catch (...) {
    log_line(LogLevel::ERROR, "Leader announce thread unknown exception");
  }
}

// Išsiunčia vieną WAL įrašą visiems aktyviems follower'iams.
// Naudojama tiek naujiems įrašams (SET/DEL), tiek paleidimo metu replay'inant logbuf.
static void broadcast_entry(const WalEntry& entry) {
  std::string message;
  if (entry.op == Op::SET)
    message = "WRITE "  + std::to_string(entry.seq) + " " + entry.key + " " + entry.value + "\n";
  else
    message = "DELETE " + std::to_string(entry.seq) + " " + entry.key + "\n";

  // Einam per visus follower'ius ir bandome siųsti
  for (auto& follower : followers) {
    if (!follower->is_alive) continue;
    if (!send_all(follower->follower_socket, message)) {
      // Jei siųsti nepavyko – follower'į laikom „nebegyvu“
      follower->is_alive = false;
      log_line(LogLevel::WARN, "broadcast_entry: send failed to follower socket");
    }
  }
}

// Kiekvienam follower'iui skirtas thread'as, kuris:
// 1) Perskaito HELLO <last_seq> iš follower'io;
// 2) Nusiunčia jam trūkstamus WAL įrašus;
// 3) Laukia iš jo ACK pranešimų.
static void follower_thread(std::shared_ptr<FollowerConn> follower) {
  try {
    // --- 1) HELLO iš follower'io ---
    std::string hello_line;
    if (!recv_line(follower->follower_socket, hello_line)) {
      follower->is_alive = false;
      return;
    }

    auto hello_parts = split(hello_line, ' ');
    if (hello_parts.size() != 2 || hello_parts[0] != "HELLO") {
      follower->is_alive = false;
      log_line(LogLevel::WARN, "Follower sent bad HELLO: " + hello_line);
      return;
    }

    uint64_t last_applied_seq = 0;
    try {
      last_applied_seq = std::stoull(hello_parts[1]);  // follower'io paskutinis pritaikytas seq
    } catch (...) {
      log_line(LogLevel::WARN, "Bad seq in follower HELLO: " + hello_parts[1]);
      follower->is_alive = false;
      return;
    }

    // --- 2) Persiunčiam follower'iui visus logbuf įrašus, kurių jis neturi ---
    {
      std::unique_lock<std::mutex> lock(mtx);
      for (const auto& entry : logbuf) {
        if (entry.seq > last_applied_seq) {
          std::string message = (entry.op == Op::SET)
            ? ("WRITE "  + std::to_string(entry.seq) + " " + entry.key + " " + entry.value + "\n")
            : ("DELETE " + std::to_string(entry.seq) + " " + entry.key + "\n");
          if (!send_all(follower->follower_socket, message)) {
            // Jei siuntimas nepavyksta – nutraukiam šitą follower'į
            follower->is_alive = false;
            return;
          }
        }
      }
    }

    // --- 3) Laukiame ACK pranešimų ---
    while (follower->is_alive) {
      std::string recv_line_str;
      if (!recv_line(follower->follower_socket, recv_line_str)) {
        // nutrūkęs ryšys / klaida
        follower->is_alive = false;
        break;
      }
      auto tokens = split(recv_line_str, ' ');
      // Tikimės formos: ACK <seq>
      if (tokens.size() == 2 && tokens[0] == "ACK") {
        try {
          uint64_t ack_seq = std::stoull(tokens[1]);
          // Atnaujinam, iki kokio seq follower'is yra atsilikęs / pasivijęs
          follower->acked_upto_seq = std::max(follower->acked_upto_seq, ack_seq);
          // Pranešam visiems, kas laukia ant condition_variable (pvz. klientų SET/DEL)
          cv.notify_all();
        } catch (...) {
          log_line(LogLevel::WARN, "Bad ACK seq from follower: " + tokens[1]);
        }
      }
    }
  } catch (const std::exception& ex) {
    log_line(LogLevel::ERROR, std::string("Exception in follower_thread: ") + ex.what());
  } catch (...) {
    log_line(LogLevel::ERROR, "Unknown exception in follower_thread");
  }
}

// Suskaičiuoja, kiek follower'ių turi acked_upto_seq >= duotas seq.
// Naudojama tam, kad patikrinti ar surinkom pakankamai ACK'ų (REQUIRED_ACKS).
static size_t count_acks(uint64_t seq) {
  size_t acked_count = 0;
  for (auto& follower : followers) {
    if (follower->is_alive && follower->acked_upto_seq >= seq)
      ++acked_count;
  }
  return acked_count;
}

// Klausosi naujų follower'io jungčių nurodytame porte ir kiekvieną naują
// follower'į prideda į followers sąrašą bei paleidžia jam follower_thread.
static void accept_followers(uint16_t follower_port) {
  sock_t listen_socket = tcp_listen(follower_port);
  if (listen_socket == NET_INVALID) {
    std::cerr << "Follower listen failed\n";
    log_line(LogLevel::ERROR, "Follower listen failed on port " + std::to_string(follower_port));
    return;
  }

  log_line(LogLevel::INFO, "Leader: listening followers on " + std::to_string(follower_port));

  while (true) {
    sock_t follower_socket = tcp_accept(listen_socket);
    if (follower_socket == NET_INVALID) continue;

    auto follower_conn = std::make_shared<FollowerConn>();
    follower_conn->follower_socket = follower_socket;

    {
      std::lock_guard<std::mutex> lock(mtx);
      followers.push_back(follower_conn);
    }

    // Kiekvienam follower'iui – atskiras thread'as
    std::thread(follower_thread, follower_conn).detach();
  }
}

// Klausosi klientų (SET/GET/DEL) nurodytame porte ir tvarko jų užklausas.
// Visi SET/DEL:
//  - įrašomi į WAL failą
//  - įrašomi į kv
//  - pridedami į logbuf
//  - išsiunčiami follower'iams (broadcast_entry)
//  - jei REQUIRED_ACKS > 0, laukiama ACK'ų iš follower'ių
static void serve_clients(uint16_t client_port, const std::string& wal_path) {
  sock_t listen_socket = tcp_listen(client_port);
  if (listen_socket == NET_INVALID) {
    std::cerr << "Client listen failed\n";
    log_line(LogLevel::ERROR, "Client listen failed on port " + std::to_string(client_port));
    return;
  }

  // Atidarom WAL failą papildymui – visus naujus įrašus rašysim čia
  std::ofstream wal_stream(wal_path, std::ios::app);
  if (!wal_stream.is_open()) {
    log_line(LogLevel::ERROR, "Failed to open WAL file: " + wal_path);
    return;
  }

  log_line(LogLevel::INFO, "Leader: listening clients on " + std::to_string(client_port));

  while (true) {
    sock_t client_socket = tcp_accept(listen_socket);
    if (client_socket == NET_INVALID) continue;

    // Kiekvienam klientui – atskiras handler'is threade
    std::thread([client_socket, &wal_stream]() {
      try {
        std::string request_line;

        // Skaitom užklausas iš kliento, kol jis kalba
        while (recv_line(client_socket, request_line)) {
          auto tokens = split(trim(request_line), ' ');
          if (tokens.empty()) continue;

          // --- SET/PUT <key> <value> ---
          if ((tokens[0] == "PUT" || tokens[0] == "SET") && tokens.size() >= 3) {
            WalEntry entry;
            {
              // Kritinė sekcija – modifikuojam kv, logbuf ir next_seq
              std::lock_guard<std::mutex> lock(mtx);

              entry.seq   = next_seq++;  // naujas sekos numeris
              entry.op    = Op::SET;
              entry.key   = tokens[1];
              entry.value = tokens[2];

              // Atnaujinam lokalią būseną
              kv[entry.key] = entry.value;
              logbuf.push_back(entry);

              // Parašom į WAL failą
              wal_append(wal_stream, entry);

              // Broadcastinam follower'iams
              broadcast_entry(entry);
            }

            // Jei turim reikalavimą gauti ACK iš followerių – palaukiam
            if (REQUIRED_ACKS > 0) {
              std::unique_lock<std::mutex> lock(mtx);
              cv.wait_for(
                lock,
                std::chrono::seconds(3),  // max laukimo laikas
                [&] { return count_acks(entry.seq) >= static_cast<size_t>(REQUIRED_ACKS); }
              );
            }

            // Atsakymas klientui – OK ir seq numeris
            send_all(client_socket, "OK " + std::to_string(entry.seq) + "\n");
          }
          // --- DEL <key> ---
          else if (tokens[0] == "DEL" && tokens.size() >= 2) {
            WalEntry entry;
            {
              std::lock_guard<std::mutex> lock(mtx);

              entry.seq = next_seq++;
              entry.op  = Op::DEL;
              entry.key = tokens[1];

              // Išmetam iš lokalaus kv store
              kv.erase(entry.key);
              logbuf.push_back(entry);

              // Parašom į WAL failą
              wal_append(wal_stream, entry);

              // Išsiunčiam follower'iams
              broadcast_entry(entry);
            }

            // Vėl – jei reikia ACK'ų, palaukiam
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
          // --- GET <key> ---
          else if (tokens[0] == "GET" && tokens.size() >= 2) {
            std::lock_guard<std::mutex> lock(mtx);
            auto kv_iter = kv.find(tokens[1]);

            if (kv_iter == kv.end())
              send_all(client_socket, "NOT_FOUND\n");
            else
              send_all(client_socket, "VALUE " + kv_iter->second + "\n");
          }
          // Neatpažinta komanda – grąžinam error'ą su usage
          else {
            send_all(client_socket, "ERR usage: SET <k> <v> | GET <k> | DEL <k>\n");
          }
        }
      } catch (const std::exception& ex) {
        log_line(LogLevel::ERROR, std::string("Exception in client handler: ") + ex.what());
      } catch (...) {
        log_line(LogLevel::ERROR, "Unknown exception in client handler");
      }

      // Baigėm su klientu – uždarom socket'ą
      net_close(client_socket);
    }).detach();
  }
}

int main(int argc, char** argv) {
  try {
    // Argumentai:
    // leader <client_port> <follower_port> <wal_path> <required_acks> [host]
    if (argc < 5) {
      std::cerr << "Usage: leader <client_port> <follower_port> <wal_path> <required_acks> [host]\n";
      return 1;
    }

    int client_port_i   = std::stoi(argv[1]);
    int follower_port_i = std::stoi(argv[2]);
    if (client_port_i <= 0 || client_port_i > 65535 ||
        follower_port_i <= 0 || follower_port_i > 65535) {
      std::cerr << "Invalid ports\n";
      return 1;
    }

    uint16_t client_port   = static_cast<uint16_t>(client_port_i);
    uint16_t follower_port = static_cast<uint16_t>(follower_port_i);
    std::string wal_path   = argv[3];

    REQUIRED_ACKS = std::stoi(argv[4]);
    if (REQUIRED_ACKS < 0) REQUIRED_ACKS = 0;  // neigiamos reikšmės – traktuojam kaip 0

    // host'as – optional 5-as argumentas, kad galėtum atspausdinti IP arba hostname'e
    std::string leader_host = (argc >= 6) ? std::string(argv[5]) : std::string("unknown-host");

    // Pradinė informacija log'ams – kad matytum, kur startavo leader'is ir su kokiais parametrais
    log_line(
      LogLevel::INFO,
      std::string("[Leader] starting on ") + leader_host +
      ":" + std::to_string(client_port) +
      " repl_port=" + std::to_string(follower_port) +
      " wal=" + wal_path +
      " required_acks=" + std::to_string(REQUIRED_ACKS)
    );

    // --- Atkuriam būseną iš esamo WAL failo (jei toks yra) ---

    std::vector<WalEntry> previous_entries;
    wal_load(wal_path, previous_entries);  // jei failo nėra, tiesiog liks tuščia

    {
      std::lock_guard<std::mutex> lock(mtx);
      uint64_t max_seq = 0;

      // Pritaikom visus senus WAL įrašus į kv ir logbuf
      for (auto& entry : previous_entries) {
        if (entry.op == Op::SET)
          kv[entry.key] = entry.value;
        else
          kv.erase(entry.key);

        logbuf.push_back(entry);
        max_seq = std::max(max_seq, entry.seq);
      }

      // next_seq nustatom į 1 + max seq, kad nenaudotume jau buvusių numerių
      next_seq = max_seq + 1;
    }

    // Paleidžiam periodinį "[Leader] host port" log'ą kas 15 s
    std::thread announce_thr(leader_announce_thread, leader_host, client_port);
    announce_thr.detach();  // thread'as gyvena iki proceso pabaigos

    // Paleidžiam followerių priėmėją atskiram threade
    std::thread follower_accept_thread(accept_followers, follower_port);

    // Pagrindinis thread'as aptarnauja klientus (SET/GET/DEL)
    serve_clients(client_port, wal_path);

    // Jei kada nors serve_clients baigtųsi (teoriškai ne), palaukiam follower_accept_thread
    follower_accept_thread.join();

    return 0;
  } catch (const std::exception& ex) {
    log_line(LogLevel::ERROR, std::string("Fatal exception in leader main: ") + ex.what());
    return 1;
  } catch (...) {
    log_line(LogLevel::ERROR, "Unknown fatal exception in leader main");
    return 1;
  }
}
