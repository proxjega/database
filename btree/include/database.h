#pragma once

#include "internalpage.h"
#include "leafpage.h"
#include "page.h"
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

public:
    // Constructor
    Database(const string &name);

    // Accessors
    string getName() const;
    fs::path getPath() const;

    // Page operations
    Page ReadPage(uint32_t pageID);
    MetaPage ReadMetaPage();
    bool WriteBasicPage(BasicPage &PageToWrite);
    bool UpdateMetaPage(MetaPage &PageToWrite);
    void SplitLeafPage(LeafPage &LeafToSplit);
    void SplitInternalPage(InternalPage &InternalToSplit);

    // Main operations
    std::optional<leafNodeCell> Get(const string &key);
    bool Set(const string& key, const string &value);
    vector<string> GetKeys();
    vector<string> GetKeys(const string &prefix);
    vector<leafNodeCell> GetFF(const string &key);
    vector<leafNodeCell> GetFF100(const string &key);
    vector<leafNodeCell> GetFB(const string &key);
    vector<leafNodeCell> GetFB100(const string &key);
    bool Remove(const string& key);
    void Optimize();

    // For Debug
    void CoutDatabase();
    void FoutDatabase();

};
