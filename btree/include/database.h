#pragma once

#include "page.h"
#include <string>
#include <filesystem>

class Page;
class BasicPage;

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
    const string& getName() const;
    const fs::path& getPath() const;

    Page ReadPage(uint32_t pageID);
    bool WriteBasicPage(BasicPage &PageToWrite);
    bool UpdateMetaPage(MetaPage &PageToWrite);

    std::optional<leafNodeCell> Get(string key);
    bool Set(string key, string value);
};
