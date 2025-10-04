#include "../include/database.h"
#include <cstdint>
#include <fstream>
#include <optional>
#include <stdexcept>
#include "../include/page.h"
#include "../include/internalpage.h"
#include "../include/leafpage.h"

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
    if (!DatabaseFile) throw std::runtime_error("Error creating database file\n");
    //create meta page
    MetaPageHeader header;
    header.lastPageID = 1;
    header.rootPageID = 1;
    header.lastSequenceNumber = 1;
    MetaPage MetaPage(header);
    if(!this->UpdateMetaPage(MetaPage)) throw std::runtime_error("Error updating meta page\n") ;
    //create rootpage
    LeafPage RootPage(1);
    if (!this->WriteBasicPage(RootPage)) throw std::runtime_error("Error writing first page\n");
}

const string& Database::getName() const {
    return name;
}

const fs::path& Database::getPath() const {
    return pathToDatabaseFile;
}

Page Database::ReadPage(uint32_t pageID) {
    Page page;

    ifstream databaseFile(this->getPath(), ios::in | ios::binary);
    if (!databaseFile) {
        throw std::runtime_error("Failed to open database file for reading");
    }

    databaseFile.seekg(pageID * Page::PAGE_SIZE, ios::beg);
    if (!databaseFile.good()) {
        throw std::runtime_error("Seek failed in ReadPage");
    }

    databaseFile.read(page.mData, Page::PAGE_SIZE);
    if (!databaseFile) {
        throw std::runtime_error("Read failed in ReadPage");
    }

    return page;
}

bool Database::WriteBasicPage(BasicPage &pageToWrite) {
    ofstream databaseFile(this->getPath(), ios::out | ios::binary);
    if (!databaseFile) {
        return false; // could not open
    }

    uint32_t pageID = pageToWrite.Header()->pageID;
    databaseFile.seekp(pageID * Page::PAGE_SIZE, ios::beg);
    if (!databaseFile.good()) return false;

    databaseFile.write(pageToWrite.mData, Page::PAGE_SIZE);
    if (!databaseFile) return false;

    return true;
}

bool Database::UpdateMetaPage(MetaPage &PageToWrite) {
    ofstream databaseFile(this->getPath(), ios::out | ios::binary);
    if (!databaseFile) {
        return false; // could not open
    }
    databaseFile.seekp(0, ios::beg);
    if (!databaseFile.good()) return false;

    databaseFile.write(PageToWrite.mData, Page::PAGE_SIZE);
    if (!databaseFile) return false;
    return true;
}

std::optional<leafNodeCell> Database::Get(string key){
    MetaPage MetaPage = this->ReadPage(0);
    uint32_t rootPageID = MetaPage.Header()->rootPageID;
    BasicPage currentPage = this->ReadPage(rootPageID);
    while (currentPage.Header()->isLeaf != true){ //here infinite loop!
        InternalPage Page(currentPage);
        uint32_t pageID = Page.FindPointerByKey(key);
        currentPage = this->ReadPage(pageID);
    }
    LeafPage Page(currentPage);
    auto cell = Page.FindKey(key);
    if (cell.has_value()) return cell;
    else {
        cout << "Key not found!\n";
        return std::nullopt;
    }
}

bool Database::Set(string key, string value){

}
