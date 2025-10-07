#include "../include/database.h"
#include <cstdint>
#include <cstring>
#include <fstream>
#include <optional>
#include <stdexcept>
#include "../include/page.h"
#include "../include/internalpage.h"
#include "../include/leafpage.h"
#include <memory>

using std::ofstream;
using std::ios;
using std::ifstream;
using std::memcpy;

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

    if (databaseFile.eof()) {
        throw std::runtime_error("Unexpected EOF while reading page " + std::to_string(pageID));
    }
    else if (databaseFile.fail()) {
        throw std::runtime_error("Logical read error (maybe short read) for page " + std::to_string(pageID));
    }
    else if (databaseFile.bad()) {
        throw std::runtime_error("I/O error while reading page " + std::to_string(pageID));
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
    ofstream databaseFile(this->getPath().string(), ios::in | ios::out | ios::binary);
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
    // check keys length
    if (key.length() > 256) {
        cout << "Key is too long!\n";
        return std::nullopt;
    }
    
    // get root page id
    MetaPage CurrentMetaPage;
    CurrentMetaPage = this->ReadPage(0);
    uint32_t rootPageID = CurrentMetaPage.Header()->rootPageID;
    if (rootPageID == 0) throw std::runtime_error("rootPageID is zero!");

    // read root page
    BasicPage currentPage = this->ReadPage(rootPageID);

    // loop to leaf
    while (currentPage.Header()->isLeaf != true){ 
        InternalPage internal(currentPage);
        uint32_t pageID = internal.FindPointerByKey(key);
        currentPage = this->ReadPage(pageID);
    }

    //get key from leaf page (if exists)
    LeafPage leaf(currentPage);
    auto cell = leaf.FindKey(key);
    if (cell.has_value()) return cell;
    else {
        cout << "Key not found!\n";
        return std::nullopt;
    }
}

bool Database::Set(const string& key, const string& value){
    // check strings length
    if (key.length() > 256 || value.length() > 256) {
        cout << "Key or value is too long!\n";
        return false;
    }

    // get root page id
    MetaPage MetaPage1;
    MetaPage1 = this->ReadPage(0);
    uint32_t rootPageID = MetaPage1.Header()->rootPageID;
    if (rootPageID == 0) throw std::runtime_error("rootPageID is zero!");

    //read root page
    BasicPage currentPage = this->ReadPage(rootPageID);
    
    //loop to leaf page
    while (currentPage.Header()->isLeaf != true){ 
        InternalPage internal(currentPage);
        uint32_t pageID = internal.FindPointerByKey(key);
        currentPage = this->ReadPage(pageID);
    }

    //get key from leaf page (if exists)
    LeafPage leaf(currentPage);
    auto cell = leaf.FindKey(key);
    bool check = false;
    if (cell.has_value()) {
        leaf.RemoveKey(key);
        check = leaf.InsertKeyValue(key, value);
    }
    else check = leaf.InsertKeyValue(key, value);

    // if check == false (couldnot insert) - split
    if (!check) this->SplitLeafPage(leaf, key, value);
    else this->WriteBasicPage(leaf);
    return true;
}

void Database::SplitLeafPage(LeafPage& LeafToSplit, const string& key, const string& value) {
    // read meta page (for last page ID)
#ifdef DEBUG
    std::cout << "Splitting page: " << LeafToSplit.Header()->pageID << std::endl;
#endif

    MetaPage CurrentMetaPage;
    CurrentMetaPage = this->ReadPage(0);
    uint32_t Child1ID = LeafToSplit.Header()->pageID;
    uint32_t Child2ID = CurrentMetaPage.Header()->lastPageID + 1;

    // create 2 new childs and assign sibling pointers
    LeafPage Child1(Child1ID);
    memcpy(Child1.Special(), &Child2ID, sizeof(uint32_t));
    LeafPage Child2(Child2ID);
    memcpy(Child2.Special(), LeafToSplit.Special(), sizeof(uint32_t));
    CurrentMetaPage.Header()->lastPageID++;

    //split leaf into 2 leaves
    uint16_t smallerHalf = LeafToSplit.Header()->numberOfCells / 2;
    uint16_t BiggerHalf = LeafToSplit.Header()->numberOfCells - smallerHalf;
    uint16_t i = 0;
    for (i = 0; i < BiggerHalf; i++) {
        auto cell = LeafToSplit.GetKeyValue(LeafToSplit.Offsets()[i]);
        Child1.InsertKeyValue(cell.key, cell.value);
    }
    string keyToMoveToParent = LeafToSplit.GetKeyValue(LeafToSplit.Offsets()[i-1]).key;
    for (; i < LeafToSplit.Header()->numberOfCells; i++) {
        auto cell = LeafToSplit.GetKeyValue(LeafToSplit.Offsets()[i]);
        Child2.InsertKeyValue(cell.key, cell.value);
    }

    //insert key value that originally caused the split
    if (key > keyToMoveToParent) Child2.InsertKeyValue(key, value);
    else Child1.InsertKeyValue(key, value);

    //add key to parent or create parent
    uint32_t parentID = LeafToSplit.Header()->parentPageID;
    if (parentID == 0) {
        uint32_t newParentID = CurrentMetaPage.Header()->lastPageID + 1;
        InternalPage Parent(newParentID);
        CurrentMetaPage.Header()->lastPageID++;
        Parent.InsertKeyAndPointer(keyToMoveToParent, Child1ID);
        memcpy(Parent.Special(), &Child2ID, sizeof(Child2ID));
        CurrentMetaPage.Header()->rootPageID = newParentID;
        if (!this->WriteBasicPage(Parent)) throw std::runtime_error("Error writing parent page!\n");
        Child1.Header()->parentPageID = newParentID;
        Child2.Header()->parentPageID = newParentID;
    }
    else {
        InternalPage Parent = this->ReadPage(parentID);
        Parent.InsertKeyAndPointer(keyToMoveToParent, Child1ID); //ADD CHECK FOR FREE SPACE!
        memcpy(Parent.Special(), &Child2ID, sizeof(Child2ID));
        this->WriteBasicPage(Parent);
    }

    //write pages
    this->WriteBasicPage(Child1);
    this->WriteBasicPage(Child2);
    this->UpdateMetaPage(CurrentMetaPage);
}

void Database::CoutDatabase(){
    MetaPage CurrentMetaPage = ReadMetaPage();
    uint32_t pagenum = CurrentMetaPage.Header()->lastPageID;
    cout << "pagenum = " << pagenum << "\n\n";
    for (int i = 1; i <= pagenum; i++) {
        BasicPage page = ReadPage(i);
        if (page.Header()->isLeaf == true) {
            LeafPage leaf(page);
            leaf.CoutPage();
        }
        else {
            InternalPage internal(page);
            internal.CoutPage();
        }
    }
}
