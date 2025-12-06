# Paskirstyta Raktų-Reikšmių Duomenų Bazė

Pilnas paskirstytos duomenų bazės sistemos įgyvendinimas su B+ Tree, WAL (Write-Ahead Logging), Raft-stiliaus replikacija ir Vue.js web sąsaja.

## Architektūra

```
┌────────────────────────────────────────────────────────────────┐
│                    Naršyklė (Vue.js SPA)                       │
│                     HTTP: localhost:8080                       │
└────────────────────────────┬───────────────────────────────────┘
                             │
┌────────────────────────────▼───────────────────────────────────┐
│              Lithium HTTP Serveris (C++)                       │
│                      Port: 8080                                │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐          │
│  │ Static Files │  │  API Routes  │  │Leader Discov.│          │
│  │  Serving     │  │   /api/*     │  │  /api/leader │          │
│  └──────────────┘  └──────────────┘  └──────────────┘          │
│                           │                                    │
│                  ┌────────┴─────────┐                          │
│                  │   DbClient (TCP) │                          │
│                  └────────┬─────────┘                          │
└───────────────────────────┼────────────────────────────────────┘
                            │
┌───────────────────────────▼────────────────────────────────────┐
│              Replikacijos Klasteris (4 mazgai)                 │
│  ┌──────────────────┐  ┌──────────────────┐                    │
│  │  Lyderis         │  │  Sekėjai (2,3,4) │                    │
│  │  Port: 7001      │  │  Ports: 7101-7104│                    │
│  │  Repl: 7002      │  │                  │                    │
│  └────────┬─────────┘  └──────────────────┘                    │
│           │                                                    │
│  ┌────────▼─────────────────────────────────────┐              │
│  │  B+ Tree Database + WAL                      │              │
│  │  - Puslapių saugykla (16KB puslapiai)        │              │
│  │  - Write-Ahead Logging                       │              │
│  │  - CRUD operacijos                           │              │
│  │  - Range queries (GETFF/GETFB)               │              │
│  └──────────────────────────────────────────────┘              │
└────────────────────────────────────────────────────────────────┘
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
- **API Endpoints**: GET, SET, DELETE, GETFF, GETFB, OPTIMIZE, ...
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

# Node.js 24 (web aplikacijai)
curl -o- https://raw.githubusercontent.com/nvm-sh/nvm/v0.40.3/install.sh | bash
\. "$HOME/.nvm/nvm.sh"
nvm install 24
node -v # Should print "v24.x.x"
npm -v # Should print "11.6.x"
```

### Sistemos Konfigūracija

- Client API: 100.x.x.x:7001 (per Tailscale)
- Control plane: 100.x.x.x:8001-8004 (per Tailscale)

#### Tailscale Setup ir Node config (one-time)
**Reikalingas visiems remote mazgams ir local machine:**

```bash
# Įdiekite Tailscale (Ubuntu/Debian)
curl -fsSL https://tailscale.com/install.sh | sh

# Prisijunkite prie Tailscale tinklo
# Parašyti Kasparui Šumskiui (kasparas.sumskis@mif.stud.vu.lt kad pridėtų prie tinklo)
sudo tailscale up

# Patikrinkite, ar visi mazgai matomi
tailscale status
```

Turėtumėte matyti visus 4 VPS nodes ir savo local machine.

**Setup SSH keys authentication (jei reikia tiesiogines prieigos):** 

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

#### Konfigūruoti Klasterio Mazgus (Jei reikia pakeisti serverius)

Čia rodoma konfiguracija dabar yra default.

**File:** [Replication/include/rules.hpp](Replication/include/rules.hpp)

```cpp
static NodeInfo CLUSTER[] = {
    {1, "100.117.80.126", 8001},  // Node 1
    {2, "100.70.98.49",   8002},  // Node 2
    {3, "100.118.80.33",  8003},  // Node 3
    {4, "100.116.151.88", 8004},  // Node 4
};
```

#### Konfigūruoti HTTP Serverį (Jei reikia pakeisti serverius)
**File:** [server/include/routes.hpp](server/include/routes.hpp)

```cpp
const std::vector<std::string> CLUSTER_NODES = {
  "100.117.80.126:7001",  // Node 1
  "100.70.98.49:7001",    // Node 2
  "100.118.80.33:7001",   // Node 3
  "100.116.151.88:7001"   // Node 4
};

// Užsilikes map'as kai naudojame tunelius apeiti tailscale tinklą.
static const std::unordered_map<std::string, std::string> CONTROL_PLANE_TUNNEL_MAP = {
  {"100.117.80.126:8001", "100.117.80.126:8001"},  // Direct
  {"100.70.98.49:8002",   "100.70.98.49:8002"},    // Direct
  {"100.118.80.33:8003",  "100.118.80.33:8003"},   // Direct
  {"100.116.151.88:8004", "100.116.151.88:8004"}   // Direct
};
```

**Minėtų senų tunelių routing**: `server/start_tunnels.sh` Sistema dabar naudoja tiesioginį Tailscale ryšį.

**Po konfigūracijos keitimo, reikia perkompiliuoti:**
```bash
cd Replication && make clean && make all
cd ../server && make clean && make all
```


### Kompiliuoti B+ Tree DB

```bash
cd btree
make all
./build/main  # CLI interaktyvus režimas
```

### Deploy Remote Cluster

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

**Deploy scriptai automatiškai:**
- Kompiliuoja lokaliai (`make all`)
- Upload binaries per SSH
- Sustabdo senus procesus
- Paleidžia naujus procesus
- Raft election vyksta automatiškai (~3 sek)

```bash
cd Replication

# Turint priega prie serverio, galima duomenų bazės mazagą nužudyti su ./kill.sh
./kill.sh
```


### Kompiliuoti ir Paleisti HTTP Serverį

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

### VPS Port'ų Schema

| Node | Physical IP | Tailscale IP | Client Port | Repl Port | Control Port |
|------|-------------|--------------|-------------|-----------|--------------|
| 1 | 207.180.251.206 | 100.117.80.126 | 7001 | 7002 | 8001 |
| 2 | 167.86.66.60 | 100.70.98.49 | 7001 | 7002 | 8002 |
| 3 | 167.86.83.198 | 100.118.80.33 | 7001 | 7002 | 8003 |
| 4 | 167.86.81.251 | 100.116.151.88 | 7001 | 7002 | 8004 |

## Naudojimas

### Vienos db CLI (Interaktyvus)

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

### Viso Clusterio CLI

```bash
cd Replication

# Cluster status
./client status  # arba ./client leader

# Tiesiai nurodyti mazgą pagal alias
./client node1 GET user01        # Skaityti iš Node 1 (follower reads)
./client node2 SET user01 Matas  # Rašyti į Node 2 (turi būti leader!)
./client node3 DEL user01        # Trinti per Node 3 (turi būti leader!)
./client node4 GETFF user 10     # Range query iš Node 4

# Arba rpateikti ip andresą ir portą:
./client 100.70.98.49 7001 GET user01
```

### HTTP API

```bash
# ?nodeId parametras leidžia pasirinkti mazgą

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

# Cluster info
curl http://localhost:8080/api/cluster/status   # Klasterio būsena
```

**Node Selection Logika:**
- **GET operacijos**: Veikia ant bet kurio mazgo (eventually consistent reads)
- **SET/DEL operacijos**: Turi eiti į lyderį (gražina klaidą jei ne leader)
- **Default (be nodeId)**: Automatinis leader discovery

#### **Web GUI**

1. Atidaryti naršyklę: http://localhost:8080
2. **Node Selection Dropdown**:
   - Auto (Leader Discovery) - numatytasis
   - Node 1 (100.117.80.126)
   - Node 2 (100.70.98.49)
   - Node 3 (100.118.80.33)
   - Node 4 (100.116.151.88)
3. Funkcijos:
   - **CRUD** - Kurti/skaityti/atnaujinti/trinti
   - **Cluster** - Klasterio būsena

## Testavimas

```bash
# Praeinama dauguma http routes, nužudomas lyderis, prikeliamas, vėl atliekami testai
./test_operation.sh
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
- **Heartbeat timeout**: 2000ms

## Žr. Taip Pat

- [B+ Tree README](btree/README.md)
- [Replication README](Replication/readme.md)
- [HTTP Server/ WEB app README](server/README.md)
