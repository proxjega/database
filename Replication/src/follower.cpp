#include "../include/common.hpp"
#include "../../btree/include/database.h"
#include <algorithm>
#include <memory>
#include <unordered_set>
#include <unordered_map>
#include <thread>
#include <chrono>

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

static std::unique_ptr<Database> duombaze;

// Paskutinio pritaikyto WAL įrašo sekos numeris.
// Naudojamas HELLO žinutėje, kad leader'is žinotų nuo kurio lsn siųsti toliau.
static uint64_t last_applied_lsn = 0;

// Informacija apie leader'į (naudojama REDIRECT atsakymui read-only serveryje).
static std::string g_leader_host;
static constexpr uint16_t LEADER_CLIENT_PORT = 7001; // leader'io kliento API (SET/GET/DEL) portas

/* ========= Helpers ========= */

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
            if (client_socket == NET_INVALID) {
                continue;
            }

            // Nedidelis timeout, kad klientas neužkabintų ryšio amžinai
            set_socket_timeouts(client_socket, Consts::SOCKET_TIMEOUT_MS);

            // Kiekvieną klientą aptarnaujam atskiram threade
            std::thread([client_socket]() {
                try {
                    std::string line;

                    // Skaitom eiles iš kliento, kol jis kalba arba nutrūksta ryšys
                    while (recv_line(client_socket, line)) {
                        auto tokens = split(trim(line), ' ');
                        if (tokens.empty()) {
                            continue;
                        }

                        // Komanda: GET <key>
                        if (tokens.size() >= 2 && tokens[0] == "GET") {
                            const std::string& key = tokens[1];
                            auto result = duombaze->Get(key);
                            if (result.has_value()) {
                                send_all(client_socket, "VALUE " + result->value + "\n");
                            } else {
                                send_all(client_socket, "NOT_FOUND\n");
                            }
                        }
                        // GETFF <key> <n> - Forward range query
                        else if (tokens.size() >= 3 && tokens[0] == "GETFF") {
                            try {
                                uint32_t n = std::stoul(tokens[2]);
                                auto results = duombaze->GetFF(tokens[1], n);

                                for (const auto& cell : results) {
                                    send_all(client_socket, "KEY_VALUE " + cell.key + " " + cell.value + "\n");
                                }
                                send_all(client_socket, "END\n");
                            } catch (const std::exception& e) {
                                send_all(client_socket, "ERR " + std::string(e.what()) + "\n");
                            }
                        }
                        // GETFB <key> <n> - Backward range query
                        else if (tokens.size() >= 3 && tokens[0] == "GETFB") {
                            try {
                                uint32_t n = std::stoul(tokens[2]);
                                auto results = duombaze->GetFB(tokens[1], n);

                                for (const auto& cell : results) {
                                    send_all(client_socket, "KEY_VALUE " + cell.key + " " + cell.value + "\n");
                                }
                                send_all(client_socket, "END\n");
                            } catch (const std::exception& e) {
                                send_all(client_socket, "ERR " + std::string(e.what()) + "\n");
                            }
                        }
                        else {
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
        if (portTmp <= 0 || portTmp > Consts::MAX_PORT_NUMBER) {
            flog(LogLevel::ERROR,
                 "leader_follower_port out of range: " + std::to_string(portTmp));
            return 1;
        }
        uint16_t leader_repl_port = static_cast<uint16_t>(portTmp);

        // Inicializuojame duomenų bazę.
        std::string dbName = argv[3];

        flog(LogLevel::INFO, "starting follower db=" + dbName);
        duombaze = std::make_unique<Database>(dbName);

        // Gauname LSN is Duombazės WAL'o.
        last_applied_lsn = duombaze->GetWalSequenceNumber();
        flog(LogLevel::INFO, "Database loaded, last_applied_lsn=" + std::to_string(last_applied_lsn));

        // Read-only serverio portas (GET/REDIRECT API)
        int roTmp = 0;
        try {
            roTmp = std::stoi(argv[5]);
        } catch (...) {
            flog(LogLevel::ERROR,
                 std::string("invalid read_only_port: ") + argv[5]);
            return 1;
        }
        if (roTmp <= 0 || roTmp > Consts::MAX_PORT_NUMBER) {
            flog(LogLevel::ERROR,
                 "read_only_port out of range: " + std::to_string(roTmp));
            return 1;
        }
        uint16_t read_only_port = static_cast<uint16_t>(roTmp);

        flog(LogLevel::INFO,
             "starting follower; leader=" + g_leader_host +
             ":" + std::to_string(leader_repl_port) +
             " dbName=" + dbName +
             " read_port=" + std::to_string(read_only_port));

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
            set_socket_timeouts(repl_socket, Consts::SOCKET_TIMEOUT_MS);

            // HELLO – nusiunčiam leader'iui savo paskutinį pritaikytą lsn,
            // kad jis galėtų siųsti tik naujus įrašus.
            std::string hello = "HELLO " + std::to_string(last_applied_lsn) + "\n";
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
                 "sent HELLO " + std::to_string(last_applied_lsn));

            // Flag'as, ar šito prisijungimo metu gavome bent vieną WRITE/DELETE.
            bool got_any_replication = false;

            // --- Replikacijos skaitymo ciklas ---
            std::string line;
            while (recv_line(repl_socket, line)) {
                auto tokens = split(trim(line), ' ');
                if (tokens.empty()) {
                    continue;
                }

                try {
                    // --- WRITE lsn key value ---
                    if (tokens[0] == "WRITE") {
                        if (tokens.size() < 4) {
                            flog(LogLevel::WARN, "malformed WRITE line: " + line);
                            continue;
                        }

                        uint64_t lsn = 0;
                        try {
                            lsn = std::stoull(tokens[1]);
                        } catch (...) {
                            flog(LogLevel::ERROR,
                                 "invalid lsn in WRITE: " + tokens[1]);
                            continue;
                        }

                        if (lsn <= last_applied_lsn) {
                            send_all(repl_socket, "ACK " + std::to_string(lsn) + "\n");
                            continue;
                        }

                        // Naujas lsn – sukuriam WalRecord objektą
                        WalRecord walRecord(lsn, WalOperation::SET, tokens[2], tokens[3]);

                        // Replikuojamę lyderį.
                        if (duombaze->ApplyReplication(walRecord)) {
                            last_applied_lsn = walRecord.lsn;
                            // Patvirtinam lyderiui
                             send_all(repl_socket, "ACK " + std::to_string(walRecord.lsn) + "\n");
                             got_any_replication = true;
                        }
                    }
                    // --- DELETE lsn key ---
                    else if (tokens[0] == "DELETE") {
                        if (tokens.size() < 3) {
                            flog(LogLevel::WARN,
                                 "malformed DELETE line: " + line);
                            continue;
                        }

                        got_any_replication = true;

                        uint64_t lsn = 0;
                        try {
                            lsn = std::stoull(tokens[1]);
                        } catch (...) {
                            flog(LogLevel::ERROR,
                                 "invalid lsn in DELETE: " + tokens[1]);
                            continue;
                        }

                        if (lsn <= last_applied_lsn) {
                            send_all(repl_socket, "ACK " + std::to_string(lsn) + "\n");
                            continue;
                        }

                        // Naujas DELETE įrašas
                        WalRecord walRecord(lsn, WalOperation::DELETE, tokens[2]);

                        // Replikuojame lyderį;
                        if (duombaze->ApplyReplication(walRecord)) {
                            last_applied_lsn = walRecord.lsn;
                            // ACK Lyderiui.
                            send_all(repl_socket, "ACK " + std::to_string(lsn) + "\n");
                            got_any_replication = true;
                        }
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
