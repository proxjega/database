#include "common.hpp"

static bool do_request_follow_redirect(const std::string& host, uint16_t port,
                                       const std::string& payload) {
  // 1st try
  sock_t s = tcp_connect(host, port);
  if (s == NET_INVALID) { std::cerr << "ERR_CONNECT\n"; return false; }
  send_all(s, payload + "\n");
  std::string line;
  if (!recv_line(s, line)) { net_close(s); std::cerr << "ERR_NO_REPLY\n"; return false; }
  auto p = split(trim(line), ' ');
  if (!p.empty() && p[0] == "REDIRECT" && p.size() >= 3) {
    // follow redirect once
    std::string h = p[1];
    uint16_t newPort = (uint16_t)std::stoi(p[2]);
    net_close(s);
    sock_t s2 = tcp_connect(h, newPort);
    if (s2 == NET_INVALID) { std::cerr << "ERR_CONNECT\n"; return false; }
    send_all(s2, payload + "\n");
    std::string out2;
    if (!recv_line(s2, out2)) { net_close(s2); std::cerr << "ERR_NO_REPLY\n"; return false; }
    std::cout << out2 << "\n";
    net_close(s2);
    return true;
  } else {
    std::cout << line << "\n";
    net_close(s);
    return true;
  }
}

static bool split_host_port(const std::string& hp, std::string& h, uint16_t& p){
  auto pos = hp.find(':'); if(pos==std::string::npos) return false;
  h = hp.substr(0,pos); p = (uint16_t)std::stoi(hp.substr(pos+1));
  return true;
}

int main(int argc, char** argv){
  if(argc < 4){
    std::cerr<<"Usage:\n"
             <<"  client <leader_host> <leader_client_port> GET <k>\n"
             <<"  client <leader_host> <leader_client_port> SET <k> <v>\n"
             <<"  client <leader_host> <leader_client_port> DEL <k>\n"
             <<"  client <leader_host> <leader_client_port> GETFF <k> <follower_host:port>\n"
             <<"  client <leader_host> <leader_client_port> GETFB <k> <follower_host:port>\n";
    return 1;
  }
  std::string leader_host = argv[1];
  uint16_t leader_port    = (uint16_t)std::stoi(argv[2]);
  std::string cmd = argv[3];

  if(cmd=="GET" && argc>=5){
    return do_request_follow_redirect(leader_host, leader_port,
                                      "GET " + std::string(argv[4])) ? 0 : 1;
  }
  else if((cmd=="SET" || cmd=="PUT") && argc>=6){
    return do_request_follow_redirect(leader_host, leader_port,
                                      "SET " + std::string(argv[4]) + " " + std::string(argv[5])) ? 0 : 1;
  }
  else if(cmd=="DEL" && argc>=5){
    return do_request_follow_redirect(leader_host, leader_port,
                                      "DEL " + std::string(argv[4])) ? 0 : 1;
  }
  else if(cmd=="GETFF" && argc>=6){
    std::string h; uint16_t p;
    if(!split_host_port(argv[5], h, p)){ std::cerr<<"bad follower host:port\n"; return 1; }
    return do_request_follow_redirect(h, p, "GET " + std::string(argv[4])) ? 0 : 1;
  }
  else if(cmd=="GETFB" && argc>=6){
    std::string h; uint16_t p;
    if(!split_host_port(argv[5], h, p)){ std::cerr<<"bad follower host:port\n"; return 1; }
    // try follower first, if NOT_FOUND or error -> retry leader
    sock_t s = tcp_connect(h, p);
    if (s != NET_INVALID) {
      send_all(s, "GET " + std::string(argv[4]) + "\n");
      std::string line;
      if (recv_line(s, line)) {
        net_close(s);
        if (line=="NOT_FOUND") {
          return do_request_follow_redirect(leader_host, leader_port,
                                            "GET " + std::string(argv[4])) ? 0 : 1;
        } else if (line.rfind("REDIRECT ",0)==0) {
          // follow follower's redirect just in case
          auto parts = split(trim(line),' ');
          if (parts.size()>=3) {
            std::string nh = parts[1]; uint16_t np=(uint16_t)std::stoi(parts[2]);
            return do_request_follow_redirect(nh, np, "GET " + std::string(argv[4])) ? 0 : 1;
          }
        } else {
          std::cout<<line<<"\n"; return 0;
        }
      } else {
        net_close(s);
      }
    }
    // fallback to leader on connect/read error
    return do_request_follow_redirect(leader_host, leader_port,
                                      "GET " + std::string(argv[4])) ? 0 : 1;
  }

  std::cerr<<"Bad args. See usage above.\n";
  return 1;
}
