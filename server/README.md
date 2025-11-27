# HTTP Serveris su Vue.js GUI

### Intaliacija (Ubuntu/Linux)

```bash
# C++ kompiliatorius ir bibliotekos
sudo apt update
sudo apt install -y build-essential cmake g++ libssl-dev libboost-all-dev

# NODE JS
# Download and install nvm:
curl -o- https://raw.githubusercontent.com/nvm-sh/nvm/v0.40.3/install.sh | bash
\. "$HOME/.nvm/nvm.sh"
nvm install 24
node -v # Should print "v24.11.1".
npm -v # Should print "11.6.2".

### 1. Kompiliuoti HTTP Serverį

```bash
cd /kelias/iki/database/server

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

### 3. Paleisti Visą Sistemą

**Terminalai 1-4: Paleisti Replikacijos Klasterį**
```bash
cd ../Replication
./run 1  # Mazgas 1
./run 2  # Mazgas 2
./run 3  # Mazgas 3
./run 4  # Mazgas 4
```

**Terminalas 5: Paleisti HTTP Serverį**
```bash
cd /kelias/iki/database/server
./server_app
```

**Prieiga**: http://localhost:8080

## Komponentai

### HTTP Serveris (C++)

**Pagrindiniai Failai:**
- [main.cpp](main.cpp) - Serverio įėjimo taškas (port 8080)
- [include/routes.hpp](include/routes.hpp) - API endpoint'ai + statinių failų pateikimas
- [include/db_client.hpp](include/db_client.hpp) - DB kliento sąsaja
- [src/db_client.cpp](src/db_client.cpp) - Kliento implementacija


### Web Aplikacija (Vue.js)

**Vieta:** [webapp/](webapp/)

**Puslapiai:**
- **Home** - Pagrindinis puslapis
- **CRUD** - Kurti, skaityti, atnaujinti, trinti operacijos
- **Browse** - Puslapiuojamas DB naršymas (forward/backward)
- **Cluster** - Klasterio būsena ir lyderio informacija

Žr. [webapp/README.md](webapp/README.md) detalesnei dokumentacijai.

## API Endpoint'ai

### Duomenų Bazės Operacijos

| Endpoint | Metodas | Aprašymas | Response |
|----------|---------|-----------|--------------|
| `GET /api/get/:key` | GET | Gauti rakto reikšmę | - |
| `POST /api/set/:key` | SET | Nustatyti raktą | `{"value": "..."}` |
| `POST /api/del/:key` | DEL | Ištrinti raktą | - |
| `GET /api/getff/:key?count=N` | GETFF | Priekinė užklausa | - |
| `GET /api/getfb/:key?count=N` | GETFB | Atbulinė užklausa | - |
| `POST /api/optimize` | - | Pertvarkyti DB | - |

### Klasterio Valdymas

| Endpoint | Metodas | Aprašymas |
|----------|---------|-----------|
| `GET /api/leader` | GET | Rasti dabartinį lyderį (host, port, status) |
| `GET /health` | GET | Serverio būsenos patikrinimas |

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

- [Pagrindinio Projekto README](../README.md)
- [Replikacijos Sistemos Dokumentacija](../Replication/readme.md)
