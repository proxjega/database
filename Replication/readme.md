# Replikacijos Sistema

Šis projektas įgyvendina **Leader–Follower replikuojamą Key-Value Store** su:

-   🔁 **Raft-style leader election**
-   📡 **WAL replication system**
-   📦 **Leader-based writes (SET/DEL)**
-   🔍 **Follower read replicas (eventually consistent)**
-   🚨 **Automatic failover elections**
-   ↩️ **Client REDIRECT mechanizmu**

---

## Deployment Režimai

Sistema palaiko **du deployment režimus**:

### **Režimas 1: Localhost Mazgai (4 procesai vienoje mašinoje)**
- Visi 4 mazgai veikia vietinėje mašinoje
- Naudoja localhost (127.0.0.1) IP adresus
- Skirtingi portai kiekvienam mazgui
- Idealus testavimui ir plėtrai

### **Režimas 2: Remote VPS (Paskirstyta su Tailscale)**
- 4 remote VPS nodes skirtinguose serveriuose
- Tailscale mesh network (100.x.x.x IPs) inter-node komunikacijai
- SSH tunnels local HTTP serveriui prisijungti
- Produkcinė paskirstyta sistema

---

## ⚙️ Reikalavimai

**Visos Mašinos:**
-   Linux / WSL / macOS
-   GCC 9+ / Clang 10+
-   C++17
-   pthread

**Remote VPS (Režimas B):**
-   Tailscale įdiegtas ir sukonfigūruotas
-   4 nodes one Tailscale network

**Local Machine (Režimas B):**
-   SSH prieiga prie VPS nodes
-   sshpass, autossh (tunnel'iams)

---

## 🧱 Kompiliavimas

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

### Remote Deployment (Režimas B)

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

## 🔧 Konfigūracija

### Pasirinkti Deployment Režimą

**File:** [include/rules.hpp](include/rules.hpp)

Pakeiskite `CLUSTER[]` array pagal jūsų režimą:

#### **Režimas 1: Localhost Mazgai**

```cpp
// Localhost cluster configuration
static NodeInfo CLUSTER[] = {
    {1, "127.0.0.1", 8001},  // Node 1
    {2, "127.0.0.1", 8002},  // Node 2
    {3, "127.0.0.1", 8003},  // Node 3
    {4, "127.0.0.1", 8004},  // Node 4
};
```

**Kaip paleisti:**
- Visi procesai veikia vienoje mašinoje
- Kiekvienas mazgas naudoja skirtingą control plane portą (8001-8004)
- Paleidžiama: `./run 1`, `./run 2`, `./run 3`, `./run 4` (4 terminalai)

#### **Režimas 2: Remote VPS (Tailscale)**

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

**Kaip paleisti:**
- SSH į kiekvieną VPS
- Paleidžiama: `./run <node_id>` kiekviename serveryje
- Mazgai komunikuoja per Tailscale network

**Po konfigūracijos pakeitimo:**
```bash
make clean && make all
```

---

## 🔌 Portų Schema

### Režimas 1: Localhost Mazgai

| Portas | Naudotojas | Paskirtis |
|--------|------------|-----------|
| **7001** | Clients → Leader | SET / GET / DEL operacijos |
| **7002** | Leader → Followers | WAL replikacijos srautas |
| **7101-7104** | Clients → Followers | Read-only GET |
| **8001-8004** | Cluster control plane | Raft heartbeats, elections |

**Visi portai:** 127.0.0.1 (localhost)

### Režimas 2: Remote VPS

**Remote Nodes (Tailscale):**

| Node | Tailscale IP | Client Port | Repl Port | Control Port |
|------|--------------|-------------|-----------|--------------|
| 1 | 100.117.80.126 | 7001 | 7002 | 8001 |
| 2 | 100.70.98.49 | 7001 | 7002 | 8002 |
| 3 | 100.118.80.33 | 7001 | 7002 | 8003 |
| 4 | 100.116.151.88 | 7001 | 7002 | 8004 |

**Local Machine (SSH Tunnels):**

HTTP serveris jungiasi per tunnels:
- `127.0.0.1:7101-7104` → Remote client API (7001)
- `127.0.0.1:8001-8004` → Remote control plane (8001-8004)

Žr. [../server/start_tunnels.sh](../server/start_tunnels.sh)

---

## 🚀 Paleisti Klasterį

### Režimas 1: Localhost Mazgai

**Prieš pradedant - Patikrinti Konfigūraciją:**
```bash
# Įsitikinti kad rules.hpp naudoja localhost IPs
grep "127.0.0.1" include/rules.hpp
# Turėtų rodyti: {1, "127.0.0.1", 8001}, ...
```

**Paleisti 4 terminalus:**

```bash
# Terminal 1
./run 1

# Terminal 2
./run 2

# Terminal 3
./run 3

# Terminal 4
./run 4
```

Palaukti ~3 sek lyderio rinkimų:
```
[node X] [INFO] became LEADER term 1 with votes=3 (required=2)
```

### Režimas 2: Remote VPS

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
- Raft election vyksta automatiškai (~3 sek)

---

# 🧠 **5. Komandos klientui**

## 📝 **SET key/value (rašoma tik į leader)**

```bash
./client <LeaderIP> 7001 SET <key> <value>
```

Pvz:

```bash
./client 100.93.100.112 7001 SET user01 Kaspa
./client 100.93.100.112 7001 SET user02 Tomas
```

---

## 🔍 **GET (automatiškai seka REDIRECT iš followerių)**

```bash
./client <LeaderIP> 7001 GET <key>
```

---

## ❌ **DELETE key (tik leader)**

```bash
./client 100.93.100.112 7001 DEL user01
```

---

# 📖 **6. Skaitymas iš followerių**

## 6.1. 🎯 Tiesiogiai iš followerio (gali būti stale)

```bash
./client <LeaderIP> 7001 GETFF <key> <FollowerIP:710X>
```

Pvz:

```bash
./client 100.93.100.112 7001 GETFF user02 100.125.32.90:7102
```

---

## 6.2. 🔁 Follower → Leader fallback

1. Jei follower turi duomenį – grąžins iš follower.
2. Jei follower NOT_FOUND – nukreips į leader.

```bash
./client <LeaderIP> 7001 GETFB <key> <FollowerIP:710X>
```

Pvz:

```bash
./client 100.93.100.112 7001 GETFB user03 100.96.196.71:7103
```

---

# 🧪 **7. Testuojame replikaciją**

### 🟢 Įrašome duomenis į leader:

```bash
./client 100.93.100.112 7001 SET balance 500
./client 100.93.100.112 7001 SET city Vilnius
./client 100.93.100.112 7001 SET name Jonas
```

### 🔵 Skaitome iš followerių:

Follower #2:

```bash
./client 100.125.32.90 7102 GET name
```

Follower #3:

```bash
./client 100.96.196.71 7103 GET balance
```

---

# 🔥 **8. Failover testas**

1️⃣ **Išjungiame leader (Node 1):**

```bash
killall leader
```

2️⃣ Laukiame ~3 sekundes.

3️⃣ Klasteris automatiškai išrenka naują leader.

4️⃣ Toliau galime rašyti į naują leader:

```bash
./client <NewLeaderIP> 7001 SET user10 Kirpas
```

5️⃣ Visi followeriai turėtų replikuoti duomenis.

---

# 📁 **9. WAL failai**

Kiekvienas node turi savo WAL:

```
node1.log
node2.log
node3.log
node4.log
```

Kiekviena eilutė:

```
<seq> <op> <key> <value>
```

Pvz:

```
1	SET	user01	Kaspa
2	SET	city	Vilnius
3	DEL	user01
```

Followeriai WAL’ą krauna paleidimo metu ir taiko į atmintį.

---

# 🧩 **10. Read-only serveriai followeriuose**

Kiekvienas followeris turi read-only serverį:

-   follower 1 → port **7101**
-   follower 2 → port **7102**
-   follower 3 → port **7103**
-   follower 4 → port **7104**

Jie palaiko tik:

```
GET <key>
```

Visos kitos komandos → automatinis:

```
REDIRECT <LeaderIP> 7001
```

---

# 🛠️ **11. Logai**

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
