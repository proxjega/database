#pragma once

#include "internalpage.h"
#include "leafpage.h"
#include "page.h"
#include "logger.hpp"
#include <cstdint>
#include <string>
#include <filesystem>

class Page;
class BasicPage;
class LeafPage;

using std::string;
namespace fs = std::filesystem;

static constexpr std::size_t MAX_KEY_LENGTH = 255;
static constexpr std::size_t MAX_VALUE_LENGTH = 2048;

/**
 * @brief Main Database class. Has all of the functionality methods (get, set, remove)
 * as well as private page operations (read page, write page)
 *
 */
class Database {
private:
    string name;
    fs::path pathToDatabaseFile;

    WAL wal;
    bool RecoverFromWal();

    // Page operations
    Page ReadPage(uint32_t pageID) const;
    MetaPage ReadMetaPage() const;
    bool WriteBasicPage(BasicPage &PageToWrite) const;
    bool UpdateMetaPage(MetaPage &PageToWrite) const;
    void SplitLeafPage(LeafPage &LeafToSplit);
    void SplitInternalPage(InternalPage &InternalToSplit);

    public:
    // Constructor
    explicit Database(const string &name);

    // Accessors
    string getName() const;
    fs::path getPath() const;

    // Main operations
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

    // methods for getting/writing lsn to metapage
    uint64_t getLSN();
    bool writeLSN(uint64_t LSNToWrite);

    // Wrapper metodai WAL metodams, kad būtų patogiau koduot.
    uint64_t ExecuteLogSetWithLSN(const string &key, const string &value);
    uint64_t ExecuteLogDeleteWithLSN(const string &key);

    bool ApplyReplication(WalRecord walRecord);

    uint64_t GetWalSequenceNumber() const { return wal.GetCurrentSequenceNumber(); }

    vector<WalRecord> GetWalRecordsSince(uint64_t lastKnownLsn);
    void ResetLogState();

    // For Debug
    void CoutDatabase() const;

};
