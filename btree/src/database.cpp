#include "../include/database.h"
#include <cstdint>
#include <fstream>
#include <optional>
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

    //create meta page
    MetaPageHeader header;
    header.lastPageID = 1;
    header.rootPageID = 1;
    header.lastSequenceNumber = 1;
    MetaPage MetaPage(header);
    this->UpdateMetaPage(MetaPage);
    //create rootpage
    LeafPage RootPage(1);
    this->WriteBasicPage(RootPage);
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
    databaseFile.seekg(pageID * ReadPage.PAGE_SIZE, ios::beg);
    databaseFile.read(ReadPage.mData, ReadPage.PAGE_SIZE);
    return ReadPage;
}

bool Database::WriteBasicPage(BasicPage &PageToWrite) {
    ofstream databaseFile(this->getPath().string(), ios::binary | ios::app);
    uint32_t pageID = PageToWrite.Header()->pageID; // add error check
    databaseFile.seekp(pageID * PageToWrite.PAGE_SIZE);
    databaseFile.write(PageToWrite.mData, PageToWrite.PAGE_SIZE); //add error check
    return true;
}

bool Database::UpdateMetaPage(MetaPage &PageToWrite) {
    ofstream databaseFile(this->getPath().string(), ios::binary | ios::app);
    databaseFile.seekp(0, ios::beg);
    databaseFile.write(PageToWrite.mData, PageToWrite.PAGE_SIZE); //add error check
    return true;
}

std::optional<leafNodeCell> Database::Get(string key){
    MetaPage MetaPage = this->ReadPage(0);
    uint32_t rootPageID = MetaPage.Header()->rootPageID;
    BasicPage rootPage = this->ReadPage(rootPageID);
    if (rootPage.Header()->isLeaf==true) {
        LeafPage Page(rootPage);
        return Page.FindKey(key);
    }
    else {
        InternalPage Page(rootPage);
        Page.FindPointerByKey(key);
        // InternalPage.FindPointer or smth
    }
    return std::nullopt;
}

bool Database::Set(string key, string value){

}
