# B+ Tree Duomenų Bazė

Puslapiais pagrįsta B+ medžio implementacija su Write-Ahead Logging (WAL) palaikymu ir crash recovery funkcionalumu.

## Architektūra

```
┌────────────────────────────────────────────────────────┐
│                  CLI Interface                          │
│         (main.cpp - interaktyvus režimas)              │
└─────────────────┬──────────────────────────────────────┘
                  │
┌─────────────────▼──────────────────────────────────────┐
│              Database Class                             │
│  ┌──────────────┐  ┌──────────────┐  ┌─────────────┐ │
│  │     Get      │  │     Set      │  │   Remove    │ │
│  │   (search)   │  │   (insert)   │  │   (delete)  │ │
│  └──────────────┘  └──────────────┘  └─────────────┘ │
│                                                         │
│  ┌──────────────┐  ┌──────────────┐  ┌─────────────┐ │
│  │    GETFF     │  │    GETFB     │  │  Optimize   │ │
│  │  (forward)   │  │  (backward)  │  │  (rebuild)  │ │
│  └──────────────┘  └──────────────┘  └─────────────┘ │
└─────────────────┬──────────────────────────────────────┘
                  │
┌─────────────────▼──────────────────────────────────────┐
│                WAL (Write-Ahead Log)                    │
│  - Įrašoma prieš kiekvieną Set/Remove operaciją       │
│  - Recovery mechanizmas (replay on open)               │
│  - Formatas: seq\top\tkey\tvalue                       │
└─────────────────┬──────────────────────────────────────┘
                  │
┌─────────────────▼──────────────────────────────────────┐
│            B+ Tree Page Manager                         │
│                                                         │
│  MetaPage (page 0)                                     │
│  ┌────────────────────────────────────────────┐       │
│  │ root_page_id │ last_page_id │ total_keys   │       │
│  └────────────────────────────────────────────┘       │
│                                                         │
│  LeafPages                    InternalPages            │
│  ┌──────────────┐            ┌──────────────┐        │
│  │ Key | Value  │            │ Key | PageID │        │
│  │ Key | Value  │            │ Key | PageID │        │
│  │ ...          │            │ ...          │        │
│  │ → next_leaf  │            └──────────────┘        │
│  └──────────────┘                                      │
│                                                         │
└─────────────────┬──────────────────────────────────────┘
                  │
┌─────────────────▼──────────────────────────────────────┐
│              Disk Storage (*.db failas)                 │
│  - Fiksuoto dydžio puslapiai (4096 baitų)             │
│  - Random access (fseek/fread/fwrite)                  │
└─────────────────────────────────────────────────────────┘
```

## Funkcijos

- ✅ **CRUD Operacijos**: Get, Set, Remove
- ✅ **Range Queries**: GETFF (forward), GETFB (backward)
- ✅ **WAL**: Automatinis recovery po crash
- ✅ **Page Splitting**: Automatinis puslapių dalijimas
- ✅ **Lazy Deletion**: Žymėjimas kaip ištrinta (ištrina tik Optimize)
- ✅ **Optimize**: Medžio perkūrimas, ištrintų įrašų šalinimas
- ✅ **Interactive CLI**: Komandų eilutės sąsaja

## Kompiliavimas

### Reikalavimai

```bash
# C++ kompiliatorius
sudo apt update
sudo apt install -y build-essential g++
```

### Build

```bash
cd /kelias/iki/database/btree

# Kompiliuoti
make all

# Išvalyti
make clean
```

## Naudojimas

### Interaktyvus Režimas

```bash
./build/main

# CLI komandos:
> SET user01 Jonas
OK

> GET user01
Jonas

> SET user02 Petras
OK

> GETFF user 10
user01: Jonas
user02: Petras

> DEL user01
OK

> OPTIMIZE
Optimized

> EXIT
```

### Palaikomos Komandos

| Komanda | Sintaksė | Aprašymas |
|---------|----------|-----------|
| `SET` | `SET <key> <value>` | Nustatyti raktą |
| `GET` | `GET <key>` | Gauti rakto reikšmę |
| `DEL` | `DEL <key>` | Ištrinti raktą (lazy) |
| `GETFF` | `GETFF <key> [count]` | Priekinė užklausa (n raktų nuo key) |
| `GETFB` | `GETFB <key> [count]` | Atbulinė užklausa (n raktų iki key) |
| `OPTIMIZE` | `OPTIMIZE` | Pertvarkyti DB, pašalinti ištrintus |
| `EXIT` | `EXIT` arba `QUIT` | Išeiti |

### Pavyzdžiai

```bash
# Sukurti keletą įrašų
> SET city01 Vilnius
> SET city02 Kaunas
> SET city03 Klaipėda
> SET city04 Šiauliai
> SET city05 Panevėžys

# Gauti vieną įrašą
> GET city03
Klaipėda

# Forward range (3 raktai nuo city02)
> GETFF city02 3
city02: Kaunas
city03: Klaipėda
city04: Šiauliai

# Backward range (2 raktai iki city04)
> GETFB city04 2
city03: Klaipėda
city04: Šiauliai

# Ištrinti
> DEL city03
OK

# Perkurti (pašalinti city03 fiziškai)
> OPTIMIZE
Optimized

# Patikrinti
> GET city03
Key not found
```

## Failų Struktūra

```
btree/
├── include/
│   ├── database.h           # Database klasė
│   ├── page.h               # Page bazinė klasė
│   ├── leafpage.h           # LeafPage implementacija
│   ├── internalpage.h       # InternalPage implementacija
│   ├── metapage.h           # MetaPage (puslapio 0)
│   └── logger.hpp           # WAL implementacija
│
├── src/
│   ├── database.cpp         # Pagrindinė DB logika
│   ├── main.cpp             # CLI interaktyvi sąsaja
│   ├── leafpage.cpp         # Lapų puslapių logika
│   ├── internalpage.cpp     # Vidinių puslapių logika
│   └── metapage.cpp         # Meta puslapio logika
│
├── build/
│   └── main                 # Sukompiliuotas binary
│
├── Makefile                 # Build konfigūracija
└── README.md                # Šis failas
```

## Techniniai Detaliai

### Puslapių Struktūra

**MetaPage (puslapio 0):**
```cpp
struct MetaPage {
    uint32_t root_page_id;   // Root puslapio ID
    uint32_t last_page_id;   // Paskutinio puslapio ID
    uint32_t total_keys;     // Bendras raktų skaičius
}
```

**LeafPage:**
```cpp
struct LeafPage {
    // Header
    uint16_t cell_count;     // Ląstelių skaičius
    uint16_t free_space;     // Laisvos vietos offset

    // Cells (key-value pairs)
    Cell[] cells;

    // Special pointers
    uint32_t next_leaf_id;   // Kito lapo ID (range queries)
    uint32_t prev_leaf_id;   // Ankstesnio lapo ID
}
```

**InternalPage:**
```cpp
struct InternalPage {
    // Header
    uint16_t cell_count;
    uint16_t free_space;

    // Cells (key + child page ID)
    Cell[] cells;

    // Special pointer
    uint32_t leftmost_child; // Kairiausiasis vaikas
}
```

### WAL Formatas

Kiekvienas WAL įrašas:
```
<seq>\t<op>\t<key>\t<value>
```

Pavyzdys (`database.wal`):
```
1	SET	user01	Jonas
2	SET	user02	Petras
3	DEL	user01
4	SET	user03	Vardas
```

### Page Splitting

Kai puslapis pilnas:
1. Sukuriamas naujas puslapis
2. Ląstelės padalijamos pusiau
3. Vidurinis raktas pakelamas į parent
4. Parent'as skaidomas jei pilnas (rekursyviai)
5. Naujas root sukuriamas jei reikia

### Recovery Procesas

Atidarant DB:
1. Nuskaitomas `database.wal` failas
2. Kiekvienas WAL įrašas "replay'inamas"
3. Atnaujinamas medis su WAL operacijomis
4. DB būsena atstatoma į paskutinę teisingą

## Apribojimai

```cpp
static constexpr size_t MAX_KEY_LENGTH = 255;      // 255 baitai
static constexpr size_t MAX_VALUE_LENGTH = 2048;   // 2KB
static constexpr size_t PAGE_SIZE = 4096;          // 4KB puslapiai
```

## Konfigūracija

Redaguoti `include/database.h`:

```cpp
class Database {
public:
    static constexpr size_t MAX_KEY_LENGTH = 255;
    static constexpr size_t MAX_VALUE_LENGTH = 2048;
    // ...
}
```

Perkompiliuoti:
```bash
make clean && make all
```

## Testavimas

### Bazinis Testas

```bash
./build/main
> SET test value
> GET test
# Turėtų grąžinti: value

> DEL test
> GET test
# Turėtų grąžinti: Key not found arba empty
```

### Daug Įrašų Testas

```bash
# Sukurti 1000 įrašų
for i in {1..1000}; do
    echo "SET key$(printf "%04d" $i) value$i"
done | ./build/main

# Testuoti range query
./build/main
> GETFF key0500 10
# Turėtų grąžinti 10 įrašų nuo key0500
```

### Crash Recovery Testas

```bash
# Paleisti DB
./build/main
> SET test1 value1
> SET test2 value2
# Nutraukti procesą (Ctrl+C)

# Paleisti iš naujo
./build/main
> GET test1
# Turėtų grąžinti: value1 (atstatyta iš WAL)
```

## Debugging

Kompiliuoti su debug režimu:
```bash
# Makefile jau turi -g -DDEBUG
make clean && make all
```

Naudoti gdb:
```bash
gdb ./build/main
(gdb) break main
(gdb) run
(gdb) continue
```

## Problemų Sprendimas

### "Failed to open database file"

```bash
# Patikrinti teises
ls -la database.db

# Sukurti katalogą jei reikia
mkdir -p /kelias/iki/database/btree/build
```

### "Page full, cannot insert"

- Maksimalus rakto ilgis viršytas (>255 baitų)
- Maksimalus reikšmės ilgis viršytas (>2048 baitų)
- Patikrinti `MAX_KEY_LENGTH` ir `MAX_VALUE_LENGTH`

### WAL failas labai didelis

```bash
# Perkurti DB (išvalo WAL)
./build/main
> OPTIMIZE
```

### Segmentation fault

```bash
# Kompiliuoti su debug
make clean && make all

# Paleisti su gdb
gdb ./build/main
(gdb) run
# Įvesti komandą kuri sukelia crash
(gdb) backtrace
```

## Performance

### Operacijų Sudėtingumas

| Operacija | Laiko Sudėtingumas | Aprašymas |
|-----------|-------------------|-----------|
| GET | O(log n) | Medžio paieška |
| SET | O(log n) + amortized split | Įterpimas su dalijimais |
| DEL | O(log n) | Lazy deletion |
| GETFF | O(log n + k) | k - rezultatų skaičius |
| GETFB | O(log n + k) | k - rezultatų skaičius |
| OPTIMIZE | O(n log n) | Pilnas rebuild |

### Optimizacija

**Dideliems duomenų kiekiams:**
1. Didinti `PAGE_SIZE` (pvz. 8192 arba 16384)
2. Naudoti OPTIMIZE retai (tik kai daug ištrinta)
3. Batch insertai su vienu OPTIMIZE pabaigoje

**Disk I/O:**
- Puslapiai cache'inami atmintyje
- Rašoma tik WAL append (greita)
- Optimize daro pilną rebuild (lėta)

## Integracijos

Ši DB naudojama:

1. **Standalone CLI** - Šiame kataloge
2. **Replication System** - `../Replication/` (leader/follower)
3. **HTTP API** - `../server/` (per DbClient wrapper)

## Žr. Taip Pat

- [Pagrindinio Projekto README](../README.md)
- [Replikacijos Sistema](../Replication/readme.md)
- [HTTP Serveris](../server/README.md)
