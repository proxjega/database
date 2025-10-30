#include "common.hpp"
#include <unordered_map>
#include <fstream>
#include <thread>
#include <atomic>
#include <iostream>

static std::unordered_map<std::string,std::string> kv;
static uint64_t last_seq = 0;

// leader info (for REDIRECT)
static std::string g_leader_host;
static constexpr uint16_t LEADER_CLIENT_PORT = 7001;

static void apply_entry(const WalEntry& e){
  if(e.op==Op::SET) kv[e.key]=e.value;
  else              kv.erase(e.key);
  if (e.seq > last_seq) last_seq = e.seq;
}

static void read_only_server(uint16_t port){
  sock_t ls = tcp_listen(port);
  if(ls==NET_INVALID){ std::cerr<<"Follower read-only listen failed\n"; return; }
  std::cout<<"Follower: read-only GET on "<<port<<"\n";
  while(true){
    sock_t cs = tcp_accept(ls);
    if(cs==NET_INVALID) continue;

    std::thread([cs](){
      std::string line;
      while(recv_line(cs,line)){
        auto p = split(trim(line),' ');
        if(p.size()>=2 && p[0]=="GET"){
          auto it = kv.find(p[1]);
          if(it==kv.end()) send_all(cs, "NOT_FOUND\n");
          else             send_all(cs, "VALUE " + it->second + "\n");
        } else {
          // any write-ish or unknown cmd -> tell client where to go
          send_all(cs, "REDIRECT " + g_leader_host + " " + std::to_string(LEADER_CLIENT_PORT) + "\n");
        }
      }
      net_close(cs);
    }).detach();
  }
}

int main(int argc, char** argv){
  if(argc<6){
    std::cerr<<"Usage: follower <leader_host> <leader_follower_port> <wal_path> <snapshot_path> <read_port>\n";
    return 1;
  }
  g_leader_host        = argv[1];
  uint16_t lport       = (uint16_t)std::stoi(argv[2]);  // leader's replication port (7002)
  std::string walp     = argv[3];
  std::string snap     = argv[4];
  uint16_t readp       = (uint16_t)std::stoi(argv[5]);

  // Load our previous WAL to restore state
  std::vector<WalEntry> prev; wal_load(walp, prev);
  for(const auto& e: prev) apply_entry(e);

  // Start read-only GET server
  std::thread t_read(read_only_server, readp);

  // Replication loop
  while(true){
    sock_t s = tcp_connect(g_leader_host, lport);
    if(s==NET_INVALID){ std::this_thread::sleep_for(std::chrono::seconds(1)); continue; }

    // tell leader what we already have
    send_all(s, "HELLO " + std::to_string(last_seq) + "\n");
    std::ofstream wal(walp, std::ios::app);

    std::string line;
    while(recv_line(s,line)){
      auto p = split(trim(line),' ');
      if(p.size()>=4 && p[0]=="WRITE"){           // WRITE seq key val
        WalEntry e; e.seq=std::stoull(p[1]); e.op=Op::SET; e.key=p[2]; e.value=p[3];
        if(e.seq>last_seq){ apply_entry(e); wal_append(wal,e); }
        send_all(s, "ACK " + std::to_string(e.seq) + "\n");
      } else if(p.size()>=3 && p[0]=="DELETE"){   // DELETE seq key
        WalEntry e; e.seq=std::stoull(p[1]); e.op=Op::DEL; e.key=p[2];
        if(e.seq>last_seq){ apply_entry(e); wal_append(wal,e); }
        send_all(s, "ACK " + std::to_string(e.seq) + "\n");
      }
    }
    net_close(s);
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }

  t_read.join();
  return 0;
}
