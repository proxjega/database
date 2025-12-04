#include "../include/common.hpp"
#include "../include/rules.hpp"
#include "../../btree/include/database.h"
#include <chrono>
#include <thread>
#include <atomic>
#include <vector>
#include <string>
#include <fstream>
#include <random>
#include <stdexcept>

#ifdef _WIN32
  #include <windows.h>
#else
  #include <unistd.h>
  #include <signal.h>
  #include <sys/wait.h>
#endif

// -------- Logging helpers (naudojam per log_msg) --------

// Lokalus log level (atskirtas nuo bendro LogLevel)
enum class RunLogLevel : uint8_t { DEBUG, INFO, WARN, ERROR };

// Paverčia RunLogLevel į string tag'ą, pvz. "[INFO] "
static std::string lvl_tag(RunLogLevel lvl) {
  switch (lvl) {
    case RunLogLevel::DEBUG: return "[DEBUG] ";
    case RunLogLevel::INFO:  return "[INFO] ";
    case RunLogLevel::WARN:  return "[WARN] ";
    case RunLogLevel::ERROR: return "[ERROR] ";
    default:                 return "[LOG] ";
  }
}

// Wrapperis aplink log_msg, kuris prideda level tag'ą.
// Naudojam visam run.cpp vietoj tiesioginio log_msg kvietimo.
static void run_log(ClusterState& cs, int selfId, RunLogLevel lvl, const std::string& msg) {
  log_msg(cs, selfId, lvl_tag(lvl) + msg);
}

// -------- Global cluster state --------

// Bendras klasterio valdymo state (NodeState, leaderId, alive ir t.t.)
static ClusterState g_cluster_state;
// Šio proceso node ID (atitinka CLUSTER masyvo id)
static int      g_self_id   = 0;
// Šio node informacija (host, control port ir t.t.)
static NodeInfo g_self_info;

// Laikas (ms nuo starto) kada paskutinį kartą gavom heartbeat iš leaderio
static std::atomic<long long> g_last_heartbeat_ms{0};
// Ar šiuo metu vyksta rinkimai (kad ne startint kelių election iškart)
static std::atomic<bool>      g_election_inflight{false};

// Dabartinis term (raft-style epochos numeris)
static std::atomic<int>      g_current_term{0};
// Už ką balsavome šiame term (kandidato id arba -1 jei dar nebalsavom)
static std::atomic<int>      g_voted_for{-1};
// Paskutinis mūsų žinomas sequencas (pagal WAL failą)
static std::atomic<uint64_t> g_my_last_seq{0};

// Rinkimų metu: kiek balsų gavom
static std::atomic<int> g_votes_received{0};
// Rinkimų term, kuriam šiuo metu renkamas lyderis
static std::atomic<int> g_election_term{0};

// Kiek yra narių CLUSTER masyve (klasterio dydis)
static constexpr int CLUSTER_N =
    (int)(sizeof(CLUSTER) / sizeof(CLUSTER[0]));

// Paskutinio žinomo „nominal" leaderio id (iš heartbeat'ų)
static int       g_leader_id{0};
// „Efektyvus" leaderis – tas kuriuo jau tikim ir kuriam spawninam follower child
static int       g_effective_leader{0};
// Nuo kada (ms) nuolat matom tą patį leader_id per HB, kad jį patvirtinti kaip effective
static long long g_leader_seen_since_ms{0};


// -------- Time helpers --------

// now_ms() is defined in common.hpp - returns current time in ms since epoch

// Gražina atsitiktinį rinkimų timeout'ą ms (kad node'ai nesinukentintų visi vienu metu)
static int random_election_timeout_ms() {
  static thread_local std::mt19937 rng(std::random_device{}());
  std::uniform_int_distribution<int> dist(ELECTION_TIMEOUT_MS,
                                          ELECTION_TIMEOUT_MS + 400);
  return dist(rng);
}

// Log failo pavadinimas šitam node (pvz. "node1.log")
static std::string my_log_file() {
  return "node" + std::to_string(g_self_id) + ".log";
}

// -------- lastSeq helpers --------

// Perskaito paskutinę eilutę iš WAL/log failo ir bando iš jos ištraukti seq (pirmą lauką)
// Tikimasi formato: "<seq>\tKITI_LAUKAI"
static uint64_t parse_last_seq_from_file(const std::string& path) {
  std::ifstream in(path);
  if (!in.good()) return 0;

  std::string line, last;
  // Skaitom failą iki galo ir prisimenam paskutinę ne tuščią eilutę
  while (std::getline(in, line)) {
    if (!line.empty()) {
      last = line;
    }
  }

  if (last.empty()) {
    return 0;
  }

  // Pirmiausia skaldom pagal tab'ą, jei nepavyksta – pagal tarpą
  auto parts = split(last, '\t');
  if (parts.empty()) {
    parts = split(last, ' ');
  }
  if (parts.empty()) {
    return 0;
  }

  try {
    return (uint64_t)std::stoull(parts[0]);
  } catch (...) {
    return 0;
  }
}

static std::string my_db_name() {
    return "node" + std::to_string(g_self_id);
}

// Apskaičiuoja paskutinį seq šio node'o loge
// Reads LSN from Database metapage instead of WAL files
// This ensures correct LSN even after Optimize() which deletes WAL files
static uint64_t compute_my_last_seq() {
    try {
        // Read LSN from database metapage (always persisted)
        Database db(my_db_name());
        return db.getLSN();
    } catch (...) {
        return 0;
    }
}

// -------- Child process utils --------

// Struktūra, sauganti paleisto vaiko informaciją (leader/follower binaras).
struct Child {
  bool running{false};
#ifdef _WIN32
  PROCESS_INFORMATION pi{};
#else
  pid_t pid{-1};
#endif
};

// Patikrina ar child procesas vis dar gyvas.
static bool child_alive(Child &child) {
  if (!child.running) {
    return false;
  }
#ifdef _WIN32
  DWORD code = 0;
  if (GetExitCodeProcess(child.pi.hProcess, &code))
    return code == STILL_ACTIVE;
  return false;
#else
  int status = 0;
  pid_t r = waitpid(child.pid, &status, WNOHANG);
  return r == 0; // 0 – procesas dar gyvas
#endif
}

// Global child process handle (leader or follower)
static Child g_child;

// Paleidžia naują procesą su komandą cmd ir užpildo Child struktūrą.
static bool start_process(const std::string &cmd, Child &child) {
#ifdef _WIN32
  STARTUPINFOA si{};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESHOWWINDOW;
  si.wShowWindow = SW_SHOWNORMAL;

  std::string mutableCmd = cmd;
  BOOL ok = CreateProcessA(nullptr, mutableCmd.data(), nullptr, nullptr, FALSE,
                           CREATE_NEW_CONSOLE | CREATE_NEW_PROCESS_GROUP,
                           nullptr, nullptr, &si, &child.pi);
  if (ok) {
    child.running = true;
    return true;
  }
  return false;
#else
  pid_t pid = fork();
  if (pid == 0) {
    // Vaiko procese – paleidžiam shell ir exe
    execl("/bin/sh", "sh", "-c", cmd.c_str(), (char*)nullptr);
    _exit(127); // jei execl nepavyko
  } else if (pid > 0) {
    // Tėvo procese
    child.pid     = pid;
    child.running = true;
    return true;
  }
  return false;
#endif
}

// Saugiai stabdo vaiką (siunčia SIGTERM/SIGKILL arba TerminateProcess ant Windows).
static void stop_process(Child& child) {
  if (!child.running) {
    return;
  }
#ifndef _WIN32
  if (child.pid > 0) {
    // Force-close all sockets to break blocking recv() calls in detached threads
    // This allows SIGTERM to be processed instead of being ignored due to I/O wait
    std::string ss_cmd = "ss -K 'sport = :7001 or sport = :7002 or sport = :7101 or sport = :7102 or sport = :7103 or sport = :7104' 2>/dev/null";
    int ss_result = system(ss_cmd.c_str());
    (void)ss_result;  // Ignore failures - this is best-effort cleanup

    // Give sockets time to close (allows threads to exit recv() and process signals)
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Send SIGTERM for graceful shutdown
    kill(child.pid, SIGTERM);

    // Wait up to 2 seconds for process to die
    bool process_died = false;
    for (int i = 0; i < 20; ++i) {
      int status = 0;
      pid_t r = waitpid(child.pid, &status, WNOHANG);
      if (r == child.pid) {
        process_died = true;
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // If still alive, force kill with SIGKILL
    if (!process_died) {
      run_log(g_cluster_state, g_self_id, RunLogLevel::WARN,
              "Child PID " + std::to_string(child.pid) + " didn't respond to SIGTERM, sending SIGKILL");

      kill(child.pid, SIGKILL);

      // Wait up to 5 more seconds for SIGKILL to work
      for (int i = 0; i < 50; ++i) {
        int status = 0;
        pid_t r = waitpid(child.pid, &status, WNOHANG);
        if (r == child.pid) {
          process_died = true;
          break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }

      if (!process_died) {
        // Process is in uninterruptible sleep or kernel deadlock - this is very bad
        run_log(g_cluster_state, g_self_id, RunLogLevel::ERROR,
                "CRITICAL: Child PID " + std::to_string(child.pid) +
                " survived SIGKILL! Process may be in uninterruptible sleep (D state). "
                "Manual intervention required: kill -9 " + std::to_string(child.pid));
      }
    }
  }
#else
  TerminateProcess(child.pi.hProcess, 0);
  WaitForSingleObject(child.pi.hProcess, INFINITE);
  CloseHandle(child.pi.hThread);
  CloseHandle(child.pi.hProcess);
#endif

  child.running = false;
}

// -------- Control plane (HB / votes) --------

// Apdoroja vieną inbound TCP jungtį kontroliniam protokolui
// (HB, VOTE_REQ, VOTE_RESP) tarp run procesų.
static void handle_conn(sock_t client_socket) {
  try {
    std::string line;
    if (!recv_line(client_socket, line)) { net_close(client_socket); return; }
    auto tokens = split(trim(line), ' ');
    if (tokens.empty()) { net_close(client_socket); return; }

    // HB <term> <leaderId> <lastSeq>
    if (tokens[0] == "HB" && tokens.size() >= 4) {
      int      term            = 0;
      int      leader_id       = 0;
      uint64_t leader_last_seq = 0;

      try {
        term            = std::stoi(tokens[1]);
        leader_id       = std::stoi(tokens[2]);
        leader_last_seq = (uint64_t)std::stoull(tokens[3]);
      } catch (...) {
        run_log(g_cluster_state, g_self_id, RunLogLevel::WARN,
                "invalid HB line: " + line);
        net_close(client_socket);
        return;
      }

      (void)leader_last_seq; // kol kas nenaudojam, bet galima būtų lyginti logų „šviežumą“

      // Jei gautas term >= mūsų – atnaujinam savo state pagal heartbeat
      if (term >= g_current_term.load()) {
        g_current_term = term;
        g_voted_for    = -1; // naujas term – pamirštam seną balsą

        // Jeigu heartbeat'e leader_id = aš, laikom save LEADER, kitu atveju FOLLOWER
        g_cluster_state.state =
          (leader_id == g_self_id) ? NodeState::LEADER : NodeState::FOLLOWER;

        g_leader_id         = leader_id;
        g_last_heartbeat_ms = now_ms();

        // Lėtas „debounce“ effective_leader: reikia ~800ms nuoseklių HB,
        // kad priimtume naują leader'į.
        if (g_effective_leader != leader_id) {
          if (g_leader_seen_since_ms == 0) {
            g_leader_seen_since_ms = now_ms();
          } else if (now_ms() - g_leader_seen_since_ms >= 800) {
            g_effective_leader     = leader_id;
            g_leader_seen_since_ms = 0;
          }
        } else {
          g_leader_seen_since_ms = 0;
        }
      }
      net_close(client_socket);
      return;
    }

    // VOTE_REQ <term> <candId> <candLastSeq>
    if (tokens[0] == "VOTE_REQ" && tokens.size() >= 4) {
      int      term          = 0;
      int      candidate_id  = 0;
      uint64_t candidate_seq = 0;

      try {
        term          = std::stoi(tokens[1]);
        candidate_id  = std::stoi(tokens[2]);
        candidate_seq = (uint64_t)std::stoull(tokens[3]);
      } catch (...) {
        run_log(g_cluster_state, g_self_id, RunLogLevel::WARN,
                "invalid VOTE_REQ line: " + line);
        net_close(client_socket);
        return;
      }

      bool grant = false;

      // Jei kandidato term > mūsų term – atnaujinam term ir „resetinam“ balsą
      if (term > g_current_term.load()) {
        g_current_term = term;
        g_voted_for    = -1;
      }

      // Balsavimo logika: balsuojam jei:
      // - term sutampa
      // - dar nebalsavom arba jau balsavom už šį kandidatą
      // - kandidato log'o seq nėra atsilikęs nuo mūsų
      if (term == g_current_term.load()) {
        uint64_t my_seq = g_my_last_seq.load();
        if ((g_voted_for.load() == -1 || g_voted_for.load() == candidate_id) &&
            (candidate_seq >= my_seq)) {
          g_voted_for = candidate_id;
          grant       = true;
        }
      }

      // Grąžinam VOTE_RESP kandidatui per atskirą TCP jungtį
      if (const auto *candidate_node = getNode(candidate_id)) {
        sock_t s = tcp_connect(candidate_node->host, candidate_node->port);
        if (s != NET_INVALID) {
          send_all(
            s,
            "VOTE_RESP " + std::to_string(term) + " " + (grant ? "1" : "0") + "\n"
          );
          net_close(s);
        } else {
          run_log(g_cluster_state, g_self_id, RunLogLevel::WARN,
                  "failed to connect back to candidate for VOTE_RESP");
        }
      }

      net_close(client_socket);
      return;
    }

    // VOTE_RESP <term> <granted>
    if (tokens[0] == "VOTE_RESP" && tokens.size() >= 3) {
      int resp_term = 0;
      int granted   = 0;

      try {
        resp_term = std::stoi(tokens[1]);
        granted   = std::stoi(tokens[2]);
      } catch (...) {
        run_log(g_cluster_state, g_self_id, RunLogLevel::WARN,
                "invalid VOTE_RESP line: " + line);
        net_close(client_socket);
        return;
      }

      // Skaičiuojam tik tada, kai patys esam CANDIDATE, term sutampa ir granted=1
      if (g_cluster_state.state == NodeState::CANDIDATE &&
          resp_term == g_election_term.load() &&
          granted == 1) {
        g_votes_received.fetch_add(1);
      }

      net_close(client_socket);
      return;
    }

    // CLUSTER_STATUS – returns node's view of cluster state
    if (tokens[0] == "CLUSTER_STATUS") {
      std::ostringstream response;

      // Node's own status
      int myId = g_self_id;
      std::string myRole = (g_cluster_state.state == NodeState::LEADER) ? "LEADER" :
                           (g_cluster_state.state == NodeState::CANDIDATE) ? "CANDIDATE" : "FOLLOWER";
      uint64_t myTerm = g_current_term.load();
      int leaderId = g_effective_leader;
      uint64_t myLSN = g_my_last_seq.load();
      long long lastHBAge = now_ms() - g_last_heartbeat_ms.load();

      response << "STATUS " << myId << " " << myRole << " " << myTerm << " "
               << leaderId << " " << myLSN << " " << lastHBAge << "\n";

      // If this node is the leader, query follower status from leader process
      if (g_cluster_state.state == NodeState::LEADER && child_alive(g_child)) {
        // Query local leader process via localhost CLIENT_PORT
        sock_t leader_sock = tcp_connect("127.0.0.1", CLIENT_PORT);
        if (leader_sock != NET_INVALID) {
          set_socket_timeouts(leader_sock, 1000); // 1s timeout
          send_all(leader_sock, "INTERNAL_FOLLOWER_STATUS\n");

          // Read follower status lines
          std::string follower_line;
          while (recv_line(leader_sock, follower_line)) {
            follower_line = trim(follower_line);
            if (follower_line == "END") break;
            if (follower_line.substr(0, 16) == "FOLLOWER_STATUS ") {
              response << follower_line << "\n";
            }
          }
          net_close(leader_sock);
        }
      }

      response << "END\n";
      send_all(client_socket, response.str());
      net_close(client_socket);
      return;
    }

    // Nežinoma kontrolinė žinutė – tiesiog užloginam
    run_log(g_cluster_state, g_self_id, RunLogLevel::DEBUG,
            "unknown control message: " + line);
    net_close(client_socket);
  } catch (const std::exception& ex) {
    run_log(g_cluster_state, g_self_id, RunLogLevel::ERROR,
            std::string("exception in handle_conn: ") + ex.what());
    net_close(client_socket);
  } catch (...) {
    run_log(g_cluster_state, g_self_id, RunLogLevel::ERROR,
            "unknown exception in handle_conn");
    net_close(client_socket);
  }
}

// Pagrindinis „control“ serverio ciklas: klausosi g_self_info.port
// ir kiekvieną jungtį atiduoda handle_conn threade.
static void listen_loop() {
  try {
    sock_t listen_socket = tcp_listen(g_self_info.port);
    if (listen_socket == NET_INVALID) {
      std::cerr << "cannot bind control port " << g_self_info.port << "\n";
      return;
    }

    while (g_cluster_state.alive) {
      sock_t client_socket = tcp_accept(listen_socket);
      if (client_socket != NET_INVALID) {
        std::thread(handle_conn, client_socket).detach();
      }
    }

    net_close(listen_socket);
  } catch (const std::exception& ex) {
    run_log(g_cluster_state, g_self_id, RunLogLevel::ERROR,
            std::string("exception in listen_loop: ") + ex.what());
  } catch (...) {
    run_log(g_cluster_state, g_self_id, RunLogLevel::ERROR,
            "unknown exception in listen_loop");
  }
}

// -------- Heartbeat & elections --------

// Jeigu mes esame LEADER, ši funkcija periodiškai siunčia:
// HB <term> <leaderId> <lastSeq>
// visiems CLUSTER nariams.
static void leader_heartbeat() {
  try {
    while (g_cluster_state.alive && g_cluster_state.state == NodeState::LEADER) {
      // Paskutinis mūsų WAL seq (iš log failo)
      uint64_t last_seq = compute_my_last_seq();
      g_my_last_seq     = last_seq;

      // Broadcastinam heartbeat'ą visiems node'ams
      for (auto& node : CLUSTER) {
        sock_t s = tcp_connect(node.host, node.port);
        if (s != NET_INVALID) {
          send_all(
            s,
            "HB " + std::to_string(g_current_term.load()) + " " +
            std::to_string(g_self_id) + " " +
            std::to_string(last_seq) + "\n"
          );
          net_close(s);
        }
      }

      g_last_heartbeat_ms = now_ms();
      std::this_thread::sleep_for(
        std::chrono::milliseconds(HEARTBEAT_INTERVAL_MS)
      );
    }
  } catch (const std::exception& ex) {
    run_log(g_cluster_state, g_self_id, RunLogLevel::ERROR,
            std::string("exception in leader_heartbeat: ") + ex.what());
  } catch (...) {
    run_log(g_cluster_state, g_self_id, RunLogLevel::ERROR,
            "unknown exception in leader_heartbeat");
  }
}

// Startuoja naujus rinkimus (jei dar nevyksta).
// 1) Pakelia term
// 2) Pasižymi CANDIDATE, balsuoja už save
// 3) Išsiunčia VOTE_REQ visiems reachinamiems node'ams
// 4) Laukia balsų arba timeout'o
// -------- Elections: tikra dauguma iš VISŲ mazgų --------
static void start_election() {
  bool expected = false;
  if (!g_election_inflight.compare_exchange_strong(expected, true))
    return; // jau vyksta rinkimai

  int new_term = g_current_term.load() + 1;
  g_current_term  = new_term;
  g_election_term = new_term;
  g_voted_for     = g_self_id;
  g_cluster_state.state = NodeState::CANDIDATE;
  g_my_last_seq   = compute_my_last_seq();

  // balsas už save
  g_votes_received = 1;

  const int total_nodes   = CLUSTER_N;
  const int requiredVotes = total_nodes / 2 + 1;   // tikra dauguma iš VISŲ

  run_log(
    g_cluster_state,
    g_self_id,
    RunLogLevel::INFO,
    "start election term " + std::to_string(g_current_term.load()) +
    " lastSeq=" + std::to_string(g_my_last_seq.load()) +
    " totalNodes=" + std::to_string(total_nodes) +
    " requiredVotes=" + std::to_string(requiredVotes)
  );

  // Išsiunčiam VOTE_REQ visiems kitiems (net jei jų nepasiekiam)
  int reachable_nodes = 1; // save skaičiuojam
  for (auto& node : CLUSTER) {
    if (node.id == g_self_id) continue;

    sock_t s = tcp_connect(node.host, node.port);
    if (s != NET_INVALID) {
      reachable_nodes++;
      send_all(
        s,
        "VOTE_REQ " + std::to_string(g_current_term.load()) + " " +
        std::to_string(g_self_id) + " " +
        std::to_string(g_my_last_seq.load()) + "\n"
      );
      net_close(s);
    } else {
      run_log(
        g_cluster_state,
        g_self_id,
        RunLogLevel::WARN,
        "election: cannot reach node " + std::to_string(node.id) +
        " (" + node.host + ":" + std::to_string(node.port) + ")"
      );
    }
  }

  // Specialus atvejis – klasteryje tik 1 mazgas
  if (total_nodes == 1) {
    g_cluster_state.state    = NodeState::LEADER;
    g_cluster_state.leaderId = g_self_id;
    g_leader_id              = g_self_id;
    g_effective_leader       = g_self_id;
    g_leader_seen_since_ms   = 0;

    run_log(
      g_cluster_state,
      g_self_id,
      RunLogLevel::INFO,
      "became LEADER term " + std::to_string(g_current_term.load()) +
      " (single-node cluster)"
    );

    std::thread(leader_heartbeat).detach();
    std::this_thread::sleep_for(std::chrono::milliseconds(800));
    g_election_inflight = false;
    return;
  }

  // Rinkimų laukimo langas
  int  election_timeout = random_election_timeout_ms();
  auto deadline         = now_ms() + election_timeout;

  while (now_ms() < deadline) {
    // Jei per rinkimus pamatėm kitą leaderį – atsitraukiam
    if (now_ms() - g_last_heartbeat_ms.load() <= HEARTBEAT_TIMEOUT_MS &&
        g_cluster_state.state != NodeState::LEADER) {
      run_log(
        g_cluster_state,
        g_self_id,
        RunLogLevel::INFO,
        "another leader detected during election, reverting to FOLLOWER"
      );
      g_cluster_state.state = NodeState::FOLLOWER;
      g_election_inflight   = false;
      return;
    }

    if (g_cluster_state.state == NodeState::LEADER) {
      g_election_inflight = false;
      return;
    }

    // ČIA – tikra dauguma iš visų: votes >= requiredVotes
    if (g_votes_received.load() >= requiredVotes) {
      g_cluster_state.state    = NodeState::LEADER;
      g_cluster_state.leaderId = g_self_id;
      g_leader_id              = g_self_id;
      g_effective_leader       = g_self_id;
      g_leader_seen_since_ms   = 0;

      run_log(
        g_cluster_state,
        g_self_id,
        RunLogLevel::INFO,
        "became LEADER term " + std::to_string(g_current_term.load()) +
        " with votes=" + std::to_string(g_votes_received.load()) +
        " (required=" + std::to_string(requiredVotes) + ", reachable=" +
        std::to_string(reachable_nodes) + ")"
      );

      std::thread(leader_heartbeat).detach();
      std::this_thread::sleep_for(std::chrono::milliseconds(800));
      g_election_inflight = false;
      return;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  // Jei nelaimėjom – reiškia arba nėra daugumos, arba yra splitas
  run_log(
    g_cluster_state,
    g_self_id,
    RunLogLevel::WARN,
    "election term " + std::to_string(g_current_term.load()) +
    " timed out without majority; votes=" +
    std::to_string(g_votes_received.load()) +
    " required=" + std::to_string(requiredVotes) +
    " reachable=" + std::to_string(reachable_nodes) +
    " -> NO LEADER (no quorum)"
  );

  g_cluster_state.state = NodeState::FOLLOWER;
  g_election_inflight   = false;
}

// Followerių monitorius – stebi heartbeat'ų amžių ir,
// jei leaderis „dingsta“, inicijuoja rinkimus.
static void follower_monitor() {
  g_last_heartbeat_ms = now_ms();
  while (g_cluster_state.alive) {
    g_my_last_seq = compute_my_last_seq();

    if (g_cluster_state.state != NodeState::LEADER) {
      long long age = now_ms() - g_last_heartbeat_ms.load();
      // Jei seniai negavom heartbeat ir rinkimai dar nevyksta – startuojam.
      if (age > HEARTBEAT_TIMEOUT_MS && !g_election_inflight.load()) {
        run_log(g_cluster_state, g_self_id, RunLogLevel::WARN,
                "leader timeout -> starting election");
        start_election();
      }
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
  }
}

// -------- role → child process manager --------

// Čia tvarkom realius „data plane" procesus: leader ir follower binarus.
// (g_child is now declared earlier in the file after the Child struct definition)

// Šitas thread'as žiūri į NodeState ir spawn'ina / stabdo
// atitinkamą child procesą: leader arba follower.
static void role_process_manager() {
  NodeState last_role = NodeState::FOLLOWER;
  int       last_effective_leader = 0;

  std::string dbName = my_db_name();

  while (g_cluster_state.alive) {
    NodeState role = g_cluster_state.state.load();
    int effective_leader =
      (role == NodeState::LEADER) ? g_self_id : g_effective_leader;

    // Jei mes = LEADER: turi veikti leader.exe / leader.
    if (role == NodeState::LEADER) {
      // Spawninam iš naujo jei:
      // - anksčiau nebuvom LEADER
      // - arba child'as numirė
      if (last_role != NodeState::LEADER || !child_alive(g_child)) {
        stop_process(g_child);
        // Require majority acknowledgments for write operations (quorum enforcement)
        const int requiredAcks = (CLUSTER_N / 2);  // 2 for 4-node cluster
#ifdef _WIN32
        std::string cmd = ".\\leader.exe " +
                          std::to_string(CLIENT_PORT) + " " +
                          std::to_string(REPL_PORT)   + " " +
                          dbName + " " +
                          std::to_string(requiredAcks) + " " +
                          g_self_info.host;
#else
        // Linux/macOS komanda leader binarui paleisti
        std::string cmd = "./leader " +
                          std::to_string(CLIENT_PORT) + " " +
                          std::to_string(REPL_PORT)   + " " +
                          dbName + " " +
                          std::to_string(requiredAcks) + " " +
                          g_self_info.host;
#endif
        run_log(g_cluster_state, g_self_id, RunLogLevel::INFO,
                "spawn leader child: " + cmd);
        start_process(cmd, g_child);
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
      }
    } else {
      // Jei mes NE leader – turim būti followeris, jei žinom effective_leader.
      if (effective_leader != 0 &&
          (last_role == NodeState::LEADER ||
           last_effective_leader != effective_leader ||
           !child_alive(g_child))) {

        stop_process(g_child);
        const NodeInfo* leader_node = getNode(effective_leader);
        if (leader_node != nullptr) {
          const std::string &follower_log  = dbName;                           // WAL failas followeriui
          std::string follower_snap = "f" + std::to_string(g_self_id) + ".snap"; // snapshot failas
          uint16_t    read_port     = FOLLOWER_READ_PORT(g_self_id);  // Separate read-only port for followers
#ifdef _WIN32
          std::string cmd = ".\\follower.exe " +
                            leader_node->host + " " +
                            std::to_string(REPL_PORT) + " " +
                            follower_log  + " " +
                            follower_snap + " " +
                            std::to_string(read_port) + " " +
                            std::to_string(g_self_id);
#else
          // Linux/macOS follower binaro paleidimo komanda
          std::string cmd = "./follower " +
                            leader_node->host + " " +
                            std::to_string(REPL_PORT) + " " +
                            follower_log  + " " +
                            follower_snap + " " +
                            std::to_string(read_port) + " " +
                            std::to_string(g_self_id);
#endif
          run_log(g_cluster_state, g_self_id, RunLogLevel::INFO,
                  "spawn follower child: " + cmd);
          start_process(cmd, g_child);
          std::this_thread::sleep_for(std::chrono::milliseconds(300));
        } else {
          run_log(g_cluster_state, g_self_id, RunLogLevel::WARN,
                  "no NodeInfo for effective leader id " + std::to_string(effective_leader));
        }
      }
    }

    last_role             = role;
    last_effective_leader = effective_leader;
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
  }

  // Išeinant – uždarom vaiką
  stop_process(g_child);
}

// -------- main --------

int main(int argc, char** argv) {
  try {
    if (argc < 2) {
      std::cerr << "Usage: run <node_id>\n";
      return 1;
    }

    int idTmp = 0;
    try {
      idTmp = std::stoi(argv[1]);
    } catch (...) {
      std::cerr << "bad node id (not an integer)\n";
      return 1;
    }

    g_self_id = idTmp;
    const NodeInfo* me = getNode(g_self_id);
    if (!me) {
      std::cerr << "bad node id\n";
      return 1;
    }
    g_self_info = *me;

    // Log'as, kad žinotume, kokiame control porte klausom šito run proceso
    run_log(
      g_cluster_state,
      g_self_id,
      RunLogLevel::INFO,
      "control port " + std::to_string(g_self_info.port)
    );

    // Pasiimam paskutinį seq iš savo log failo
    g_my_last_seq = compute_my_last_seq();

    // Paleidžiam 3 background thread'us:
    //  - listen_loop: priima HB/VOTE_REQ/VOTE_RESP
    //  - follower_monitor: stebi heartbeat'us ir startuoja rinkimus
    //  - role_process_manager: spawn'ina leader/follower child binarus
    std::thread(listen_loop).detach();
    std::thread(follower_monitor).detach();
    std::thread(role_process_manager).detach();

    // Nedidelis random „delay“ prieš pirmus rinkimus, kad node'ai nesusidubliuotų
    std::this_thread::sleep_for(
      std::chrono::milliseconds(400 + ((g_self_id * 123) % 400))
    );
    start_election();

    // Pagrindinis thread'as tiesiog miega – visa logika vyksta threade.
    while (true) {
      std::this_thread::sleep_for(std::chrono::seconds(5));
    }

    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "[run.cpp] fatal exception: " << ex.what() << "\n";
    return 1;
  } catch (...) {
    std::cerr << "[run.cpp] fatal unknown exception\n";
    return 1;
  }
}
