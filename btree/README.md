# B+ Tree Duomenų Bazė

Puslapiais pagrįsta B+ medžio implementacija su Write-Ahead Logging (WAL) palaikymu ir crash recovery funkcionalumu.

## Funkcijos

- **CRUD Operacijos**: Get, Set, Remove
- **Range Queries**: GETFF (forward), GETFB (backward), GETKEYS, GETKEYS(prefix)
- **WAL**: Automatinis recovery po crash
- **Page Splitting**: Automatinis puslapių dalijimas
- **Lazy Deletion**: Žymėjimas kaip ištrinta (ištrina tik Optimize)
- **Optimize**: Medžio perkūrimas, ištrintų įrašų šalinimas

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

## Naudojimas(kode)
1) Sukurti klasės "Database" objektą
2) atlikti operacijas

### Operacijos:

```cpp
    std::optional<leafNodeCell> Get(const string &key) const;
    bool Set(const string& key, const string &value);
    vector<string> GetKeys() const;
    pagingResultKeysOnly GetKeysPaging(uint32_t pageSize, uint32_t pageNum) const;
    vector<leafNodeCell> GetKeysValues() const;
    pagingResult GetKeysValuesPaging(uint32_t pageSize, uint32_t pageNum) const;
    vector<string> GetKeys(const string &prefix) const;
    vector<leafNodeCell> GetFF(const string &key, uint32_t n) const;
    vector<leafNodeCell> GetFB(const string &key, uint32_t n) const;
    bool Remove(const string& key);
    void Optimize();
```

## Puslapių Struktūra

**MetaPage (puslapio 0):**
- Header
```cpp
struct MetaPageHeader {
    uint32_t rootPageID;
    uint32_t lastPageID;
    uint64_t keyNumber;
    uint64_t lastSequenceNumber;
}
```

**LeafPage:**
- PageHeader* Header();
```cpp
struct PageHeader {
    bool isLeaf;
    uint32_t pageID;
    uint32_t parentPageID;
    uint16_t numberOfCells;
    uint16_t offsetToStartOfFreeSpace;
    uint16_t offsetToEndOfFreeSpace;
    uint16_t offsetToStartOfSpecialSpace;
}
```

- uint16_t* Offsets();
- uint32_t* Special1();
- uint32_t* Special2();

**InternalPage:**
- PageHeader* Header();
```cpp
struct PageHeader {
    bool isLeaf;
    uint32_t pageID;
    uint32_t parentPageID;
    uint16_t numberOfCells;
    uint16_t offsetToStartOfFreeSpace;
    uint16_t offsetToEndOfFreeSpace;
    uint16_t offsetToStartOfSpecialSpace;
}
```

- uint16_t* Offsets();
- uint32_t* Special1();
- uint32_t* Special2();

## WAL Formatas

Kiekvienas WAL įrašas:
```
<seq>\t<op>\t<key>\t<value>
```

## Page Splitting

Kai puslapis pilnas:
1. Sukuriamas naujas puslapis
2. Ląstelės padalijamos pusiau
3. Vidurinis raktas pakelamas į parent
4. Parent'as skaidomas jei pilnas (rekursyviai)
5. Naujas root sukuriamas jei reikia

## Recovery Procesas

Atidarant DB:
1. Nuskaitomas `database.wal` failas
2. Kiekvienas WAL įrašas "replay'inamas"
3. Atnaujinamas medis su WAL operacijomis
4. DB būsena atstatoma į paskutinę teisingą

## Apribojimai

```cpp
static constexpr size_t MAX_KEY_LENGTH = 255;      // 255 baitai
static constexpr size_t MAX_VALUE_LENGTH = 2048;   // 2KB
static constexpr size_t PAGE_SIZE = 16384;          // 16KB puslapiai
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

## Optimizacija

**Dideliems duomenų kiekiams:**
1. Didinti `PAGE_SIZE`
2. Naudoti OPTIMIZE retai (tik kai daug ištrinta)
3. Batch insertai su vienu OPTIMIZE pabaigoje

**Disk I/O:**
- Puslapiai cache'inami atmintyje
- Rašoma tik WAL append (greita)
- Optimize daro pilną rebuild (lėta)

## Integracijos

Ši DB naudojama:

1. **Replication System** - `../Replication/` (leader/follower)
2. **HTTP API** - `../server/` (per DbClient wrapper)

## Žr. Taip Pat

- [Pagrindinio Projekto README](../README.md)
- [Replikacijos Sistema](../Replication/readme.md)
- [HTTP Serveris](../server/README.md)
