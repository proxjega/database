#include "common.hpp"

// Paprastas in-memory key–value žemėlapis, kuriame laikome replikacijos rezultatą
static std::unordered_map<std::string,std::string> kv;

// Paskutinio pritaikyto WAL įrašo sekos numeris (kad žinotume, nuo kur tęsti)
static uint64_t last_seq = 0;

// Informacija apie lyderį (redirect'ui iš read-only serverio)
static std::string g_leader_host;
static constexpr uint16_t LEADER_CLIENT_PORT = 7001; // port'as, kuriame lyderis aptarnauja klientus (SET/GET/DEL)

// Pritaiko vieną WAL įrašą lokaliai:
// - atnaujina kv žemėlapį
// - atnaujina last_seq
static void apply_entry(const WalEntry& entry){
  if(entry.op==Op::SET)
    kv[entry.key] = entry.value;   // SET: įrašome arba atnaujiname reikšmę
  else
    kv.erase(entry.key);           // DEL: pašaliname key, jei egzistuoja

  if (entry.seq > last_seq)
    last_seq = entry.seq;          // sekame didžiausią panaudotą seq
}

// Read-only TCP serveris, kuris:
// - leidžia daryti GET į followerį
// - bet jei bandoma rašyti ar siųsti kitą komandą – grąžina REDIRECT į lyderį
static void read_only_server(uint16_t port){
  sock_t listenSock = tcp_listen(port);
  if(listenSock==NET_INVALID){
    std::cerr<<"Follower read-only listen failed\n";
    return;
  }
  std::cout<<"Follower: read-only GET on "<<port<<"\n";

  while(true){
    sock_t clientSock = tcp_accept(listenSock);
    if(clientSock==NET_INVALID) continue;

    // Kiekvienam klientui – atskiras thread'as
    std::thread([clientSock](){
      std::string line;
      // Skaitom eilutes iš kliento
      while(recv_line(clientSock,line)){
        auto tokens = split(trim(line),' ');
        if(tokens.size()>=2 && tokens[0]=="GET"){
          // GET <key> – ieškom kv žemėlapyje
          auto it = kv.find(tokens[1]);
          if(it==kv.end())
            send_all(clientSock, "NOT_FOUND\n");
          else
            send_all(clientSock, "VALUE " + it->second + "\n");
        } else {
          // Bet kokia kita komanda (SET/DEL/...)
          // nurodom klientui, kad turi kreiptis į lyderį
          send_all(clientSock,
                   "REDIRECT " + g_leader_host + " " +
                   std::to_string(LEADER_CLIENT_PORT) + "\n");
        }
      }
      // Kai klientas atsijungia / klaida – uždarom socket'ą
      net_close(clientSock);
    }).detach();  // atskiriam thread'ą, kad tęstų savarankiškai
  }
}

// Followerio main funkcija:
// - prisijungia prie lyderio, gauna replikaciją
// - startuoja read-only GET serverį
int main(int argc, char** argv){
  if(argc<6){
    std::cerr<<"Usage: follower <leader_host> <leader_follower_port> <wal_path> <snapshot_path> <read_port>\n";
    return 1;
  }

  // argv[1] – lyderio host'as (IP arba hostname), kur prisijungsim replikacijai
  g_leader_host        = argv[1];

  // argv[2] – lyderio replication port (7002)
  uint16_t leader_repl_port = (uint16_t)std::stoi(argv[2]);

  // argv[3] – followerio WAL failo kelias (čia saugom replikacijos logą)
  std::string wal_path = argv[3];

  // argv[4] – snapshot kelias
  std::string snapshot_path = argv[4];

  // argv[5] – read-only serverio portas (iš jo klientai gali daryti GET tiesiai į followerį)
  uint16_t read_only_port = (uint16_t)std::stoi(argv[5]);

  // 1) Atkuriam būseną iš savo WAL failo (jei followeris jau buvo paleistas anksčiau)
  std::vector<WalEntry> previous_entries;
  wal_load(wal_path, previous_entries);
  for(const auto& entry : previous_entries)
    apply_entry(entry); // panaudojam visus ankstesnius SET/DEL, atstatom kv ir last_seq

  // 2) Paleidžiam read-only GET serverį atskirame thread'e
  std::thread read_thread(read_only_server, read_only_port);

  // 3) Nuolatinis replikacijos ciklas:
  //    - jungiamės prie lyderio
  //    - sakom HELLO <last_seq>
  //    - priimam WRITE/DELETE įrašus ir ACK'inam
  while(true){
    sock_t replSock = tcp_connect(g_leader_host, leader_repl_port);
    if(replSock==NET_INVALID){
      // jei nepavyko prisijungti – palaukiam sekundę ir bandome vėl
      std::this_thread::sleep_for(std::chrono::seconds(1));
      continue;
    }

    // Nusiunčiam lyderiui, iki kurio seq jau esam atsilikę:
    // HELLO <last_seq>
    send_all(replSock, "HELLO " + std::to_string(last_seq) + "\n");

    // Atidarom WAL failą append režimu – nauji įrašai bus prirašomi gale
    std::ofstream wal_out(wal_path, std::ios::app);

    std::string line;
    // Skaitom replikacijos srautą iš lyderio
    while(recv_line(replSock,line)){
      auto tokens = split(trim(line),' ');

      if(tokens.size()>=4 && tokens[0]=="WRITE"){
        // WRITE seq key val
        WalEntry entry;
        entry.seq   = std::stoull(tokens[1]);
        entry.op    = Op::SET;
        entry.key   = tokens[2];
        entry.value = tokens[3];

        // Tik pritaikom, jei seq naujesnis už mūsų last_seq
        if(entry.seq > last_seq){
          apply_entry(entry);      // atnaujinam kv ir last_seq
          wal_append(wal_out, entry); // išsaugom į savo WAL
        }

        // Patvirtinam lyderiui, kad esam pritaikę iki šio seq
        send_all(replSock, "ACK " + std::to_string(entry.seq) + "\n");
      }
      else if(tokens.size()>=3 && tokens[0]=="DELETE"){
        // DELETE seq key
        WalEntry entry;
        entry.seq = std::stoull(tokens[1]);
        entry.op  = Op::DEL;
        entry.key = tokens[2];

        if(entry.seq > last_seq){
          apply_entry(entry);
          wal_append(wal_out, entry);
        }

        send_all(replSock, "ACK " + std::to_string(entry.seq) + "\n");
      }
      // jei ateitų kažkas kito – tiesiog ignoruojam
    }

    // jei šita vieta pasiekiama, reiškia nutrūko ryšys su lyderiu
    net_close(replSock);

    // šiek tiek palaukiam ir bandome prisijungti vėl (while(true) ciklas)
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }

  // Šitos eilutės realiai niekada nepasieksim (hopefully), bet formaliai – joininam read_only thread'ą
  read_thread.join();
  return 0;
}
