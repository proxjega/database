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
    string getName() const;
    fs::path getPath() const;

    Page ReadPage(uint32_t pageID);
    MetaPage ReadMetaPage();
    bool WriteBasicPage(BasicPage &PageToWrite);
    bool UpdateMetaPage(MetaPage &PageToWrite);

    std::optional<leafNodeCell> Get(const string& key);
    bool Set(const string& key, const string& value);
};
