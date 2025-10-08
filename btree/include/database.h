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

    Page ReadPage(uint32_t pageID);
    MetaPage ReadMetaPage();
    bool WriteBasicPage(BasicPage &PageToWrite);
    bool UpdateMetaPage(MetaPage &PageToWrite);

    void SplitLeafPage(LeafPage& LeafToSplit, const string& key, const string& value);
    void SplitInternalPage(InternalPage& InternalToSplit, const string& key, const uint32_t &pointer1, const uint32_t &pointer2);

    std::optional<leafNodeCell> Get(const string& key);
    bool Set(const string& key, const string& value);

    void CoutDatabase();
    void FoutDatabase();

};
