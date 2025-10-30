#include "common.hpp"
#include "rules.cpp"
#include <chrono>
#include <thread>
#include <atomic>
#include <vector>
#include <string>
#include <fstream>

#ifdef _WIN32
  #include <windows.h>
#else
  #include <unistd.h>
  #include <signal.h>
  #include <sys/wait.h>
#endif

// --------------- Globals ---------------
static ClusterState G;
static int SELF_ID = 0;
static NodeInfo SELF;

static std::atomic<long long> g_last_hb_ms{0};
static std::atomic<bool>      g_election_inflight{false};

static std::atomic<int>      g_current_term{0};
static std::atomic<int>      g_voted_for{-1};
static std::atomic<uint64_t> g_my_last_seq{0};
static constexpr int CLUSTER_N =
    (int)(sizeof(CLUSTER) / sizeof(CLUSTER[0]));

static int       g_leader_id{0};              // last seen leader (may flap)
static int       g_effective_leader{0};       // confirmed leader
static long long g_leader_seen_since_ms{0};

// util
static inline long long now_ms(){
  using namespace std::chrono;
  return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

// --------------- lastSeq helpers ---------------
static uint64_t parse_last_seq_from_file(const std::string& path){
  std::ifstream in(path);
  if(!in.good()) return 0;
  std::string line, last;
  while(std::getline(in,line)) if(!line.empty()) last = line;
  if(last.empty()) return 0;
  // format: "seq \t OP \t key \t value" (wal / f*.log)
  auto parts = split(last, '\t');
  if(parts.empty()) {
    // follower may log with spaces; fallback split space
    parts = split(last, ' ');
  }
  if(parts.empty()) return 0;
  try { return (uint64_t)std::stoull(parts[0]); } catch(...) { return 0; }
}

static uint64_t compute_my_last_seq(){
  // Leader stores in wal.log; followers in f<ID>.log. Check both and take max.
  uint64_t w = parse_last_seq_from_file("wal.log");
  std::string flog = "f" + std::to_string(SELF_ID) + ".log";
  uint64_t f = parse_last_seq_from_file(flog);
  return std::max(w,f);
}

// --------------- Child process utils ---------------
struct Child {
  bool running{false};
#ifdef _WIN32
  PROCESS_INFORMATION pi{};
#else
  pid_t pid{-1};
#endif
};

static bool child_alive(Child& c){
  if(!c.running) return false;
#ifdef _WIN32
  DWORD code = 0;
  if (GetExitCodeProcess(c.pi.hProcess, &code)) return code == STILL_ACTIVE;
  return false;
#else
  int status = 0;
  pid_t r = waitpid(c.pid, &status, WNOHANG);
  return r == 0;
#endif
}

static bool start_process(const std::string& cmd, Child& c){
#ifdef _WIN32
  STARTUPINFOA si{}; si.cb = sizeof(si);
  si.dwFlags = STARTF_USESHOWWINDOW;
  si.wShowWindow = SW_SHOWNORMAL;
  std::string mutableCmd = cmd;
  BOOL ok = CreateProcessA(nullptr, mutableCmd.data(), nullptr, nullptr, FALSE,
                           CREATE_NEW_CONSOLE | CREATE_NEW_PROCESS_GROUP,
                           nullptr, nullptr, &si, &c.pi);
  if(ok){ c.running=true; return true; }
  return false;
#else
  pid_t pid = fork();
  if(pid==0){ execl("/bin/sh","sh","-c",cmd.c_str(),(char*)nullptr); _exit(127); }
  else if(pid>0){ c.pid=pid; c.running=true; return true; }
  return false;
#endif
}

static void stop_process(Child& c){
  if(!c.running) return;
#ifdef _WIN32
  TerminateProcess(c.pi.hProcess, 0);
  CloseHandle(c.pi.hThread);
  CloseHandle(c.pi.hProcess);
#else
  kill(c.pid, SIGTERM);
#endif
  c.running=false;
}

// --------------- Control plane (Raft-lite) ---------------
// Messages:
// HB <term> <leaderId> <lastSeq>
// VOTE_REQ <term> <candId> <candLastSeq>
// VOTE_RESP <term> <granted 0/1>

static void handle_conn(sock_t cs){
  std::string line;
  if(!recv_line(cs,line)){ net_close(cs); return; }
  auto p = split(trim(line),' ');
  if(p.empty()){ net_close(cs); return; }

  if(p[0]=="HB" && p.size()>=4){
    int term   = std::stoi(p[1]);
    int lid    = std::stoi(p[2]);
    uint64_t lseq = (uint64_t)std::stoull(p[3]);

    if(term >= g_current_term.load()){
      g_current_term = term;
      g_voted_for    = -1;
      G.state        = (lid==SELF_ID) ? NodeState::LEADER : NodeState::FOLLOWER;
      g_leader_id    = lid;
      g_last_hb_ms   = now_ms();

      // confirm leader after stable window
      if (g_effective_leader != lid) {
        if (g_leader_seen_since_ms == 0) g_leader_seen_since_ms = now_ms();
        else if (now_ms() - g_leader_seen_since_ms >= 800) {
          g_effective_leader = lid;
          g_leader_seen_since_ms = 0;
        }
      } else {
        g_leader_seen_since_ms = 0;
      }
    }
    net_close(cs); return;
  }

  if(p[0]=="VOTE_REQ" && p.size()>=4){
    int term = std::stoi(p[1]);
    int cid  = std::stoi(p[2]);
    uint64_t cseq = (uint64_t)std::stoull(p[3]);

    bool grant=false;
    if(term > g_current_term.load()){
      g_current_term = term;
      g_voted_for    = -1;
    }
    if(term == g_current_term.load()){
      uint64_t myseq = g_my_last_seq.load();
      if( (g_voted_for.load()==-1 || g_voted_for.load()==cid) && (cseq >= myseq) ){
        g_voted_for = cid;
        grant = true;
      }
    }
    // reply
    if(auto n = getNode(cid)){
      sock_t s = tcp_connect(n->host, n->port);
      if(s!=NET_INVALID){
        send_all(s, "VOTE_RESP " + std::to_string(term) + " " + (grant?"1":"0") + "\n");
        net_close(s);
      }
    }
    net_close(cs); return;
  }

  if(p[0]=="VOTE_RESP" && p.size()>=3){
    net_close(cs); return;
  }

  net_close(cs);
}

static void listen_loop(){
  sock_t ls = tcp_listen(SELF.port);
  if(ls==NET_INVALID){ std::cerr<<"cannot bind control port "<<SELF.port<<"\n"; return; }
  while(G.alive){
    sock_t cs = tcp_accept(ls);
    if(cs!=NET_INVALID) std::thread(handle_conn, cs).detach();
  }
  net_close(ls);
}

static void leader_heartbeat(){
  while(G.alive && G.state==NodeState::LEADER){
    uint64_t lseq = compute_my_last_seq();
    g_my_last_seq = lseq;
    for(auto& n: CLUSTER){
      sock_t s = tcp_connect(n.host, n.port);
      if(s!=NET_INVALID){
        send_all(s, "HB " + std::to_string(g_current_term.load()) + " " +
                      std::to_string(SELF_ID) + " " +
                      std::to_string(lseq) + "\n");
        net_close(s);
      }
    }
    g_last_hb_ms = now_ms();
    std::this_thread::sleep_for(std::chrono::milliseconds(HEARTBEAT_INTERVAL_MS));
  }
}

static void start_election(){
  bool expected=false;
  if(!g_election_inflight.compare_exchange_strong(expected, true)) return;

  g_current_term = g_current_term.load() + 1;
  g_voted_for    = SELF_ID;
  G.state        = NodeState::CANDIDATE;
  g_my_last_seq  = compute_my_last_seq();

  log_msg(G, SELF_ID, "election term " + std::to_string(g_current_term.load()) +
                      " lastSeq=" + std::to_string(g_my_last_seq.load()));

  std::atomic<int> votes{1}; // vote for self
  // send VOTE_REQ to all
  for(auto& n: CLUSTER){
    if(n.id==SELF_ID) continue;
    sock_t s = tcp_connect(n.host, n.port);
    if(s!=NET_INVALID){
      send_all(s, "VOTE_REQ " + std::to_string(g_current_term.load()) + " " +
                    std::to_string(SELF_ID) + " " +
                    std::to_string(g_my_last_seq.load()) + "\n");
      net_close(s);
    }
  }

  // collect votes for up to ELECTION_TIMEOUT
  auto deadline = now_ms() + ELECTION_TIMEOUT_MS;
  while(now_ms() < deadline){
    for(auto& n: CLUSTER){
      if(n.id==SELF_ID) continue;
    }
    // check if we already have majority
    if (votes.load() > CLUSTER_N / 2) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  for(auto& n: CLUSTER){
    if(n.id==SELF_ID) continue;
    sock_t s = tcp_connect(n.host, n.port);
    if(s!=NET_INVALID){
      send_all(s, "VOTE_REQ " + std::to_string(g_current_term.load()) + " " +
                    std::to_string(SELF_ID) + " " +
                    std::to_string(g_my_last_seq.load()) + "\n");
      net_close(s);
    }
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(150));

  if (now_ms() - g_last_hb_ms.load() <= HEARTBEAT_TIMEOUT_MS) {
    // someone else is alive as leader; back off
    G.state = NodeState::FOLLOWER;
    g_election_inflight = false;
    return;
  }

  // become leader
  G.state = NodeState::LEADER;
  G.leaderId = SELF_ID;
  g_leader_id = SELF_ID;
  g_effective_leader = SELF_ID;
  g_leader_seen_since_ms = 0;
  log_msg(G, SELF_ID, "became LEADER term " + std::to_string(g_current_term.load()));
  std::thread(leader_heartbeat).detach();
  std::this_thread::sleep_for(std::chrono::milliseconds(800)); // stabilize
  g_election_inflight = false;
}

static void follower_monitor(){
  g_last_hb_ms = now_ms(); // bootstrap
  while(G.alive){
    g_my_last_seq = compute_my_last_seq(); // keep fresh for voting
    if(G.state != NodeState::LEADER){
      long long age = now_ms() - g_last_hb_ms.load();
      if(age > HEARTBEAT_TIMEOUT_MS && !g_election_inflight.load()){
        log_msg(G, SELF_ID, "leader timeout -> election");
        start_election();
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
  }
}

// --------------- role → child process manager ---------------
static Child child;

static void role_process_manager(){
  NodeState lastRole = NodeState::FOLLOWER;
  int       lastEff  = 0;

  while(G.alive){
    NodeState role = G.state.load();
    int eff = (role==NodeState::LEADER) ? SELF_ID : g_effective_leader;

    if(role == NodeState::LEADER){
      if(lastRole != NodeState::LEADER || !child_alive(child)){
        stop_process(child);
#ifdef _WIN32
        std::string cmd = ".\\leader.exe " + std::to_string(CLIENT_PORT) + " " +
                          std::to_string(REPL_PORT) + " wal.log 1";
#else
        std::string cmd = "./leader " + std::to_string(CLIENT_PORT) + " " +
                          std::to_string(REPL_PORT) + " wal.log 1";
#endif
        log_msg(G, SELF_ID, "spawn: " + cmd);
        start_process(cmd, child);
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
      }
    } else {
      if(eff!=0 && (lastRole==NodeState::LEADER || lastEff!=eff || !child_alive(child))){
        stop_process(child);
        const NodeInfo* L = getNode(eff);
        if(L){
          std::string flog = "f" + std::to_string(SELF_ID) + ".log";
          std::string fsnap= "f" + std::to_string(SELF_ID) + ".snap";
          uint16_t readp = FOLLOWER_READ_PORT(SELF_ID);
#ifdef _WIN32
          std::string cmd = ".\\follower.exe " + L->host + " " + std::to_string(REPL_PORT) +
                            " " + flog + " " + fsnap + " " + std::to_string(readp);
#else
          std::string cmd = "./follower " + L->host + " " + std::to_string(REPL_PORT) +
                            " " + flog + " " + fsnap + " " + std::to_string(readp);
#endif
          log_msg(G, SELF_ID, "spawn: " + cmd);
          start_process(cmd, child);
          std::this_thread::sleep_for(std::chrono::milliseconds(300));
        }
      }
    }
    lastRole = role;
    lastEff  = eff;
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
  }
  stop_process(child);
}

// ------------------------------- main -------------------------------
int main(int argc, char** argv){
  if(argc<2){ std::cerr<<"Usage: run <node_id>\n"; return 1; }
  SELF_ID = std::stoi(argv[1]);
  const NodeInfo* me = getNode(SELF_ID);
  if(!me){ std::cerr<<"bad node id\n"; return 1; }
  SELF = *me;

  log_msg(G, SELF_ID, "control port " + std::to_string(SELF.port));
  g_my_last_seq = compute_my_last_seq();

  std::thread(listen_loop).detach();
  std::thread(follower_monitor).detach();
  std::thread(role_process_manager).detach();

  // small stagger
  std::this_thread::sleep_for(std::chrono::milliseconds(400 + (SELF_ID*123)%400));
  start_election();

  while(true) std::this_thread::sleep_for(std::chrono::seconds(5));
  return 0;
}
