#include "common.hpp"
#include <unordered_set>
#include <unordered_map>
#include <thread>
#include <chrono>
#include <stdexcept>

// Paprastas logger'is šitam faile – tik su "Follower" prefiksu, kad atskirt nuo kitų komponentų.
static void flog(LogLevel lvl, const std::string& msg) {
    const char* tag = nullptr;
    switch (lvl) {
        case LogLevel::DEBUG: tag = "DEBUG"; break;
        case LogLevel::INFO:  tag = "INFO";  break;
        case LogLevel::WARN:  tag = "WARN";  break;
        case LogLevel::ERROR: tag = "ERROR"; break;
        default:              tag = "LOG";   break;
    }
    std::cerr << "[Follower][" << tag << "] " << msg << "\n";
}

/* ========= In-memory state ========= */

// Paprastas key-value žemėlapis, kuriame laikom lyderio replikacijos rezultatą.
static std::unordered_map<std::string, std::string> kv_store;

// Paskutinio pritaikyto WAL įrašo sekos numeris.
// Naudojamas HELLO žinutėje, kad leader'is žinotų nuo kurio seq siųsti toliau.
static uint64_t last_applied_seq = 0;

// Rinkinys su visais jau pritaikytais seq numeriais (naudojam greitam patikrinimui).
static std::unordered_set<uint64_t> applied_seq_numbers;

// Žemėlapis seq -> WalEntry. Saugo pilną įrašo turinį, kad būtų galima aptikti konfliktus
// (kai leader'is su tuo pačiu seq atsiunčia kažką kitą).
static std::unordered_map<uint64_t, WalEntry> applied_entries_by_seq;

// Informacija apie leader'į (naudojama REDIRECT atsakymui read-only serveryje).
static std::string g_leader_host;
static constexpr uint16_t LEADER_CLIENT_PORT = 7001; // leader'io kliento API (SET/GET/DEL) portas

/* ========= Helpers ========= */

// Pritaiko vieną WAL įrašą followerio in-memory būsenai.
static void apply_entry(const WalEntry& entry) {
    // 1) pritaikom operaciją key-value žemėlapiui
    if (entry.op == Op::SET)
        kv_store[entry.key] = entry.value;  // SET – įrašom / atnaujinam reikšmę
    else
        kv_store.erase(entry.key);          // DEL – ištrinam raktą

    // 2) atnaujinam paskutinį seq, jei šis įrašas naujesnis
    if (entry.seq > last_applied_seq)
        last_applied_seq = entry.seq;

    // 3) įsimenam, kad šitas seq jau pritaikytas ir ką tiksliai padarėm
    applied_seq_numbers.insert(entry.seq);
    applied_entries_by_seq[entry.seq] = entry;
}

/* ========= Read-only server (GET / REDIRECT) ========= */

// Paprastas read-only serveris, kuris:
// - aptarnauja GET <key> užklausas iš lokalaus kv_store
// - bet kokiai kitai komandai atsako REDIRECT į leader'į.
static void read_only_server(uint16_t port) {
    try {
        // Startuojam TCP klausymą nurodytame porte
        sock_t listen_socket = tcp_listen(port);
        if (listen_socket == NET_INVALID) {
            flog(LogLevel::ERROR, "read-only listen failed on port " + std::to_string(port));
            return;
        }

        flog(LogLevel::INFO, "read-only GET on port " + std::to_string(port));

        // Begalinis ciklas – laukiam naujų klientų
        while (true) {
            sock_t client_socket = tcp_accept(listen_socket);
            if (client_socket == NET_INVALID) continue;

            // Nedidelis timeout, kad klientas neužkabintų ryšio amžinai
            set_socket_timeouts(client_socket, 5000);

            // Kiekvieną klientą aptarnaujam atskiram threade
            std::thread([client_socket]() {
                try {
                    std::string line;

                    // Skaitom eiles iš kliento, kol jis kalba arba nutrūksta ryšys
                    while (recv_line(client_socket, line)) {
                        auto tokens = split(trim(line), ' ');
                        if (tokens.empty()) continue;

                        // Komanda: GET <key>
                        if (tokens.size() >= 2 && tokens[0] == "GET") {
                            const std::string& key = tokens[1];
                            auto it = kv_store.find(key);
                            if (it == kv_store.end())
                                send_all(client_socket, "NOT_FOUND\n");
                            else
                                send_all(client_socket, "VALUE " + it->second + "\n");
                        } else {
                            // Bet kokia kita komanda – redirect į leader'į.
                            // Taip užtikrinam, kad followeris nekeičia būsenos.
                            send_all(
                                client_socket,
                                "REDIRECT " + g_leader_host + " " +
                                std::to_string(LEADER_CLIENT_PORT) + "\n"
                            );
                        }
                    }
                } catch (const std::exception& ex) {
                    flog(LogLevel::ERROR,
                         std::string("exception in read_only client thread: ") + ex.what());
                } catch (...) {
                    flog(LogLevel::ERROR, "unknown exception in read_only client thread");
                }
                // Uždarom kliento socket'ą, kai baigėm
                net_close(client_socket);
            }).detach(); // thread'ą „atsiejame“, kad gyventų savarankiškai
        }

        // (teoriškai niekad nepasieksim šios vietos)
        net_close(listen_socket);
    } catch (const std::exception& ex) {
        flog(LogLevel::ERROR, std::string("exception in read_only_server: ") + ex.what());
    } catch (...) {
        flog(LogLevel::ERROR, "unknown exception in read_only_server");
    }
}

/* ========= main ========= */

int main(int argc, char** argv) {
    try {
        // Tikrinam argumentų skaičių:
        // follower <leader_host> <leader_follower_port> <wal_path> <snapshot_path> <read_port> [node_id]
        if (argc < 6) {
            std::cerr << "Usage: follower <leader_host> <leader_follower_port> "
                         "<wal_path> <snapshot_path> <read_port> [node_id]\n";
            return 1;
        }

        // --- argument validation ---

        // Leader'io host vardas / IP
        g_leader_host = argv[1];
        if (g_leader_host.empty()) {
            flog(LogLevel::ERROR, "leader_host is empty");
            return 1;
        }

        // Leader'io replikacijos portas (tas, kuriuo followeriai jungiasi)
        int portTmp = 0;
        try {
            portTmp = std::stoi(argv[2]);
        } catch (...) {
            flog(LogLevel::ERROR,
                 std::string("invalid leader_follower_port: ") + argv[2]);
            return 1;
        }
        if (portTmp <= 0 || portTmp > 65535) {
            flog(LogLevel::ERROR,
                 "leader_follower_port out of range: " + std::to_string(portTmp));
            return 1;
        }
        uint16_t leader_repl_port = static_cast<uint16_t>(portTmp);

        // Kelias iki lokalaus WAL failo, kuriame followeris saugo gautus įrašus
        std::string wal_path = argv[3];
        if (wal_path.empty()) {
            flog(LogLevel::ERROR, "wal_path is empty");
            return 1;
        }

        // Snapshot'o kelias (šiam variante nenaudojam, bet galim ateityje)
        std::string snapshot_path = argv[4];
        (void)snapshot_path; // rezervuota ateičiai

        // Read-only serverio portas (GET/REDIRECT API)
        int roTmp = 0;
        try {
            roTmp = std::stoi(argv[5]);
        } catch (...) {
            flog(LogLevel::ERROR,
                 std::string("invalid read_only_port: ") + argv[5]);
            return 1;
        }
        if (roTmp <= 0 || roTmp > 65535) {
            flog(LogLevel::ERROR,
                 "read_only_port out of range: " + std::to_string(roTmp));
            return 1;
        }
        uint16_t read_only_port = static_cast<uint16_t>(roTmp);

        // optional node_id iš 6 argumento (jei perduodamas – galima naudoti logams/debug'ui)
        int node_id = (argc >= 7) ? std::stoi(argv[6]) : -1;
        (void)node_id; // kol kas nenaudojam, bet gali prireikti ateity

        flog(LogLevel::INFO,
             "starting follower; leader=" + g_leader_host +
             ":" + std::to_string(leader_repl_port) +
             " wal=" + wal_path +
             " read_port=" + std::to_string(read_only_port));

        // --- 1) Atkuriam būseną iš WAL failo ---

        std::vector<WalEntry> previous_entries;
        // Bandome užkrauti visus ankstesnius įrašus iš WAL failo
        if (!wal_load(wal_path, previous_entries)) {
            flog(LogLevel::WARN,
                 "wal_load failed or empty for path=" + wal_path +
                 " (starting with empty state)");
        }

        // Kiekvieną rastą WAL įrašą pritaikom vietiniam kv_store
        for (const auto& entry : previous_entries) {
            apply_entry(entry);  // kv_store + last_applied_seq + seq map
        }

        flog(LogLevel::INFO,
             "WAL loaded: " + std::to_string(previous_entries.size()) +
             " entries, last_applied_seq=" + std::to_string(last_applied_seq));

        // --- 2) Startuojam read-only GET serverį atskiram threade ---
        std::thread read_thread(read_only_server, read_only_port);
        read_thread.detach();           // tegu serveris gyvena iki kol procesas užsidarys

        // --- 3) Replikacijos ciklas su exponential backoff + „suicide“ mechanizmu ---

        // Bazinis ir maksimalus atsijungimų „backoff“ (kiek miegam prieš kitą bandymą)
        const int BASE_BACKOFF_MS           = 1000;
        const int MAX_BACKOFF_MS            = 30000;

        // Maksimalus iš eilės nesėkmių skaičius, po kurio followeris nukillinimas (Profilaktika + spam reduce)
        const int MAX_FAILURES_BEFORE_EXIT  = 5;

        int backoff_ms   = BASE_BACKOFF_MS;
        int failure_count = 0;

        // Pagrindinis replikacijos „amžinas“ ciklas
        while (true) {
            flog(LogLevel::INFO,
                 "trying to connect to leader " + g_leader_host +
                 ":" + std::to_string(leader_repl_port));

            // Bandom užmegzti TCP ryšį su leader'iu
            sock_t repl_socket = tcp_connect(g_leader_host, leader_repl_port);
            if (repl_socket == NET_INVALID) {
                // Nepavyko prisijungti – didinam failure counter'į
                failure_count++;
                flog(LogLevel::WARN,
                     "connect to leader failed, sleeping " +
                     std::to_string(backoff_ms) + " ms before retry "
                     "(failure_count=" + std::to_string(failure_count) + ")");

                // Jei jau per daug nesėkmių – išeinam su klaidos kodu, kad
                // supervisoris galėtų mus perstartuoti „švariai“.
                if (failure_count >= MAX_FAILURES_BEFORE_EXIT) {
                    flog(LogLevel::ERROR,
                         "too many failures to reach leader, exiting so supervisor can respawn me");
                    return 2; // „suicide“ – leisk run.cpp mus spawn'int iš naujo
                }

                // Palaukiam ir didinam backoff'ą eksponentiškai
                std::this_thread::sleep_for(std::chrono::milliseconds(backoff_ms));
                backoff_ms = std::min(backoff_ms * 2, MAX_BACKOFF_MS);
                continue;
            }

            // Jei prisijungėm – resetinam backoff'ą į bazinį
            backoff_ms = BASE_BACKOFF_MS;
            set_socket_timeouts(repl_socket, 5000);

            // HELLO – nusiunčiam leader'iui savo paskutinį pritaikytą seq,
            // kad jis galėtų siųsti tik naujus įrašus.
            std::string hello = "HELLO " + std::to_string(last_applied_seq) + "\n";
            if (!send_all(repl_socket, hello)) {
                failure_count++;
                flog(LogLevel::WARN,
                     "failed to send HELLO, closing socket (failure_count=" +
                     std::to_string(failure_count) + ")");

                net_close(repl_socket);

                if (failure_count >= MAX_FAILURES_BEFORE_EXIT) {
                    flog(LogLevel::ERROR,
                         "too many failures talking to leader (HELLO), exiting");
                    return 2;
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(backoff_ms));
                backoff_ms = std::min(backoff_ms * 2, MAX_BACKOFF_MS);
                continue;
            }

            flog(LogLevel::INFO,
                 "sent HELLO " + std::to_string(last_applied_seq));

            // Atidarom WAL failą pildymui – visus naujus įrašus rašysim į jį.
            std::ofstream wal_out(wal_path, std::ios::app);
            if (!wal_out.is_open()) {
                failure_count++;
                flog(LogLevel::ERROR,
                     "failed to open WAL file for append: " + wal_path +
                     " (failure_count=" + std::to_string(failure_count) + ")");
                net_close(repl_socket);

                if (failure_count >= MAX_FAILURES_BEFORE_EXIT) {
                    flog(LogLevel::ERROR,
                         "too many failures opening WAL, exiting");
                    return 2;
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(backoff_ms));
                backoff_ms = std::min(backoff_ms * 2, MAX_BACKOFF_MS);
                continue;
            }

            // Flag'as, ar šito prisijungimo metu gavome bent vieną WRITE/DELETE.
            bool got_any_replication = false;

            // --- Replikacijos skaitymo ciklas ---
            std::string line;
            while (true) {
                // Skaitom eilutes iš leader'io (WRITE/DELETE komandas)
                if (!recv_line(repl_socket, line)) {
                    // čia timeout / disconnect / klaida – išeinam iš ciklo
                    break;
                }

                try {
                    auto tokens = split(trim(line), ' ');
                    if (tokens.empty()) continue;

                    // --- WRITE seq key value ---
                    if (tokens[0] == "WRITE") {
                        if (tokens.size() < 4) {
                            flog(LogLevel::WARN, "malformed WRITE line: " + line);
                            continue;
                        }

                        got_any_replication = true;

                        uint64_t seq = 0;
                        try {
                            seq = std::stoull(tokens[1]);
                        } catch (...) {
                            flog(LogLevel::ERROR,
                                 "invalid seq in WRITE: " + tokens[1]);
                            continue;
                        }

                        // Jei šis seq jau buvo pritaikytas – tikrinam, ar sutampa turinys.
                        auto it = applied_entries_by_seq.find(seq);
                        if (it != applied_entries_by_seq.end()) {
                            const WalEntry& old_entry = it->second;
                            const std::string& key   = tokens[2];
                            const std::string& value = tokens[3];

                            // Jeigu turinys skiriasi – tai „FATAL“ konfliktas
                            // (leader'is bandytų „pakeisti praeitį“).
                            if (old_entry.op  != Op::SET ||
                                old_entry.key != key ||
                                old_entry.value != value) {
                                flog(LogLevel::ERROR,
                                     "FATAL seq conflict: seq=" +
                                     std::to_string(seq) +
                                     " old=(" +
                                     (old_entry.op==Op::SET ? "SET " : "DEL ") +
                                     old_entry.key + " " + old_entry.value +
                                     ") new=(SET " + key + " " + value + ")");
                            }

                            // Kadangi įrašas identiškas – pakartotinai jo nepritaikom,
                            // tik patvirtinam ACK.
                            send_all(repl_socket,
                                     "ACK " + std::to_string(seq) + "\n");
                            continue;
                        }

                        // Naujas seq – sukuriam WalEntry objektą
                        WalEntry entry;
                        entry.seq   = seq;
                        entry.op    = Op::SET;
                        entry.key   = tokens[2];
                        entry.value = tokens[3];

                        // Pritaikom atmintyje ir parašom į WAL failą
                        apply_entry(entry);
                        wal_append(wal_out, entry);

                        // Patvirtinam lyderiui
                        send_all(repl_socket,
                                 "ACK " + std::to_string(entry.seq) + "\n");
                    }
                    // --- DELETE seq key ---
                    else if (tokens[0] == "DELETE") {
                        if (tokens.size() < 3) {
                            flog(LogLevel::WARN,
                                 "malformed DELETE line: " + line);
                            continue;
                        }

                        got_any_replication = true;

                        uint64_t seq = 0;
                        try {
                            seq = std::stoull(tokens[1]);
                        } catch (...) {
                            flog(LogLevel::ERROR,
                                 "invalid seq in DELETE: " + tokens[1]);
                            continue;
                        }

                        // Jei šis seq jau egzistuoja – tikrinam turinio atitikimą.
                        auto it = applied_entries_by_seq.find(seq);
                        if (it != applied_entries_by_seq.end()) {
                            const WalEntry& old_entry = it->second;
                            const std::string& key = tokens[2];

                            if (old_entry.op  != Op::DEL ||
                                old_entry.key != key) {
                                flog(LogLevel::ERROR,
                                     "FATAL seq conflict: seq=" +
                                     std::to_string(seq) +
                                     " old=(" +
                                     (old_entry.op==Op::SET ? "SET " : "DEL ") +
                                     old_entry.key + " " + old_entry.value +
                                     ") new=(DEL " + key + ")");
                            }

                            // Turinyje jokio konflikto – tiesiog išsiunčiam ACK.
                            send_all(repl_socket,
                                     "ACK " + std::to_string(seq) + "\n");
                            continue;
                        }

                        // Naujas DELETE įrašas
                        WalEntry entry;
                        entry.seq = seq;
                        entry.op  = Op::DEL;
                        entry.key = tokens[2];

                        // Pritaikom atmintyje ir įrašom į WAL
                        apply_entry(entry);
                        wal_append(wal_out, entry);

                        // ACK lyderiui
                        send_all(repl_socket,
                                 "ACK " + std::to_string(entry.seq) + "\n");
                    }
                    // Kiti pranešimai – nežinomi, tiesiog užloginam DEBUG lygiu.
                    else {
                        flog(LogLevel::DEBUG,
                             "unknown replication command: " + line);
                    }
                } catch (const std::exception& ex) {
                    flog(LogLevel::ERROR,
                         std::string("exception in replication loop: ") + ex.what());
                } catch (...) {
                    flog(LogLevel::ERROR,
                         "unknown exception in replication loop");
                }
            }

            // Baigėm replikacijos sesiją – uždarom socket'ą
            net_close(repl_socket);

            // Jeigu per visą prisijungimą negavom NEI VIENO WRITE/DELETE,
            // laikom, kad tai nesėkmingas bandymas (leader'io problema ar pan.).
            if (!got_any_replication) {
                failure_count++;
                flog(LogLevel::WARN,
                     "disconnected from leader without replication, "
                     "failure_count=" + std::to_string(failure_count));
            } else {
                // Jei gavom bent vieną įrašą – tai laikom sėkme ir
                // resetinam failure_count.
                failure_count = 0;
            }

            // Jei nesėkmių skaičius viršijo limitą – „suicide“.
            if (failure_count >= MAX_FAILURES_BEFORE_EXIT) {
                flog(LogLevel::ERROR,
                     "too many consecutive failures (no replication), exiting");
                return 2;
            }

            // Prieš kitą bandymą palaukiam (backoff'ą vis tiek didinam)
            std::this_thread::sleep_for(std::chrono::milliseconds(backoff_ms));
            backoff_ms = std::min(backoff_ms * 2, MAX_BACKOFF_MS);
        }
    } catch (const std::exception& ex) {
        flog(LogLevel::ERROR,
             std::string("fatal exception in follower main: ") + ex.what());
        return 1;
    } catch (...) {
        flog(LogLevel::ERROR, "fatal unknown exception in follower main");
        return 1;
    }
}
