#include "../include/database.h"
#include <fstream>
#include "../include/page.h" // Only include in .cpp if needed

using std::ofstream;
using std::ios;
using std::ifstream;

// ---------------- Database ----------------
Database::Database(const string &name) {
    this->name = name;
    fs::path fileName(name + ".db");
    fs::path folderName = "data";
    this->pathToDatabaseFile = folderName / fileName;

    fs::create_directories(folderName);

    ofstream DatabaseFile(this->pathToDatabaseFile.string(), ios::binary);
}

const string& Database::getName() const {
    return name;
}

const fs::path& Database::getPath() const {
    return pathToDatabaseFile;
}

Page Database::ReadPage(uint32_t pageID) {
    Page ReadPage;
    ifstream databaseFile(this->getPath().string(), ios::binary);
    databaseFile.seekg(pageID * ReadPage.PAGE_SIZE);
    databaseFile.read(ReadPage.mData, ReadPage.PAGE_SIZE);
    return ReadPage;
}

bool Database::WritePage(Page &PageToWrite) {
    ofstream databaseFile(this->getPath().string(), ios::binary);
    uint32_t pageID = PageToWrite.Header()->pageID; // add error check
    databaseFile.seekp(pageID * PageToWrite.PAGE_SIZE);
    databaseFile.write(PageToWrite.mData, PageToWrite.PAGE_SIZE); //add error check
    return true;
}
