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
│  │  Lyderis (Node1) │  │  Sekėjai (2,3,4) │                    │
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

# Node.js 18+ (web aplikacijai)
curl -o- https://raw.githubusercontent.com/nvm-sh/nvm/v0.40.3/install.sh | bash
\. "$HOME/.nvm/nvm.sh"
nvm install 18
```

### 1. Kompiliuoti B+ Tree DB

```bash
cd btree
make all
./build/main  # CLI interaktyvus režimas
```

### 2. Kompiliuoti Replikacijos Sistemą

```bash
cd Replication
make all
```

### 3. Kompiliuoti HTTP Serverį + Web App

```bash
cd server

# Įdiegti Lithium framework (pirma kartą)
./scripts/setup_lithium

# Kompiliuoti C++ serverį
make all

# Kompiliuoti Vue.js aplikaciją
cd webapp
npm install
npm run build
```

### 4. Paleisti Visą Sistemą

**Terminalai 1-4: Klasteris**
```bash
cd Replication
./run 1  # Mazgas 1 (taps lyderiu)
./run 2  # Mazgas 2
./run 3  # Mazgas 3
./run 4  # Mazgas 4
```

Palaukti lyderio rinkimų (~3 sek):
```
[node X] [INFO] became LEADER term 1 with votes=3
```

**Terminalas 5: HTTP Serveris**
```bash
cd server
./server_app  # Port 8080
```

**Prieiga**: http://localhost:8080

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

```bash
cd Replication

# Rasti lyderį
./client leader

# Rašyti į lyderį
./client 127.0.0.1 7001 SET user01 Matas

# Skaityti iš bet kurio mazgo
./client 127.0.0.1 7101 GET user01

# Range užklausos
./client 127.0.0.1 7001 GETFF user 10
./client 127.0.0.1 7001 GETFB user99 10
```

### HTTP API

```bash
# SET
curl -X POST http://localhost:8080/api/set/user01 \
  -H "Content-Type: application/json" \
  -d '{"value":"Matas"}'

# GET
curl http://localhost:8080/api/get/user01

# DELETE
curl -X POST http://localhost:8080/api/del/user01

# Range (forward)
curl "http://localhost:8080/api/getff/user?count=10"

# Leader info
curl http://localhost:8080/api/leader
```

### Web GUI

1. Atidaryti naršyklę: http://localhost:8080
2. Naudoti web sąsają:
   - **CRUD** - Kurti/skaityti/atnaujinti/trinti
   - **Browse** - Naršyti su puslapiavimų
   - **Cluster** - Klasterio būsena

## Port'ų Schema

| Komponentas | Portas | Aprašymas |
|-------------|--------|-----------|
| HTTP Serveris | 8080 | Web GUI + API |
| Leader Client | 7001 | Rašymo operacijos |
| Leader Replication | 7002 | WAL streaming |
| Follower 1 Read | 7101 | Skaitymo operacijos |
| Follower 2 Read | 7102 | Skaitymo operacijos |
| Follower 3 Read | 7103 | Skaitymo operacijos |
| Follower 4 Read | 7104 | Skaitymo operacijos |
| Control Ports | 8001-8004 | Heartbeat, voting |

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
