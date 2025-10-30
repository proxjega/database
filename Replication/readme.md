# SET key/value via leader

`./client <LeaderIP> 7001 SET <key> <value>`

# GET (automatically follows REDIRECT from followers)

`./client <LeaderIP> 7001 GET <key>`

# DELETE key

`./client <LeaderIP> 7001 DEL <key>`

# Read follower directly (may be stale)

`./client <LeaderIP> 7001 GETFF <key> <FollowerIP:710X>`

# Read follower first → fallback leader if needed

`./client <LeaderIP> 7001 GETFB <key> <FollowerIP:710X>`

`g++ -std=c++17 -pthread leader.cpp -o leader
g++ -std=c++17 -pthread follower.cpp -o follower
g++ -std=c++17 -pthread client.cpp -o client
g++ -std=c++17 -pthread run.cpp -o run`

| Port                    | Used by                 | Purpose                       |
| ----------------------- | ----------------------- | ----------------------------- |
| **7001, 7003, 7005, …** | **Clients → Leader**    | Handling GET/SET/DEL requests |
| **7002, 7004, 7006, …** | **Leader → Followers**  | Replication and WAL updates   |
| **7101, 7102, 7103, …** | **Clients → Followers** | Read-only GET access          |

# Insert data (leader accepts writes)

`./client 100.93.100.112 7001 SET user01 Kaspa`
`./client 100.93.100.112 7001 SET user02 KaspXaAS`

# Read leader

`./client 100.93.100.112 7001 GET user02`

# Read follower

`./client 100.125.32.90 7102 GET user02`

# Read follower w/ fallback

`./client 100.93.100.112 7001 GETFB user03 100.96.196.71:7103`
