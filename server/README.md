# HTTP Serveris su Vue.js GUI

HTTP serveris su Vue.js SPA, palaikantis du deployment režimus:

-   **Režimas 1: Localhost Mazgai** - Lokalus klasteris (4 procesai vienoje mašinoje)
-   **Režimas 2: Remote VPS** - Remote klasteris (4 VPS) per SSH tunnels

---

## Instalacija (Ubuntu/Linux)

```bash
# C++ kompiliatorius ir bibliotekos
sudo apt update
sudo apt install -y build-essential cmake g++ libssl-dev libboost-all-dev

# SSH tunneling tools (Režimas B only)
sudo apt install -y sshpass autossh

# Node.js 18+
curl -o- https://raw.githubusercontent.com/nvm-sh/nvm/v0.40.3/install.sh | bash
\. "$HOME/.nvm/nvm.sh"
nvm install 24
node -v # Should print "v24.x.x"
npm -v # Should print "11.6.x"
```

---

## Setup

### 0. Konfigūruoti Deployment Režimą

**SVARBU:** Prieš kompiliuojant, pasirinkite deployment režimą ir sukonfigūruokite `routes.hpp`.

**File:** [include/routes.hpp](include/routes.hpp)

#### **Režimas 1: Localhost Mazgai**

Serveris jungiasi tiesiogiai prie localhost portų.

```cpp
// Line ~60: CONTROL_PLANE_TUNNEL_MAP - localhost control plane
static const std::unordered_map<std::string, std::string> CONTROL_PLANE_TUNNEL_MAP = {
  {"127.0.0.1:8001", "127.0.0.1:8001"},  // Node 1 control
  {"127.0.0.1:8002", "127.0.0.1:8002"},  // Node 2 control
  {"127.0.0.1:8003", "127.0.0.1:8003"},  // Node 3 control
  {"127.0.0.1:8004", "127.0.0.1:8004"}   // Node 4 control
};

// routes.hpp - Localhost configuration
// Line ~69: CLUSTER_NODES should point to localhost
const std::vector<std::string> CLUSTER_NODES = {
  "127.0.0.1:7101",  // Node 1 client API
  "127.0.0.1:7102",  // Node 2 client API
  "127.0.0.1:7103",  // Node 3 client API
  "127.0.0.1:7104"   // Node 4 client API
};

// Line ~257
string leader_host = "127.0.0.1";

// Line ~59: TAILSCALE_TO_LOCAL_MAP - NOT NEEDED for localhost
// (Comment out or leave empty)
```

#### **Režimas 2: Remote VPS (Current Default)**

Naudoja SSH tunnels. Serveris jungiasi per `127.0.0.1:710x` → Remote.

```cpp
// routes.hpp - Remote VPS configuration (CURRENT DEFAULT)
// Line ~50: CLUSTER_NODES - Local tunnel endpoints
const std::vector<std::string> CLUSTER_NODES = {
  "127.0.0.1:7101",  // SSH tunnel → Node 1:7001
  "127.0.0.1:7102",  // SSH tunnel → Node 2:7001
  "127.0.0.1:7103",  // SSH tunnel → Node 3:7001
  "127.0.0.1:7104"   // SSH tunnel → Node 4:7001
};

// Line ~41: CONTROL_PLANE_TUNNEL_MAP - Maps Tailscale to local tunnels
static const std::unordered_map<std::string, std::string> CONTROL_PLANE_TUNNEL_MAP = {
  {"100.117.80.126:8001", "127.0.0.1:8001"},  // Node 1
  {"100.70.98.49:8002",   "127.0.0.1:8002"},  // Node 2
  {"100.118.80.33:8003",  "127.0.0.1:8003"},  // Node 3
  {"100.116.151.88:8004", "127.0.0.1:8004"}   // Node 4
};

// Line ~59: TAILSCALE_TO_LOCAL_MAP - Translates REDIRECT responses
const static std::unordered_map<std::string, std::string> TAILSCALE_TO_LOCAL_MAP = {
  {"100.117.80.126", "127.0.0.1:7101"},  // Node 1
  {"100.70.98.49",   "127.0.0.1:7102"},  // Node 2
  {"100.118.80.33",  "127.0.0.1:7103"},  // Node 3
  {"100.116.151.88", "127.0.0.1:7104"}   // Node 4
};
```

---

### 1. Kompiliuoti HTTP Serverį

```bash
cd server

# Pirmas kartas: įdiegti Lithium framework'ą
./scripts/setup_lithium

# Kompiliuoti HTTP serverį
make all
```

### 2. Kompiliuoti Web Aplikaciją

```bash
cd webapp

# Įdiegti priklausomybes (tik pirma kartą)
npm install

# Sukurti produkcinį build'ą
npm run build
```

---

## Paleisti Sistemą

### Režimas 1: Localhost Mazgai

**Prieš pradedant:**

1. Įsitikinti kad `Replication/include/rules.hpp` naudoja `127.0.0.1`
2. Įsitikinti kad `server/include/routes.hpp` sukonfigūruotas localhost režimui

**Terminalai 1-4: Paleisti Replikacijos Klasterį**

```bash
cd Replication
./run 1  # Mazgas 1
./run 2  # Mazgas 2
./run 3  # Mazgas 3
./run 4  # Mazgas 4
```

Palaukti ~3 sek lyderio rinkimų:

```
[node X] [INFO] became LEADER term 1 with votes=3
```

**Terminalas 5: Paleisti HTTP Serverį**

```bash
cd server
./server_app
```

**Prieiga**: http://localhost:8080

**Nereikia SSH tunnels!** Visi procesai veikia vietinėje mašinoje.

---

### Režimas 2: Remote Klasteris per SSH Tunnels

**Prieš pradedant:**

1. Įsitikinti kad `Replication/include/rules.hpp` naudoja Tailscale IPs (100.x.x.x)
2. Įsitikinti kad `server/include/routes.hpp` sukonfigūruotas remote režimui

#### B.1. Remote VPS Setup

**Deploy į Remote Nodes:**

```bash
cd Replication

# Deploy į visus nodes (automatiškai compile, upload, restart)
./deploy_all.sh

# Arba deploy po vieną node
./deploy.sh 1
./deploy.sh 2
./deploy.sh 3
./deploy.sh 4
```

**Deploy script automatiškai:**

-   Kompiliuoja lokaliai
-   Upload binaries per SSH
-   Sustabdo senus procesus
-   Paleidžia naujus procesus

Palaukti ~3 sek kol klasteris išrenks leader.

---

#### B.2. Local Machine Setup

**Setup SSH Tunnels:**

```bash
cd server

# Make scripts executable (first time)
chmod +x start_tunnels.sh stop_tunnels.sh

# Start tunnels
./start_tunnels.sh
```

Output:

```
Starting SSH tunnels to remote cluster nodes...
Creating tunnels for client API (7001) and control plane (8001-8004)...
✓ SSH tunnels established:
  - Client API: 7101-7104
  - Control plane: 8001-8004
```

**Verify Tunnels:**

```bash
# Check ports listening
ss -tlnp | grep -E "710[1-4]|800[1-4]"

# Test connectivity
echo "GET test" | nc 127.0.0.1 7101
echo "GET test" | nc 127.0.0.1 7102
```

**Start HTTP Server:**

```bash
./server_app
```

**Prieiga**: http://localhost:8080

---

## SSH Tunnel Configuration

Tunnels bypass Tailscale network and connect to remote nodes via physical IPs.

**File**: [start_tunnels.sh](start_tunnels.sh)

Each node has 2 tunnels:

1. **Client API tunnel** (7101-7104 → remote 7001)
2. **Control Plane tunnel** (8001-8004 → remote 8001-8004)

**Cluster Mapping:**

| Local Port | Remote Node | Physical IP     | Remote Port | Purpose       |
| ---------- | ----------- | --------------- | ----------- | ------------- |
| 7101       | Node 1      | 207.180.251.206 | 7001        | Client API    |
| 7102       | Node 2      | 167.86.66.60    | 7001        | Client API    |
| 7103       | Node 3      | 167.86.83.198   | 7001        | Client API    |
| 7104       | Node 4      | 167.86.81.251   | 7001        | Client API    |
| 8001       | Node 1      | 207.180.251.206 | 8001        | Control Plane |
| 8002       | Node 2      | 167.86.66.60    | 8002        | Control Plane |
| 8003       | Node 3      | 167.86.83.198   | 8003        | Control Plane |
| 8004       | Node 4      | 167.86.81.251   | 8004        | Control Plane |

**IP Translation** ([routes.hpp](include/routes.hpp)):

HTTP serveris automatically translates Tailscale IPs → tunnel endpoints:

```cpp
static const std::unordered_map<std::string, std::string> TAILSCALE_TO_LOCAL_MAP = {
  {"100.117.80.126:7001", "127.0.0.1:7101"},  // Node 1
  {"100.70.98.49:7001",   "127.0.0.1:7102"},  // Node 2
  {"100.118.80.33:7001",  "127.0.0.1:7103"},  // Node 3
  {"100.116.151.88:7001", "127.0.0.1:7104"},  // Node 4
};
```

**Stop Tunnels:**

```bash
./stop_tunnels.sh
```

---

## Komponentai

### HTTP Serveris (C++)

**Pagrindiniai Failai:**

-   [main.cpp](main.cpp) - Serverio įėjimo taškas (port 8080)
-   [include/routes.hpp](include/routes.hpp) - API endpoint'ai + statinių failų pateikimas
-   [include/db_client.hpp](include/db_client.hpp) - DB kliento sąsaja
-   [src/db_client.cpp](src/db_client.cpp) - Kliento implementacija

### Web Aplikacija (Vue.js)

**Vieta:** [webapp/](webapp/)

**Puslapiai:**

-   **Home** - Pagrindinis puslapis
-   **CRUD** - Kurti, skaityti, atnaujinti, trinti operacijos
-   **Browse** - Puslapiuojamas DB naršymas (forward/backward)
-   **Cluster** - Klasterio būsena ir lyderio informacija

Žr. [webapp/README.md](webapp/README.md) detalesnei dokumentacijai.

## API Endpoint'ai

### Duomenų Bazės Operacijos

| Endpoint                      | Metodas | Aprašymas           | Response           |
| ----------------------------- | ------- | ------------------- | ------------------ |
| `GET /api/get/:key`           | GET     | Gauti rakto reikšmę | -                  |
| `POST /api/set/:key`          | SET     | Nustatyti raktą     | `{"value": "..."}` |
| `POST /api/del/:key`          | DEL     | Ištrinti raktą      | -                  |
| `GET /api/getff/:key?count=N` | GETFF   | Priekinė užklausa   | -                  |
| `GET /api/getfb/:key?count=N` | GETFB   | Atbulinė užklausa   | -                  |
| `POST /api/optimize`          | -       | Pertvarkyti DB      | -                  |

### Klasterio Valdymas

| Endpoint          | Metodas | Aprašymas                                   |
| ----------------- | ------- | ------------------------------------------- |
| `GET /api/leader` | GET     | Rasti dabartinį lyderį (host, port, status) |
| `GET /health`     | GET     | Serverio būsenos patikrinimas               |

### Pavyzdžiai

```bash
# Nustatyti raktą
curl -X POST http://localhost:8080/api/set/user01 \
  -H "Content-Type: application/json" \
  -d '{"value":"Jonas Jonaitis"}'

# Gauti raktą
curl http://localhost:8080/api/get/user01

# Ištrinti raktą
curl -X POST http://localhost:8080/api/del/user01

# Priekinė užklausa (10 raktų nuo 'user')
curl "http://localhost:8080/api/getff/user?count=10"

# Rasti lyderį
curl http://localhost:8080/api/leader

# Patikrinti būseną
curl http://localhost:8080/health
```

### HTTP Serveris

```bash
make clean        # Pašalinti binary
make all          # Kompiliuoti serverį
./server_app      # Paleisti serverį
killall server_app # Sustabdyti serverį
```

### Web Aplikacija

```bash
cd webapp
npm install       # Įdiegti priklausomybes
npm run build     # Produkcinis build
npm run dev       # Development serveris (hot reload)
npm run watch     # Watch režimas (auto-rebuild)
```

## Testavimas

### Testuoti API Endpoint'us

```bash
# Paleisti serverį
./server_app

# Kitame terminale testuoti
curl http://localhost:8080/health
curl http://localhost:8080/api/leader
curl -X POST http://localhost:8080/api/set/test \
  -H "Content-Type: application/json" \
  -d '{"value":"sveikas"}'
curl http://localhost:8080/api/get/test
```

## Žr. Taip Pat

-   [Pagrindinio Projekto README](../README.md)
-   [Replikacijos Sistemos Dokumentacija](../Replication/readme.md)
