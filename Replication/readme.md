# Replikacijos Sistema

Šis projektas įgyvendina **Leader–Follower replikuojamą Key-Value Store** su:

-   **Raft-style leader election**
-   **WAL replication system**
-   **Leader-based writes (SET/DEL)**
-   **Follower read replicas (eventually consistent)**
-   **Automatic failover elections**

---

##  Reikalavimai

**Visos Mašinos:**
-   Linux / WSL / macOS
-   GCC 9+ / Clang 10+
-   C++17
-   pthread

**Remote VPS**
-   Tailscale įdiegtas ir sukonfigūruotas
-   4 nodes one Tailscale network

---

## Kompiliavimas

### Lokalus Build

```bash
cd Replication
make all
```

Output:
```
leader    - Leader procesas
follower  - Follower procesas
client    - CLI kliento įrankis
run       - Control plane daemon
```

### Remote Deployment

**Deploy į visus nodes:**

```bash
./deploy_all.sh
```

**Deploy į vieną node:**

```bash
./deploy.sh <node_id>
```

**Deploy Script Workflow:**
1. Kompiliuoja lokaliai (`make all`)
2. Sustabdo remote procesą per SSH
3. Upload binaries per SCP
4. Perkrauna remote node (`./run <node_id>`)

---

## Konfigūracija

```cpp
// Remote VPS cluster configuration (current default)
static NodeInfo CLUSTER[] = {
    {1, "100.117.80.126", 8001},  // Node 1 Tailscale IP
    {2, "100.70.98.49",   8002},  // Node 2 Tailscale IP
    {3, "100.118.80.33",  8003},  // Node 3 Tailscale IP
    {4, "100.116.151.88", 8004},  // Node 4 Tailscale IP
};
```

**Physical servers (for deployment):**
- Node 1: Anthony@207.180.251.206 - `/home/Anthony/database`
- Node 2: Austin@167.86.66.60 - `/home/Austin/database`
- Node 3: Edward@167.86.83.198 - `/home/Edward/database`
- Node 4: Anthony@167.86.81.251 - `/home/Anthony/database`

**Note:** Use SSH key authentication (see main README for setup instructions)
---

## Portų Schema

**Remote Nodes (Tailscale):**

| Node | Tailscale IP | Client Port | Repl Port | Control Port |
|------|--------------|-------------|-----------|--------------|
| 1 | 100.117.80.126 | 7001 | 7002 | 8001 |
| 2 | 100.70.98.49 | 7001 | 7002 | 8002 |
| 3 | 100.118.80.33 | 7001 | 7002 | 8003 |
| 4 | 100.116.151.88 | 7001 | 7002 | 8004 |

---

## Paleisti Klasterį

**Prieš pradedant - Patikrinti Konfigūraciją:**
```bash
# Įsitikinti kad rules.hpp naudoja Tailscale IPs
grep "100\." include/rules.hpp
# Turėtų rodyti: {1, "100.117.80.126", 8001}, ...
```

**Deploy į Remote Nodes:**

```bash
# Deploy į visus nodes (automatiškai compile, upload, restart)
./deploy_all.sh

# Arba deploy po vieną node
./deploy.sh 1
./deploy.sh 2
./deploy.sh 3
./deploy.sh 4
```

**Deploy script automatiškai:**
- Kompiliuoja lokaliai (`make all`)
- Upload binaries per SSH (`scp`)
- Sustabdo senus procesus (`pkill`)
- Paleidžia naujus procesus (`./run <node_id>`)
- Raft style election vyksta automatiškai (~3 sek)

---

# **5. Komandos CLI klientui**

## **SET key/value**

```bash
./client <LeaderIP> 7001 SET <key> <value>
```

Pvz:

```bash
./client 100.93.100.112 7001 SET user01 Kaspa
./client 100.93.100.112 7001 SET user02 Tomas
```

---

## **GET**

```bash
./client <LeaderIP> 7001 GET <key>
```

---

## **DELETE **

```bash
./client 100.93.100.112 7001 DEL user01
```

---

#  **6. Skaitymas iš followerių**

## 6.1. Tiesiogiai iš followerio

```bash
./client <FollowerIP> 710X GETFF <key>
```

Pvz:

```bash
./client 100.116.151.88 7104 GETFF a
```

---

# **Logai**

### Leader logai:

```
[INFO] [Leader] 100.93.100.112 7001
[INFO] Leader: listening clients on 7001
[INFO] Leader: listening followers on 7002
```

### Follower logai:

```
[Follower][INFO] sent HELLO 33
[Follower][INFO] trying to connect to leader 100.x.x.x:7002
[Follower][WARN] connect failed, sleeping 4000 ms
```

### Run control plane logai:

```
[node 3] [INFO] became LEADER term 12 with votes=3 (required=2)
```
