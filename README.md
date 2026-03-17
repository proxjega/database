# Distributed Key-Value Database

This project is an implementation of a distributed key-value database built using C++. The system consists of several main components: a page-based B+ Tree database, a Write-Ahead Logging (WAL) mechanism for reliable data storage, and Raft-style replication, which allows multiple nodes to maintain a consistent data state.

The cluster runs 4 nodes – one leader and several followers. The leader handles write operations and replicates WAL entries to followers, while read operations can be performed on any node. If the leader fails, the system automatically elects a new leader.

Clients can connect to the database via a CLI client, HTTP API, or a web interface built with Vue.js. The web application allows CRUD operations, range queries, and real-time cluster monitoring.

## Architecture

```
┌────────────────────────────────────────────────────────────────┐
│                    Browser (Vue.js SPA)                        │
│                     HTTP: localhost:8080                       │
└────────────────────────────┬───────────────────────────────────┘
                             │
┌────────────────────────────▼───────────────────────────────────┐
│              Lithium HTTP Server (C++)                         │
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
│              Replication cluster (4 nodes)                     │
│  ┌──────────────────┐  ┌──────────────────┐                    │
│  │  Leader          │  │  Followers(2,3,4)│                    │
│  │  Port: 7001      │  │  Ports: 7101-7104│                    │
│  │  Repl: 7002      │  │                  │                    │
│  └────────┬─────────┘  └──────────────────┘                    │
│           │                                                    │
│  ┌────────▼─────────────────────────────────────┐              │
│  │  B+ Tree Database + WAL                      │              │
│  │  - Page storage (16KB puslapiai)             │              │
│  │  - Write-Ahead Logging                       │              │
│  │  - CRUD operations                           │              │
│  │  - Range queries (GETFF/GETFB)               │              │
│  └──────────────────────────────────────────────┘              │
└────────────────────────────────────────────────────────────────┘
```

## Components

### 1. B+ Tree Database ([btree/](btree/))

Page-based B+ Tree database with WAL support:
- **Page types**: MetaPage, LeafPage, InternalPage
- **Operations**: Get, Set, Remove, Optimize
- **WAL**: Automatic recovery after crash
- **Page size**: 16KB (configurable)

**See [btree/README.md](btree/README.md)**

### 2. Replication System ([Replication/](Replication/))

Raft-style leader-follower replication:
- **4 Nodes**: 1 leader, 3 followers
- **Automatic Elections**: Heartbeat + voting system
- **WAL Replication**: Leader → Followers streaming
- **Failover**: Automatic new leader election

**See [Replication/readme.md](Replication/readme.md)**

### 3. HTTP Server + Web GUI ([server/](server/))

Lithium C++ HTTP server with Vue.js 3 application:
- **API Endpoints**: GET, SET, DELETE, GETFF, GETFB, OPTIMIZE, ...
- **Leader Discovery**: Dynamic leader detection
- **Static Files**: Vue.js SPA hosting
- **Web GUI**: Bootstrap 5 responsive UI

**See [server/README.md](server/README.md)**

## Quick Start

### Requirements (Ubuntu/Linux)

```bash
# C++ compiler and libraries
sudo apt update
sudo apt install -y build-essential cmake g++ libssl-dev libboost-all-dev

# Node.js 24 (for web application)
curl -o- https://raw.githubusercontent.com/nvm-sh/nvm/v0.40.3/install.sh | bash
\. "$HOME/.nvm/nvm.sh"
nvm install 24
node -v # Should print "v24.x.x"
npm -v # Should print "11.6.x"
```

### System Configuration

- Client API: 100.x.x.x:7001 (via Tailscale)
- Control plane: 100.x.x.x:8001-8004 (via Tailscale)

#### Tailscale Setup and Node Config (one-time)
**Required for all remote nodes and local machine:**

```bash
# Install Tailscale (Ubuntu/Debian)
curl -fsSL https://tailscale.com/install.sh | sh

# Connect to Tailscale network
# Contact Kaspar Šumski (kasparas.sumskis@mif.stud.vu.lt to add to network)
sudo tailscale up

# Check all nodes are visible
tailscale status
```

You should see 4 VPS nodes and your local machine.

**Setup SSH keys authentication (if direct access is needed):** 

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

#### Configure Cluster Nodes (if servers need changing)

Current configuration is default.

**File:** [Replication/include/rules.hpp](Replication/include/rules.hpp)

```cpp
static NodeInfo CLUSTER[] = {
    {1, "100.117.80.126", 8001},  // Node 1
    {2, "100.70.98.49",   8002},  // Node 2
    {3, "100.118.80.33",  8003},  // Node 3
    {4, "100.116.151.88", 8004},  // Node 4
};
```

#### Configure HTTP Server (if servers need changing)
**File:** [server/include/routes.hpp](server/include/routes.hpp)

```cpp
const std::vector<std::string> CLUSTER_NODES = {
  "100.117.80.126:7001",  // Node 1
  "100.70.98.49:7001",    // Node 2
  "100.118.80.33:7001",   // Node 3
  "100.116.151.88:7001"   // Node 4
};

// Old tunnel map (not used now, system uses direct Tailscale connection)
static const std::unordered_map<std::string, std::string> CONTROL_PLANE_TUNNEL_MAP = {
  {"100.117.80.126:8001", "100.117.80.126:8001"},  // Direct
  {"100.70.98.49:8002",   "100.70.98.49:8002"},    // Direct
  {"100.118.80.33:8003",  "100.118.80.33:8003"},   // Direct
  {"100.116.151.88:8004", "100.116.151.88:8004"}   // Direct
};
```
**Old tunnel routing: server/start_tunnels.sh is no longer used; system now uses direct Tailscale connection.**

**After configuration changes, recompile:**
```bash
cd Replication && make clean && make all
cd ../server && make clean && make all
```


### Compile B+ Tree DB (optional)

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

**Deploy scripts automatically:**

- Compile locally (make all)
- Upload binaries via SSH
- Stop old processes
- Start new processes
- Raft election occurs automatically (~3 sec)

```bash
cd Replication

# If you have access to a server, you can kill a database node
./kill.sh
```


### Compile and Run HTTP Server

```bash
cd server

# First time: setup Lithium framework
./scripts/setup_lithium

# Compile server
make all

# Compile Vue.js app
cd webapp && npm install && npm run build && cd ..

# Run server
./server_app  # Port 8080
```

**Access**: http://localhost:8080

**Detailed documentation** [server/README.md](server/README.md)

---

### VPS Ports Scheme

| Node | Physical IP | Tailscale IP | Client Port | Repl Port | Control Port |
|------|-------------|--------------|-------------|-----------|--------------|
| 1 | 207.180.251.206 | 100.117.80.126 | 7001 | 7002 | 8001 |
| 2 | 167.86.66.60 | 100.70.98.49 | 7001 | 7002 | 8002 |
| 3 | 167.86.83.198 | 100.118.80.33 | 7001 | 7002 | 8003 |
| 4 | 167.86.81.251 | 100.116.151.88 | 7001 | 7002 | 8004 |

## Usage

### One db CLI (Interaktyvus)

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

### Whole cluster CLI

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
# ?nodeId parameter allows selecting the node

# SET (must go to leader)
curl -X POST "http://localhost:8080/api/set/user01?nodeId=2" \
  -H "Content-Type: application/json" \
  -d '{"value":"Matas"}'

# GET (can read from any node - follower reads)
curl "http://localhost:8080/api/get/user01?nodeId=1"  # Read from Node 1
curl "http://localhost:8080/api/get/user01?nodeId=3"  # Read from Node 3

# DELETE (must go to leader)
curl -X POST "http://localhost:8080/api/del/user01?nodeId=2"

# Range queries with node selection
curl "http://localhost:8080/api/getff/user?count=10&nodeId=4"

# Cluster info
curl http://localhost:8080/api/cluster/status   # Cluster status
```

**Node Selection Logika:**
- **GET operacijos**: Works on any node (eventually consistent reads)
- **SET/DEL operacijos**: Must go into leader (raises error if not leader)
- **Default (be nodeId)**: Automatic leader discovery

#### **Web GUI**

1. Open browser: http://localhost:8080
2. **Node Selection Dropdown**:
   - Auto (Leader Discovery) - default
   - Node 1 (100.117.80.126)
   - Node 2 (100.70.98.49)
   - Node 3 (100.118.80.33)
   - Node 4 (100.116.151.88)
3. Functions:
   - **CRUD** - Create/read/update/delete
   - **Cluster** - Cluster status

## Testing

```bash
# Run through most HTTP routes, kill the leader, restart, rerun tests
./test_operation.sh
```

## Technologies

- **C++20** - Main programming language
- **B+ Tree** - Data structure
- **WAL** - Write-Ahead Logging
- **Raft** - Consensus algorithm (simplified)
- **TCP/IP** - Network communication
- **Lithium** - HTTP server framework
- **Vue.js 3** - Frontend framework
- **Bootstrap 5** - CSS framework
- **Webpack 5** - Module bundler

## Coding Convention

Follow: https://www.geeksforgeeks.org/cpp/naming-convention-in-c/

## Limitations

- **Max key length**: 255 bytes
- **Max value length**: 2048 bytes
- **Page size**: 16384 bytes
- **Cluster size**: 4 nodes (fixed)
- **Heartbeat timeout**: 2000ms

## See Also

- [B+ Tree README](btree/README.md)
- [Replication README](Replication/readme.md)
- [HTTP Server/ WEB app README](server/README.md)
