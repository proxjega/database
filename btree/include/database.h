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

class Database {
private:
    string name;
    fs::path pathToDatabaseFile;

    WAL wal;
    bool RecoverFromWal();

    public:
    // Constructor
    explicit Database(const string &name);

    static constexpr std::size_t MAX_KEY_LENGTH = 255;
    static constexpr std::size_t MAX_VALUE_LENGTH = 255;

    // Accessors
    string getName() const;
    fs::path getPath() const;

    // Page operations
    Page ReadPage(uint32_t pageID) const;
    MetaPage ReadMetaPage() const;
    bool WriteBasicPage(BasicPage &PageToWrite) const;
    bool UpdateMetaPage(MetaPage &PageToWrite) const;
    void SplitLeafPage(LeafPage &LeafToSplit);
    void SplitInternalPage(InternalPage &InternalToSplit);

    // Main operations
    std::optional<leafNodeCell> Get(const string &key) const;
    bool Set(const string& key, const string &value);
    vector<string> GetKeys() const;
    vector<string> GetKeys(const string &prefix) const;
    vector<leafNodeCell> GetFF(const string &key) const;
    vector<leafNodeCell> GetFF100(const string &key) const;
    vector<leafNodeCell> GetFB(const string &key) const;
    vector<leafNodeCell> GetFB100(const string &key) const;
    bool Remove(const string& key);
    void Optimize();

    // For Debug
    void CoutDatabase() const;
    void FoutDatabase();

};
