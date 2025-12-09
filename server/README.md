# HTTP Serveris su Vue.js GUI

## HTTP serveris su Vue.js

## Instalacija (Ubuntu/Linux)

```bash
# C++ kompiliatorius ir bibliotekos
sudo apt update
sudo apt install -y build-essential cmake g++ libssl-dev libboost-all-dev

# Node.js 18+
curl -o- https://raw.githubusercontent.com/nvm-sh/nvm/v0.40.3/install.sh | bash
\. "$HOME/.nvm/nvm.sh"
nvm install 24
node -v # Should print "v24.x.x"
npm -v # Should print "11.6.x"
```

---

## Setup

Serveriu ip adresų nustatymas:
**Routes.hpp** [include/routes.hpp](include/routes.hpp)

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

**Prieš pradedant:**

1. Įsitikinti kad `Replication/include/rules.hpp` naudoja Tailscale IPs (100.x.x.x)
2. Įsitikinti kad `server/include/routes.hpp` naudoja Tailscale IPs (100.x.x.x)

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

-   Upload binaries per SSH
-   Sustabdo senus procesus
-   Paleidžia naujus procesus

Palaukti ~3 sek kol klasteris išrenks leader.

---

Output:

```
Starting SSH tunnels to remote cluster nodes...
Creating tunnels for client API (7001) and control plane (8001-8004)...
✓ SSH tunnels established:
  - Client API: 7101-7104
  - Control plane: 8001-8004
```

**Start HTTP Server:**

```bash
./server_app
```

**Prieiga**: http://localhost:8080

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
-   **Cluster** - Klasterio būsena ir lyderio informacija

## API Endpoint'ai

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

```bash
# Įvarių tipų operacijos, prieš ir po naujo lyderio
./test_operations.sh

```

## Žr. Taip Pat

-   [Pagrindinio Projekto README](../README.md)
-   [Replikacijos Sistemos Dokumentacija](../Replication/readme.md)
