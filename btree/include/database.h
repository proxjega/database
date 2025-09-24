#pragma once

#include <string>
#include <filesystem>

class Page;

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
    bool WritePage(Page &PageToWrite);
};
