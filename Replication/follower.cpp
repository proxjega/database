#include "common.hpp"

// Paprastas in-memory key–value žemėlapis, kuriame laikome replikacijos rezultatą.
static std::unordered_map<std::string, std::string> kv_store;

// Paskutinio pritaikyto WAL įrašo sekos numeris.
// Naudojamas tiek HELLO siunčiant leader'iui, tiek duplikatams filtruoti.
static uint64_t last_applied_seq = 0;

// Informacija apie leader'į (redirect'ui iš read-only serverio).
static std::string g_leader_host;
static constexpr uint16_t LEADER_CLIENT_PORT = 7001; // leader klientų API (SET/GET/DEL)

// Pritaiko vieną WAL įrašą lokaliai ir atnaujina last_applied_seq.
static void apply_entry(const WalEntry& entry) {
    if (entry.op == Op::SET)
        kv_store[entry.key] = entry.value;  // SET: įrašome arba atnaujiname reikšmę
    else
        kv_store.erase(entry.key);          // DEL: pašaliname key, jei egzistuoja

    if (entry.seq > last_applied_seq)
        last_applied_seq = entry.seq;       // sekame didžiausią pritaikytą seq
}

// Read-only TCP serveris:
//
// - leidžia daryti GET į followerį;
// - bet bet kokią kitą komandą redirect'ina į leader'į
//   (klientui nurodom leader host + portą).
static void read_only_server(uint16_t port) {
    sock_t listen_socket = tcp_listen(port);
    if (listen_socket == NET_INVALID) {
        std::cerr << "Follower read-only listen failed\n";
        return;
    }

    std::cout << "Follower: read-only GET on " << port << "\n";

    while (true) {
        sock_t client_socket = tcp_accept(listen_socket);
        if (client_socket == NET_INVALID) continue;

        std::thread([client_socket]() {
            std::string line;

            while (recv_line(client_socket, line)) {
                auto tokens = split(trim(line), ' ');

                // GET <key>
                if (tokens.size() >= 2 && tokens[0] == "GET") {
                    auto it = kv_store.find(tokens[1]);
                    if (it == kv_store.end())
                        send_all(client_socket, "NOT_FOUND\n");
                    else
                        send_all(client_socket, "VALUE " + it->second + "\n");
                } else {
                    // Bet kokia kita komanda – nurodom kreiptis į leader'į.
                    send_all(
                        client_socket,
                        "REDIRECT " + g_leader_host + " " +
                        std::to_string(LEADER_CLIENT_PORT) + "\n"
                    );
                }
            }

            net_close(client_socket);
        }).detach();
    }
}

// Follower main:
//
// - atstato būseną iš savo WAL;
// - paleidžia read-only GET serverį;
// - cikle jungiasi prie leader'io ir priima replikaciją.
int main(int argc, char** argv) {
    if (argc < 6) {
        std::cerr << "Usage: follower <leader_host> <leader_follower_port> "
                     "<wal_path> <snapshot_path> <read_port>\n";
        return 1;
    }

    // argv[1] – leader host (IP / hostname), prie kurio jungiamės replikacijai.
    g_leader_host = argv[1];

    // argv[2] – leader replication port (pvz., 7002).
    uint16_t leader_repl_port = static_cast<uint16_t>(std::stoi(argv[2]));

    // argv[3] – follower WAL failo kelias (čia saugom replikacijos logą).
    std::string wal_path = argv[3];

    // argv[4] – snapshot kelias (šioje versijoje nenaudojamas, rezervuota plėtrai).
    std::string snapshot_path = argv[4];
    (void)snapshot_path; // kad nekeltų „unused“ warning'o.

    // argv[5] – read-only serverio portas (GET per followerį).
    uint16_t read_only_port = static_cast<uint16_t>(std::stoi(argv[5]));

    // 1) Atkuriam būseną iš savo WAL (jeigu followeris jau buvo paleistas anksčiau).
    std::vector<WalEntry> previous_entries;
    wal_load(wal_path, previous_entries);

    for (const auto& entry : previous_entries)
        apply_entry(entry);  // atstatom kv_store ir last_applied_seq

    // 2) Paleidžiam read-only GET serverį atskirame threade.
    std::thread read_thread(read_only_server, read_only_port);

    // 3) Nuolatinis replikacijos ciklas:
    //    - jungiamės prie leader'io;
    //    - siunčiam HELLO <last_applied_seq>;
    //    - priimam WRITE/DELETE ir ACK'inam.
    while (true) {
        sock_t repl_socket = tcp_connect(g_leader_host, leader_repl_port);
        if (repl_socket == NET_INVALID) {
            // nepavyko prisijungti – palaukiam ir bandome iš naujo
            std::this_thread::sleep_for(std::chrono::seconds(1));
            continue;
        }

        // HELLO <last_applied_seq> – pasakome, iki kurio seq esame sinchronizuoti.
        send_all(repl_socket, "HELLO " + std::to_string(last_applied_seq) + "\n");

        // Atidarom WAL append režimu – nauji įrašai bus prirašomi gale.
        std::ofstream wal_out(wal_path, std::ios::app);

        std::string line;
        // Skaitom replikacijos srautą iš leader'io.
        while (recv_line(repl_socket, line)) {
            auto tokens = split(trim(line), ' ');

            // WRITE seq key val
            if (tokens.size() >= 4 && tokens[0] == "WRITE") {
                WalEntry entry;
                entry.seq   = std::stoull(tokens[1]);
                entry.op    = Op::SET;
                entry.key   = tokens[2];
                entry.value = tokens[3];

                // IDOMPOTENTIŠKUMO APSAUGA:
                // jei seq jau matytas ar senesnis – neberašom į kv/WAL,
                // bet ACK vis tiek siunčiam.
                if (entry.seq > last_applied_seq) {
                    apply_entry(entry);
                    wal_append(wal_out, entry);
                }

                send_all(repl_socket, "ACK " + std::to_string(entry.seq) + "\n");
            }
            // DELETE seq key
            else if (tokens.size() >= 3 && tokens[0] == "DELETE") {
                WalEntry entry;
                entry.seq = std::stoull(tokens[1]);
                entry.op  = Op::DEL;
                entry.key = tokens[2];

                if (entry.seq > last_applied_seq) {
                    apply_entry(entry);
                    wal_append(wal_out, entry);
                }

                send_all(repl_socket, "ACK " + std::to_string(entry.seq) + "\n");
            }
            // kiti pranešimai ignoruojami tyliai
        }

        // Jei šita vieta pasiekta – nutrūko ryšys su leader'iu.
        net_close(repl_socket);

        // Šiek tiek palaukiam ir bandome prisijungti vėl (while(true) ciklas).
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    // Teoriškai niekada nepasiekiama, bet formaliai – joininam read_only thread'ą.
    read_thread.join();
    return 0;
}
