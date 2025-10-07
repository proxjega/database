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
    MetaPage MetaPage1(header);
    if(!this->UpdateMetaPage(MetaPage1)) throw std::runtime_error("Error updating meta page\n") ;
    
    //create rootpage
    LeafPage RootPage(1);
    if (!this->WriteBasicPage(RootPage)) throw std::runtime_error("Error writing first page\n");
}

string Database::getName() const {
    return name;
}

fs::path Database::getPath() const {
    return pathToDatabaseFile;
}

Page Database::ReadPage(uint32_t pageID) {
    Page page;

    ifstream databaseFile(this->getPath().string(), ios::in | ios::binary);
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

MetaPage Database::ReadMetaPage() {
    MetaPage page;

    ifstream databaseFile(this->getPath().string(), ios::in | ios::binary);
    if (!databaseFile) {
        throw std::runtime_error("Failed to open database file for reading");
    }

    databaseFile.seekg(0, ios::beg);
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
    ofstream databaseFile(this->getPath().string(), ios::in | ios::out | ios::binary);
    if (!databaseFile) {
        return false; 
    }

    uint32_t pageID = pageToWrite.Header()->pageID;
    databaseFile.seekp(pageID * Page::PAGE_SIZE, ios::beg);
    if (!databaseFile.good()) return false;

    databaseFile.write(pageToWrite.mData, Page::PAGE_SIZE);
    if (!databaseFile) return false;

    return true;
}

bool Database::UpdateMetaPage(MetaPage &PageToWrite) {
    ofstream databaseFile(this->getPath().string(), ios::out | ios::binary);
    if (!databaseFile) {
        return false; // could not open
    }
    databaseFile.seekp(0, ios::beg);
    if (!databaseFile.good()) return false;

    databaseFile.write(PageToWrite.mData, Page::PAGE_SIZE);
    if (!databaseFile) return false;
    return true;
}

std::optional<leafNodeCell> Database::Get(const string& key){
    MetaPage MetaPage1;
    MetaPage1 = this->ReadPage(0);
    uint32_t rootPageID = MetaPage1.Header()->rootPageID;
    if (rootPageID == 0) throw std::runtime_error("rootPageID is zero!");
    BasicPage currentPage = this->ReadPage(rootPageID);
    while (currentPage.Header()->isLeaf != true){ 
        InternalPage internal(currentPage);
        uint32_t pageID = internal.FindPointerByKey(key);
        currentPage = this->ReadPage(pageID);
    }
    LeafPage leaf(currentPage);
    auto cell = leaf.FindKey(key);
    if (cell.has_value()) return cell;
    else {
        cout << "Key not found!\n";
        return std::nullopt;
    }
}

bool Database::Set(const string& key, const string& value){
    MetaPage MetaPage1;
    MetaPage1 = this->ReadPage(0);
    uint32_t rootPageID = MetaPage1.Header()->rootPageID;
    if (rootPageID == 0) throw std::runtime_error("rootPageID is zero!");
    BasicPage currentPage = this->ReadPage(rootPageID);
    while (currentPage.Header()->isLeaf != true){ 
        InternalPage internal(currentPage);
        uint32_t pageID = internal.FindPointerByKey(key);
        currentPage = this->ReadPage(pageID);
    }
    LeafPage leaf(currentPage);
    auto cell = leaf.FindKey(key);
    if (cell.has_value()) ;//remove and insert??
}
