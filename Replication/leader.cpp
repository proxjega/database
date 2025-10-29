#include "common.hpp"

struct FollowerConn {
  sock_t s{NET_INVALID};
  uint64_t acks_upto{0};
  bool alive{true};
};

static std::mutex mtx;
static std::condition_variable cv;
static std::unordered_map<std::string,std::string> kv;
static std::vector<WalEntry> logbuf;
static std::vector<std::shared_ptr<FollowerConn>> followers;
static std::atomic<uint64_t> next_seq{1};
static int REQUIRED_ACKS = 0;

static void broadcast_entry(const WalEntry& e){
  std::string msg;
  if(e.op==Op::SET)
    msg = "WRITE "  + std::to_string(e.seq) + " " + e.key + " " + e.value + "\n";
  else
    msg = "DELETE " + std::to_string(e.seq) + " " + e.key + "\n";

  for(auto& f: followers){
    if(!f->alive) continue;
    if(!send_all(f->s, msg)) f->alive=false;
  }
}

static void follower_thread(std::shared_ptr<FollowerConn> f){
  std::string line;
  if(!recv_line(f->s,line)){ f->alive=false; return; }
  auto parts = split(line,' ');
  if(parts.size()!=2 || parts[0]!="HELLO"){ f->alive=false; return; }
  uint64_t last = std::stoull(parts[1]);

  { 
    // backlog
    std::unique_lock<std::mutex> lk(mtx);
    for(const auto& e: logbuf){
      if(e.seq>last){
        std::string msg = (e.op==Op::SET)
          ? ("WRITE "  + std::to_string(e.seq) + " " + e.key + " " + e.value + "\n")
          : ("DELETE " + std::to_string(e.seq) + " " + e.key + "\n");
        if(!send_all(f->s,msg)){ f->alive=false; return; }
      }
    }
  }

  while(f->alive){
    std::string l;
    if(!recv_line(f->s,l)){ f->alive=false; break; }
    auto p = split(l,' ');
    if(p.size()==2 && p[0]=="ACK"){
      uint64_t s = std::stoull(p[1]);
      f->acks_upto = std::max(f->acks_upto, s);
      cv.notify_all();
    }
  }
}

static size_t count_acks(uint64_t seq){
  size_t c=0;
  for(auto& f: followers) if(f->alive && f->acks_upto>=seq) ++c;
  return c;
}

static void accept_followers(uint16_t port){
  sock_t ls = tcp_listen(port);
  if(ls==NET_INVALID){ std::cerr<<"Follower listen failed\n"; return; }
  std::cout<<"Leader: listening followers on "<<port<<"\n";
  while(true){
    sock_t s = tcp_accept(ls);
    if(s==NET_INVALID) continue;
    auto f = std::make_shared<FollowerConn>(); f->s=s;
    { std::lock_guard<std::mutex> lk(mtx); followers.push_back(f); }
    std::thread(follower_thread, f).detach();
  }
}

static void serve_clients(uint16_t port, const std::string& wal_path){
  sock_t ls = tcp_listen(port);
  if(ls==NET_INVALID){ std::cerr<<"Client listen failed\n"; return; }
  std::ofstream wal(wal_path, std::ios::app);
  std::cout<<"Leader: listening clients on "<<port<<"\n";

  while(true){
    sock_t cs = tcp_accept(ls);
    if(cs==NET_INVALID) continue;
    std::thread([cs,&wal](){
      std::string line;
      while(recv_line(cs,line)){
        auto p = split(trim(line),' ');
        if(p.empty()) continue;

        if( (p[0]=="PUT" || p[0]=="SET") && p.size()>=3 ){
          WalEntry e;
          { 
            // apply
            std::lock_guard<std::mutex> lk(mtx);
            e.seq = next_seq++;
            e.op  = Op::SET;
            e.key = p[1];
            e.value = p[2];
            kv[e.key]=e.value;
            logbuf.push_back(e);
            wal_append(wal,e);
            broadcast_entry(e);
          }
          if(REQUIRED_ACKS>0){
            std::unique_lock<std::mutex> lk(mtx);
            cv.wait_for(lk, std::chrono::seconds(3), [&]{ return count_acks(e.seq) >= (size_t)REQUIRED_ACKS; });
          }
          send_all(cs, "OK " + std::to_string(e.seq) + "\n");
        }
        else if(p[0]=="DEL" && p.size()>=2){
          WalEntry e;
          {
            std::lock_guard<std::mutex> lk(mtx);
            e.seq = next_seq++;
            e.op  = Op::DEL;
            e.key = p[1];
            kv.erase(e.key);
            logbuf.push_back(e);
            wal_append(wal,e);
            broadcast_entry(e);
          }
          if(REQUIRED_ACKS>0){
            std::unique_lock<std::mutex> lk(mtx);
            cv.wait_for(lk, std::chrono::seconds(3), [&]{ return count_acks(e.seq) >= (size_t)REQUIRED_ACKS; });
          }
          send_all(cs, "OK " + std::to_string(e.seq) + "\n");
        }
        else if(p[0]=="GET" && p.size()>=2){
          std::lock_guard<std::mutex> lk(mtx);
          auto it = kv.find(p[1]);
          if(it==kv.end()) send_all(cs, "NOT_FOUND\n");
          else send_all(cs, "VALUE " + it->second + "\n");
        }
        else {
          send_all(cs, "ERR usage: SET <k> <v> | GET <k> | DEL <k>\n");
        }
      }
      net_close(cs);
    }).detach();
  }
}

int main(int argc, char** argv){
  if(argc<5){
    std::cerr<<"Usage: leader <client_port> <follower_port> <wal_path> <required_acks>\n";
    return 1;
  }
  uint16_t client_port   = (uint16_t)std::stoi(argv[1]);
  uint16_t follower_port = (uint16_t)std::stoi(argv[2]);
  std::string wal_path   = argv[3];
  REQUIRED_ACKS          = std::stoi(argv[4]);

  std::vector<WalEntry> prev; wal_load(wal_path, prev);
  { std::lock_guard<std::mutex> lk(mtx);
    uint64_t maxseq=0;
    for(auto& e: prev){
      if(e.op==Op::SET) kv[e.key]=e.value;
      else              kv.erase(e.key);
      logbuf.push_back(e);
      maxseq = std::max(maxseq, e.seq);
    }
    next_seq = maxseq+1;
  }

  std::thread t1(accept_followers, follower_port);
  serve_clients(client_port, wal_path);
  t1.join();
  return 0;
}
