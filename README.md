# Paskirstyta Raktų-Reikšmių Duomenų Bazė

Pilnas paskirstytos duomenų bazės sistemos įgyvendinimas su B+ Tree saugykla, WAL (Write-Ahead Logging), Raft-stiliaus replikacija ir Vue.js žiniatinklio sąsaja.

## Architektūra

```
┌─────────────────────────────────────────────────────────────────┐
│                    Naršyklė (Vue.js SPA)                         │
│                     HTTP: localhost:8080                         │
└────────────────────────────┬────────────────────────────────────┘
                             │
┌────────────────────────────▼────────────────────────────────────┐
│              Lithium HTTP Serveris (C++)                         │
│                      Port: 8080                                  │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐         │
│  │ Static Files │  │  API Routes  │  │Leader Discov.│         │
│  │  Serving     │  │   /api/*     │  │  /api/leader │         │
│  └──────────────┘  └──────────────┘  └──────────────┘         │
│                           │                                      │
│                  ┌────────┴─────────┐                          │
│                  │   DbClient (TCP) │                          │
│                  └────────┬─────────┘                          │
└───────────────────────────┼──────────────────────────────────────┘
                            │
┌───────────────────────────▼──────────────────────────────────────┐
│              Replikacijos Klasteris (4 mazgai)                   │
│  ┌──────────────────┐  ┌──────────────────┐                    │
│  │  Lyderis         │  │  Sekėjai (2,3,4) │                    │
│  │  Port: 7001      │  │  Ports: 7101-7104│                    │
│  │  Repl: 7002      │  │                  │                    │
│  └────────┬─────────┘  └──────────────────┘                    │
│           │                                                      │
│  ┌────────▼─────────────────────────────────────┐              │
│  │  B+ Tree Database + WAL                      │              │
│  │  - Puslapių saugykla (4KB puslapiai)        │              │
│  │  - Write-Ahead Logging                       │              │
│  │  - CRUD operacijos                           │              │
│  │  - Range queries (GETFF/GETFB)              │              │
│  └──────────────────────────────────────────────┘              │
└──────────────────────────────────────────────────────────────────┘
```

## Komponentai

### 1. B+ Tree Duomenų Bazė ([btree/](btree/))

Puslapiais pagrįsta B+ medžio duomenų bazė su WAL palaikymu:
- **Puslapių tipai**: MetaPage, LeafPage, InternalPage
- **Operacijos**: Get, Set, Remove, Optimize
- **WAL**: Automatinis recovery po crash
- **Puslapių dydis**: 16KB (konfigūruojamas)

**Žr. [btree/README.md](btree/README.md)**

### 2. Replikacijos Sistema ([Replication/](Replication/))

Raft-stiliaus lyderio-sekėjų replikacija:
- **4 Mazgai**: 1 lyderis, 3 sekėjai
- **Automatinės Rinkimai**: Heartbeat + voting sistema
- **WAL Replikacija**: Leader → Followers streaming
- **Failover**: Automatinis naujojo lyderio išrinkimas

**Žr. [Replication/readme.md](Replication/readme.md)**

### 3. HTTP Serveris + Web GUI ([server/](server/))

Lithium C++ HTTP serveris su Vue.js 3 aplikacija:
- **API Endpoints**: GET, SET, DELETE, GETFF, GETFB, OPTIMIZE
- **Leader Discovery**: Dinaminis lyderio aptikimas
- **Static Files**: Vue.js SPA hosting
- **Web GUI**: Bootstrap 5 responsive UI

**Žr. [server/README.md](server/README.md)**

## Greitas Startas

### Reikalavimai (Ubuntu/Linux)

```bash
# C++ kompiliatorius ir bibliotekos
sudo apt update
sudo apt install -y build-essential cmake g++ libssl-dev libboost-all-dev

# SSH tunneling tools (local machine only)
sudo apt install -y sshpass autossh

# Node.js 18+ (web aplikacijai)
curl -o- https://raw.githubusercontent.com/nvm-sh/nvm/v0.40.3/install.sh | bash
\. "$HOME/.nvm/nvm.sh"
nvm install 18
```

### Sistemos Konfigūracija

Sistema palaiko **du deployment režimus**:

#### **Režimas 1: Localhost Mazgai (4 procesai viename PC)**

**Aprašymas:**
- Visi 4 mazgai veikia vietinėje mašinoje
- Naudoja localhost (127.0.0.1) su skirtingais portais

**Paleisti:**
```bash
cd Replication
./run 1  # Terminal 1
./run 2  # Terminal 2
./run 3  # Terminal 3
./run 4  # Terminal 4
```

**Konfigūracija:** Žr. [Režimas 1 konfigūraciją](#konfigūruoti-deployment-režimą)

---

#### **Režimas 2: Remote VPS (Paskirstyta su Tailscale)**

**Remote Setup (4 VPS nodes):**
- Mazgai veikia nuotoliniu serveriuose su Tailscale tinklu
- Tailscale IP adresai: 100.x.x.x (node-to-node ir client-to-node komunikacijai)
- Fiziniai IP adresai: prieinami per SSH deployment

**Local Setup (Development Machine):**
- Turi būti įjungtas Tailscale ir prijungtas prie to paties tinklo
- HTTP serveris veikia lokaliai (port 8080)
- **Tiesioginis ryšys**: Jungiasi tiesiai per Tailscale (be SSH tunnels)
- Client API: 100.x.x.x:7001 (per Tailscale)
- Control plane: 100.x.x.x:8001-8004 (per Tailscale)

**Konfigūracija:** Žr. [Režimas 2 konfigūraciją](#konfigūruoti-deployment-režimą)

---

## Konfigūruoti Deployment Režimą

**SVARBU:** Prieš kompiliuojant, pasirinkite režimą ir sukonfigūruokite du failus:

### Režimas 1: Localhost Mazgai

#### 1. Konfigūruoti Klasterio Mazgus
**File:** [Replication/include/rules.hpp](Replication/include/rules.hpp)

```cpp
// Localhost cluster - Line ~18
static NodeInfo CLUSTER[] = {
    {1, "127.0.0.1", 8001},  // Node 1
    {2, "127.0.0.1", 8002},  // Node 2
    {3, "127.0.0.1", 8003},  // Node 3
    {4, "127.0.0.1", 8004},  // Node 4
};
```

#### 2. Konfigūruoti HTTP Serverį
**File:** [server/include/routes.hpp](server/include/routes.hpp)

```cpp
// Line ~50: CLUSTER_NODES
const std::vector<std::string> CLUSTER_NODES = {
  "127.0.0.1:7101", "127.0.0.1:7102",
  "127.0.0.1:7103", "127.0.0.1:7104"
};

// Line ~41: CONTROL_PLANE_TUNNEL_MAP
static const std::unordered_map<std::string, std::string> CONTROL_PLANE_TUNNEL_MAP = {
  {"127.0.0.1:8001", "127.0.0.1:8001"},
  {"127.0.0.1:8002", "127.0.0.1:8002"},
  {"127.0.0.1:8003", "127.0.0.1:8003"},
  {"127.0.0.1:8004", "127.0.0.1:8004"}
};

// Line ~59: TAILSCALE_TO_LOCAL_MAP - Leave empty for localhost
const static std::unordered_map<std::string, std::string> TAILSCALE_TO_LOCAL_MAP = {};
```

---

### Režimas 2: Remote VPS (Current Default)

#### 0. Tailscale Setup (One-Time)
**Reikalingas visiems remote mazgams ir local machine:**

```bash
# Įdiekite Tailscale (Ubuntu/Debian)
curl -fsSL https://tailscale.com/install.sh | sh

# Prisijunkite prie Tailscale tinklo
sudo tailscale up

# Patikrinkite, ar visi mazgai matomi
tailscale status
```

Turėtumėte matyti visus 4 VPS nodes ir savo local machine.

#### 1. Konfigūruoti Klasterio Mazgus
**File:** [Replication/include/rules.hpp](Replication/include/rules.hpp)

```cpp
// Remote VPS cluster with Tailscale - Line ~18
static NodeInfo CLUSTER[] = {
    {1, "100.117.80.126", 8001},  // Node 1 Tailscale IP
    {2, "100.70.98.49",   8002},  // Node 2 Tailscale IP
    {3, "100.118.80.33",  8003},  // Node 3 Tailscale IP
    {4, "100.116.151.88", 8004},  // Node 4 Tailscale IP
};
```

#### 2. Konfigūruoti HTTP Serverį
**File:** [server/include/routes.hpp](server/include/routes.hpp)

```cpp
// Line ~50: CLUSTER_NODES - Direct Tailscale connections
const std::vector<std::string> CLUSTER_NODES = {
  "100.117.80.126:7001",  // Node 1 (direct Tailscale)
  "100.70.98.49:7001",    // Node 2 (direct Tailscale)
  "100.118.80.33:7001",   // Node 3 (direct Tailscale)
  "100.116.151.88:7001"   // Node 4 (direct Tailscale)
};

// Line ~41: CONTROL_PLANE_TUNNEL_MAP - Direct Tailscale (no translation)
static const std::unordered_map<std::string, std::string> CONTROL_PLANE_TUNNEL_MAP = {
  {"100.117.80.126:8001", "100.117.80.126:8001"},  // Direct
  {"100.70.98.49:8002",   "100.70.98.49:8002"},    // Direct
  {"100.118.80.33:8003",  "100.118.80.33:8003"},   // Direct
  {"100.116.151.88:8004", "100.116.151.88:8004"}   // Direct
};
```

**⚠️ PASTABA**: `server/start_tunnels.sh` yra **deprecated** ir nebereikalingas. Sistema dabar naudoja tiesioginį Tailscale ryšį.

**Po konfigūracijos pakeitimo, reikia perkompiliuoti:**
```bash
cd Replication && make clean && make all
cd ../server && make clean && make all
```

---

### 1. Kompiliuoti B+ Tree DB

```bash
cd btree
make all
./build/main  # CLI interaktyvus režimas
```

### 2. SSH Key Setup (Režimas 2 - One Time)

**Setup passwordless SSH authentication:**

```bash
# Generate SSH key (if you don't have one)
ssh-keygen -t ed25519

# Copy public key to each remote server
ssh-copy-id Anthony@207.180.251.206  # Node 1
ssh-copy-id Austin@167.86.66.60      # Node 2
ssh-copy-id Edward@167.86.83.198     # Node 3
ssh-copy-id Anthony@167.86.81.251    # Node 4

# Test connection (should not ask for password)
ssh Anthony@207.180.251.206 'echo OK'
```

### 3. Deploy Remote Cluster (Režimas 2)

```bash
cd Replication

# Deploy į visus nodes (automatiškai compile, upload, restart)
./deploy_all.sh
```

**Remote Servers:**
- Node 1: Anthony@207.180.251.206 (Tailscale: 100.117.80.126)
- Node 2: Austin@167.86.66.60 (Tailscale: 100.70.98.49)
- Node 3: Edward@167.86.83.198 (Tailscale: 100.118.80.33)
- Node 4: Anthony@167.86.81.251 (Tailscale: 100.116.151.88)

**Deploy script automatiškai:**
- Kompiliuoja lokaliai (`make all`)
- Upload binaries per SSH
- Sustabdo senus procesus
- Paleidžia naujus procesus
- Raft election vyksta automatiškai (~3 sek)

**Cluster Configuration:** [Replication/include/rules.hpp](Replication/include/rules.hpp)

```cpp
static NodeInfo CLUSTER[] = {
    {1, "100.117.80.126", 8001},  // Node 1 Tailscale IP
    {2, "100.70.98.49",   8002},  // Node 2
    {3, "100.118.80.33",  8003},  // Node 3
    {4, "100.116.151.88", 8004},  // Node 4
};
```

### 4. Kompiliuoti ir Paleisti HTTP Serverį

```bash
cd server

# Pirmas kartas: įdiegti Lithium framework
./scripts/setup_lithium

# Kompiliuoti serverį
make all

# Kompiliuoti Vue.js app
cd webapp && npm install && npm run build && cd ..

# Paleisti serverį
./server_app  # Port 8080
```

**Prieiga**: http://localhost:8080

**Detalesnė dokumentacija:** [server/README.md](server/README.md)

---

### Port'ų Schema

#### Režimas 1: Localhost Mazgai

**Visi procesai:** 127.0.0.1 (localhost)

| Komponentas | Portas | Aprašymas |
|-------------|--------|-----------|
| HTTP Serveris | 8080 | Web GUI + API |
| Node 1 Client | 7001 | Leader client API |
| Node 1 Replication | 7002 | WAL streaming |
| Node 1-4 Read | 7101-7104 | Follower read APIs |
| Control Plane | 8001-8004 | Raft heartbeat/election |

**Kaip veikia:**
- HTTP serveris jungiasi į `127.0.0.1:7101-7104` (tiesiogiai)
- Mazgai komunikuoja per `127.0.0.1:8001-8004` (tiesiogiai)
- Nereikia SSH tunnels ar IP translation

#### Režimas 2: Remote VPS (Remote + Tunnels)

**Local Machine:**

| Port | Tunnel To | Aprašymas |
|------|-----------|-----------|
| 8080 | - | HTTP Server (local) |
| 7101 | Node1:7001 | SSH tunnel → Node 1 client API |
| 7102 | Node2:7001 | SSH tunnel → Node 2 client API |
| 7103 | Node3:7001 | SSH tunnel → Node 3 client API |
| 7104 | Node4:7001 | SSH tunnel → Node 4 client API |
| 8001 | Node1:8001 | SSH tunnel → Node 1 control plane |
| 8002 | Node2:8002 | SSH tunnel → Node 2 control plane |
| 8003 | Node3:8003 | SSH tunnel → Node 3 control plane |
| 8004 | Node4:8004 | SSH tunnel → Node 4 control plane |

**Remote VPS Nodes:**

| Node | Physical IP | Tailscale IP | Client Port | Repl Port | Control Port |
|------|-------------|--------------|-------------|-----------|--------------|
| 1 | 207.180.251.206 | 100.117.80.126 | 7001 | 7002 | 8001 |
| 2 | 167.86.66.60 | 100.70.98.49 | 7001 | 7002 | 8002 |
| 3 | 167.86.83.198 | 100.118.80.33 | 7001 | 7002 | 8003 |
| 4 | 167.86.81.251 | 100.116.151.88 | 7001 | 7002 | 8004 |

## Naudojimas

### CLI (Interaktyvus)

```bash
cd btree
./build/main

> SET user01 Jonas
> GET user01
> GETFF user 10
> DEL user01
> OPTIMIZE
> EXIT
```

### CLI (Klasteris)

#### **Patobulintas Node Selection Syntax** (Naujiena!)

```bash
cd Replication

# Cluster status
./client status  # arba ./client leader

# ✨ NAUJAS SYNTAX: Tiesiai nurodyti mazgą pagal alias
./client node1 GET user01        # Skaityti iš Node 1 (follower reads)
./client node2 SET user01 Matas  # Rašyti į Node 2 (turi būti leader!)
./client node3 DEL user01        # Trinti per Node 3 (turi būti leader!)
./client node4 GETFF user 10     # Range query iš Node 4

# Node aliases: node1, node2, node3, node4
# - GET: veikia ant bet kurio mazgo (follower reads)
# - SET/DEL: turi eiti į lyderį (automatic REDIRECT)
```

#### **Tradicinis Syntax** (tebepalaiko)

```bash
# Rasti lyderį ir naudoti IP:port
./client leader  # Parodo kuris node yra leader

# Tradicinis host:port formatas
./client 100.70.98.49 7001 SET user01 Matas  # Direct Tailscale IP
./client 100.70.98.49 7001 GET user01
./client 100.70.98.49 7001 GETFF user 10
./client 100.70.98.49 7001 GETFB user99 10
```

### HTTP API

#### **Patobulintas API su Node Selection** (Naujiena!)

```bash
# ✨ NAUJAS: ?nodeId parametras leidžia pasirinkti mazgą

# SET (turi eiti į lyderį)
curl -X POST "http://localhost:8080/api/set/user01?nodeId=2" \
  -H "Content-Type: application/json" \
  -d '{"value":"Matas"}'

# GET (gali skaityti iš bet kurio mazgo - follower reads)
curl "http://localhost:8080/api/get/user01?nodeId=1"  # Skaityti iš Node 1
curl "http://localhost:8080/api/get/user01?nodeId=3"  # Skaityti iš Node 3

# DELETE (turi eiti į lyderį)
curl -X POST "http://localhost:8080/api/del/user01?nodeId=2"

# Range queries su node selection
curl "http://localhost:8080/api/getff/user?count=10&nodeId=4"

# Be nodeId parametro - automatic leader discovery (default)
curl -X POST http://localhost:8080/api/set/user01 \
  -H "Content-Type: application/json" \
  -d '{"value":"Matas"}'

# Cluster info
curl http://localhost:8080/api/cluster/nodes    # Gauti visus mazgus
curl http://localhost:8080/api/cluster/status   # Klasterio būsena
```

**Node Selection Logika:**
- **GET operacijos**: Veikia ant bet kurio mazgo (eventually consistent reads)
- **SET/DEL operacijos**: Turi eiti į lyderį (gražina klaidą jei ne leader)
- **Default (be nodeId)**: Automatinis leader discovery

#### **Web GUI** (Naujiena! Node Selector)

1. Atidaryti naršyklę: http://localhost:8080
2. **Node Selection Dropdown**:
   - Auto (Leader Discovery) - numatytasis
   - Node 1 (100.117.80.126)
   - Node 2 (100.70.98.49)
   - Node 3 (100.118.80.33)
   - Node 4 (100.116.151.88)
3. Funkcijos:
   - **CRUD** - Kurti/skaityti/atnaujinti/trinti
   - **Browse** - Naršyti su puslapiavimų
   - **Cluster** - Klasterio būsena
   - **Node Selector** - Pasirinkti konkretų mazgą operacijoms

## Testuojimo Scenarijai

### 1. Vieno Mazgo Testas

```bash
cd btree
./build/main
# Testuoti CRUD operacijas
```

### 2. Replikacijos Testas

```bash
# Paleisti klasterį (4 terminalai)
# Rašyti į lyderį
./client 127.0.0.1 7001 SET test value
# Skaityti iš sekėjo
./client 127.0.0.2 7101 GET test
```

### 3. Failover Testas

```bash
# Nutraukti lyderį
killall leader
# Palaukti naujų rinkimų (~3 sek)
# Testuoti su nauju lyderiu
./client leader
./client <new_leader_ip> 7001 GET test
```

### 4. Web GUI Testas

```bash
# Paleisti serverį + klasterį
# Atidaryti http://localhost:8080
# Testuoti visas funkcijas per GUI
```

## Problemų Sprendimas

### Klasteris nepasileido

```bash
# Patikrinti ar portai laisvi
sudo lsof -i :7001
sudo lsof -i :7002
sudo lsof -i :7101-7104
sudo lsof -i :8001-8004

# Išvalyti procesus
killall run leader follower
```

### Leader neišrenkamas

1. Įsitikinti, kad veikia bent 3 mazgai (majority)
2. Patikrinti control port'us (8001-8004)
3. Peržiūrėti terminalų išvestis (rinkimų pranešimai)

### HTTP serveris negali rasti lyderio

```bash
# Patikrinti ar klasteris veikia
cd Replication
./client leader

# Paleisti serverį iš naujo
cd ../server
killall server_app
./server_app
```

### Web aplikacija neužsikrauna

```bash
# Perkompiliuoti
cd server/webapp
npm run build

# Patikrinti ar sukurti failai
ls -la ../public/

# Paleisti serverį
cd ..
./server_app
```

## Technologijos

- **C++20** - Pagrindinis programavimo kalba
- **B+ Tree** - Duomenų struktūra
- **WAL** - Write-Ahead Logging
- **Raft** - Consensus algoritmas (supaprastintas)
- **TCP/IP** - Tinklo komunikacija
- **Lithium** - HTTP serverio framework
- **Vue.js 3** - Frontend framework
- **Bootstrap 5** - CSS framework
- **Webpack 5** - Module bundler

## Coding Convention

Sekti: https://www.geeksforgeeks.org/cpp/naming-convention-in-c/

## Apribojimai

- **Max rakto ilgis**: 255 baitai
- **Max reikšmės ilgis**: 2048 baitai
- **Puslapio dydis**: 16384 baitai
- **Klasterio dydis**: 4 mazgai (fiksuotas)
- **Heartbeat timeout**: 1500ms

## Saugumas

⚠️ **Dabartinė implementacija yra TIK kūrimui/mokymui:**
- Nėra autentifikacijos
- Nėra šifravimo (plain TCP/HTTP)
- Nėra rate limiting
- Nėra input validation
- Atvira prieiga prie visų operacijų

**Nenaudoti produkcinėje aplinkoje be papildomų saugumo priemonių!**

## Žr. Taip Pat

- [B+ Tree README](btree/README.md)
- [Replication README](Replication/readme.md)
- [HTTP Server README](server/README.md)
- [Web App README](server/webapp/README.md)
