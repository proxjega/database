# B+ Tree Duomenų Bazė

Puslapiais pagrįsta B+ medžio implementacija su Write-Ahead Logging (WAL) palaikymu ir crash recovery funkcionalumu.

## Funkcijos

- **CRUD Operacijos**: Get, Set, Remove
- **Range Queries**: GETFF (forward), GETFB (backward)
- **WAL**: Automatinis recovery po crash
- **Page Splitting**: Automatinis puslapių dalijimas
- **Lazy Deletion**: Žymėjimas kaip ištrinta (ištrina tik Optimize)
- **Optimize**: Medžio perkūrimas, ištrintų įrašų šalinimas
-  **Interactive CLI**: Komandinės eilutės sąsaja

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
