#include "../include/database.h"
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <stdexcept>
#include "../include/page.h"
#include "../include/internalpage.h"
#include "../include/leafpage.h"

using std::ofstream;
using std::ios;
using std::ifstream;
using std::memcpy;
using std::cout;

// ---------------- Database ----------------
Database::Database(const string &name) : name(name), wal(name) {
    fs::path fileName(name + ".db");
    fs::path folderName = "data";
    this->pathToDatabaseFile = folderName / fileName;

    fs::create_directories(folderName);
    if (!fs::exists(this->pathToDatabaseFile)) {
        // Create new database
        ofstream DatabaseFile(this->pathToDatabaseFile.string(), ios::binary);
        if (!DatabaseFile) {
            throw std::runtime_error("Error creating database file\n");
        }

        // Create meta page
        MetaPageHeader header{};
        header.lastPageID = 1;
        header.rootPageID = 1;
        header.lastSequenceNumber = 1;
        MetaPage MetaPage1(header);
        if (!this->UpdateMetaPage(MetaPage1)) {
            throw std::runtime_error("Error updating meta page\n");
        }

        // Create root page
        LeafPage RootPage(1);
        if (!this->WriteBasicPage(RootPage)) {
            throw std::runtime_error("Error writing first page\n");
        }

        cout << "Database created successfully: " << this->pathToDatabaseFile << "\n";
    } else {
        // Database file exists — check if WAL recovery is needed
        if (this->wal.HasPendingRecords()) {
            cout << "Recovering from WAL...\n";
            if (!this->RecoverFromWal()) {
                throw std::runtime_error("WAL recovery failed");
            }
            cout << "WAL recovery complete.\n";
        } else {
            cout << "No WAL recovery needed.\n";
        }
    }
}

string Database::getName() const {
    return name;
}

fs::path Database::getPath() const {
    return pathToDatabaseFile;
}

Page Database::ReadPage(uint32_t pageID) const {
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
    if (databaseFile.fail()) {
        throw std::runtime_error("Logical read error (maybe short read) for page " + std::to_string(pageID));
    }
    if (databaseFile.bad()) {
        throw std::runtime_error("I/O error while reading page " + std::to_string(pageID));
    }

    return page;
}

MetaPage Database::ReadMetaPage() const {
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

bool Database::WriteBasicPage(BasicPage &pageToWrite) const {
    ofstream databaseFile(this->getPath().string(), ios::in | ios::out | ios::binary);
    if (!databaseFile) {
        return false;
    }

    uint32_t pageID = pageToWrite.Header()->pageID;
    databaseFile.seekp(pageID * Page::PAGE_SIZE, ios::beg);
    if (!databaseFile.good()) {
        return false;
    }

    databaseFile.write(pageToWrite.mData, Page::PAGE_SIZE);
    return !!databaseFile;
}

bool Database::UpdateMetaPage(MetaPage &PageToWrite) const {
    ofstream databaseFile(this->getPath().string(), ios::in | ios::out | ios::binary);
    if (!databaseFile) {
        return false; // could not open
    }
    databaseFile.seekp(0, ios::beg);
    if (!databaseFile.good()) {
        return false;
    }

    databaseFile.write(PageToWrite.mData, Page::PAGE_SIZE);
    return !!databaseFile;
}

std::optional<leafNodeCell> Database::Get(const string& key) const {
    // check keys length
    if (key.length() > MAX_KEY_LENGTH) {
        cout << "Key is too long!\n";
        return std::nullopt;
    }

    // get root page id
    MetaPage CurrentMetaPage;
    CurrentMetaPage = this->ReadPage(0);
    uint32_t rootPageID = CurrentMetaPage.Header()->rootPageID;
    if (rootPageID == 0) {
        throw std::runtime_error("rootPageID is zero!");
    }

    // read root page
    BasicPage currentPage = this->ReadPage(rootPageID);

    // loop to leaf
    while (!currentPage.Header()->isLeaf){
        InternalPage internal(currentPage);
        uint32_t pageID = internal.FindPointerByKey(key);
        currentPage = this->ReadPage(pageID);
    }

    //get key from leaf page (if exists)
    LeafPage leaf(currentPage);
    auto cell = leaf.FindKey(key);
    if (cell.has_value()){
        cout << cell->value << "\n";
        return cell;
    }

    cout << "Key not found!\n";
    return std::nullopt;
}

bool Database::Set(const string& key, const string& value){
    // check strings length
    if (key.length() > MAX_KEY_LENGTH || value.length() > MAX_VALUE_LENGTH) {
        cout << "Key or value is too long!\n";
        return false;
    }

    // this->wal.LogSet(key, value); nzn ar pries patikrinant duombazes struktura ar po.

    // get root page id
    MetaPage MetaPage1;
    MetaPage1 = this->ReadPage(0);
    uint32_t rootPageID = MetaPage1.Header()->rootPageID;
    if (rootPageID == 0) {
        throw std::runtime_error("rootPageID is zero!");
    }

    this->wal.LogSet(key, value);

    //read root page
    BasicPage currentPage = this->ReadPage(rootPageID);

    //loop to leaf page
    while (!currentPage.Header()->isLeaf){
        InternalPage internal(currentPage);
        //debug
        uint32_t pageID = internal.FindPointerByKey(key);
        currentPage = this->ReadPage(pageID);
    }

    // Try to insert
    LeafPage leaf(currentPage);
    if (leaf.WillFit(key, value)) {
        leaf.InsertKeyValue(key, value);
        this->WriteBasicPage(leaf);
    }
    // If doesnt fit - optimize and then try
    else {
        leaf = leaf.Optimize();
        if (leaf.WillFit(key, value)) {
            leaf.InsertKeyValue(key, value);
            this->WriteBasicPage(leaf);
        }
        // If still doesnt fit - split
        else {
            this->SplitLeafPage(leaf);
            this->Set(key, value);
        }
    }
    cout << "OK\n";
    return true;
}

void Database::SplitLeafPage(LeafPage& LeafToSplit) {
#ifdef DEBUG
    std::cout << "Splitting leaf page: " << LeafToSplit.Header()->pageID << std::endl;
#endif
    // re-read page before working to get the newest data
    LeafToSplit = this->ReadPage(LeafToSplit.Header()->pageID);
    uint16_t rightPart = LeafToSplit.Header()->numberOfCells / 2;
    uint16_t leftPart = LeafToSplit.Header()->numberOfCells - rightPart;
    string keyToMoveToParent = LeafToSplit.GetKeyValue(LeafToSplit.Offsets()[leftPart-1]).key;

    // check if the parent needs to be splitted
    uint32_t parentID = LeafToSplit.Header()->parentPageID;
    if (parentID != 0) {
        InternalPage parent = this->ReadPage(parentID);
        bool fit = parent.WillFit(keyToMoveToParent, LeafToSplit.Header()->pageID);
        if (!fit) {
            this->SplitInternalPage(parent);
            this->SplitLeafPage(LeafToSplit);
            return;
        }
    }

    // get new children IDs
    MetaPage Meta;
    Meta = this->ReadPage(0);
    uint32_t Child1ID = LeafToSplit.Header()->pageID;
    uint32_t Child2ID = Meta.Header()->lastPageID + 1;
    Meta.Header()->lastPageID++;

    // create 2 new children and assign sibling pointers
    LeafPage Child1(Child1ID);
    memcpy(Child1.Special(), &Child2ID, sizeof(uint32_t));
    LeafPage Child2(Child2ID);
    memcpy(Child2.Special(), LeafToSplit.Special(), sizeof(uint32_t));

    //split leaf into 2 leaves
    uint16_t i = 0;
    for (i = 0; i < leftPart; i++) {
        auto cell = LeafToSplit.GetKeyValue(LeafToSplit.Offsets()[i]);
        Child1.InsertKeyValue(cell.key, cell.value);
    }
    for (; i < LeafToSplit.Header()->numberOfCells; i++) {
        auto cell = LeafToSplit.GetKeyValue(LeafToSplit.Offsets()[i]);
        Child2.InsertKeyValue(cell.key, cell.value);
    }

    //add key to parent or create parent
    if (parentID == 0) {
        // get new parent id
        uint32_t newParentID = Meta.Header()->lastPageID + 1;
        Meta.Header()->lastPageID++;

        //create parent and insert pointers
        InternalPage Parent(newParentID);
        Parent.InsertKeyAndPointer(keyToMoveToParent, Child1ID);
        memcpy(Parent.Special(), &Child2ID, sizeof(Child2ID));

        // make parent the root
        Meta.Header()->rootPageID = newParentID;

        // change parent pointers
        Child1.Header()->parentPageID = newParentID;
        Child2.Header()->parentPageID = newParentID;

        // write pages
        this->WriteBasicPage(Parent);
        this->WriteBasicPage(Child1);
        this->WriteBasicPage(Child2);
        this->UpdateMetaPage(Meta);
    }
    else {
        // get parent
        InternalPage Parent = this->ReadPage(parentID);

        //update parent pointers
        Child1.Header()->parentPageID = parentID;
        Child2.Header()->parentPageID = parentID;

        //insert key and pointers
        Parent.InsertKeyAndPointer(keyToMoveToParent, Child1ID); //ADD CHECK FOR FREE SPACE!
        Parent.UpdatePointerToTheRightFromKey(keyToMoveToParent, Child2ID);

        //write pages
        this->WriteBasicPage(Parent);
        this->WriteBasicPage(Child1);
        this->WriteBasicPage(Child2);
        this->UpdateMetaPage(Meta);
    }
}

void Database::SplitInternalPage(InternalPage& InternalToSplit){
#ifdef DEBUG
    std::cout << "Splitting internal page: " << InternalToSplit.Header()->pageID << std::endl;
#endif
    InternalToSplit = this->ReadPage(InternalToSplit.Header()->pageID);

    uint16_t total = InternalToSplit.Header()->numberOfCells;
    uint16_t mid = total / 2;
    string keyToMoveToParent = InternalToSplit.GetKeyAndPointer(InternalToSplit.Offsets()[mid]).key;

    // check if the parent needs to be splitted
    uint32_t parentID = InternalToSplit.Header()->parentPageID;
    if (parentID != 0) {
        InternalPage parent = this->ReadPage(parentID);
        bool fit = parent.WillFit(keyToMoveToParent, InternalToSplit.Header()->pageID);
        if (!fit) {
            this->SplitInternalPage(parent);
            this->SplitInternalPage(InternalToSplit);
            return;
        }
    }

    // read meta page (for last page ID)
    MetaPage Meta;
    Meta = this->ReadPage(0);
    uint32_t Child1ID = InternalToSplit.Header()->pageID;
    uint32_t Child2ID = Meta.Header()->lastPageID + 1;

    // create 2 new childs
    InternalPage Child1(Child1ID);
    InternalPage Child2(Child2ID);
    Meta.Header()->lastPageID++;

    //split internal into 2 internals
    for (uint16_t i = 0; i < mid; i++) {
        auto cell = InternalToSplit.GetKeyAndPointer(InternalToSplit.Offsets()[i]);
        Child1.InsertKeyAndPointer(cell.key, cell.childPointer);
    }
    //copy pointer of middle key (that will be moved to parent) to child1 special
    uint32_t pointerOfKeyToMoveToParent = InternalToSplit.GetKeyAndPointer(InternalToSplit.Offsets()[mid]).childPointer;
    memcpy(Child1.Special(), &pointerOfKeyToMoveToParent, sizeof(pointerOfKeyToMoveToParent));

    //copy cells to second child and assing special pointer (to the most right child)
    for (uint16_t i = mid + 1; i < total; i++) {
        auto cell = InternalToSplit.GetKeyAndPointer(InternalToSplit.Offsets()[i]);
        Child2.InsertKeyAndPointer(cell.key, cell.childPointer);
    }
    memcpy(Child2.Special(), InternalToSplit.Special(), sizeof(pointerOfKeyToMoveToParent));

    //update child2 children parentpointers (child1 id is the same so it not needed to be updated)
    for (uint16_t j = 0; j < Child2.Header()->numberOfCells; j++) {
        auto cell = Child2.GetKeyAndPointer(Child2.Offsets()[j]);
        BasicPage GrandchildPage = this->ReadPage(cell.childPointer);
        GrandchildPage.Header()->parentPageID = Child2ID;
        this->WriteBasicPage(GrandchildPage);
    }
    {
    BasicPage GrandchildPage = this->ReadPage(*Child2.Special());
    GrandchildPage.Header()->parentPageID = Child2ID;
    this->WriteBasicPage(GrandchildPage);
    }
     //add key to parent or create parent
    if (parentID == 0) {
        // get new parent id
        uint32_t newParentID = Meta.Header()->lastPageID + 1;
        Meta.Header()->lastPageID++;

        //create parent and insert pointers
        InternalPage Parent(newParentID);
        Parent.InsertKeyAndPointer(keyToMoveToParent, Child1ID);
        memcpy(Parent.Special(), &Child2ID, sizeof(Child2ID));

        // make parent the root
        Meta.Header()->rootPageID = newParentID;

        // change parent pointers
        Child1.Header()->parentPageID = newParentID;
        Child2.Header()->parentPageID = newParentID;

        // write pages
        this->WriteBasicPage(Parent);
        this->WriteBasicPage(Child1);
        this->WriteBasicPage(Child2);
        this->UpdateMetaPage(Meta);
    }
    else {
        // get parent
        InternalPage Parent = this->ReadPage(parentID);

        //update parent pointers
        Child1.Header()->parentPageID = parentID;
        Child2.Header()->parentPageID = parentID;

        //insert key and pointers
        Parent.InsertKeyAndPointer(keyToMoveToParent, Child1ID); //ADD CHECK FOR FREE SPACE!
        Parent.UpdatePointerToTheRightFromKey(keyToMoveToParent, Child2ID);

        //write pages
        this->WriteBasicPage(Parent);
        this->WriteBasicPage(Child1);
        this->WriteBasicPage(Child2);
        this->UpdateMetaPage(Meta);
    }
}

vector<string> Database::GetKeys() const {
    MetaPage CurrentMetaPage;
    CurrentMetaPage = this->ReadPage(0);
    uint32_t rootPageID = CurrentMetaPage.Header()->rootPageID;
    if (rootPageID == 0) {
        throw std::runtime_error("rootPageID is zero!");
    }

    // read root page
    BasicPage currentPage = this->ReadPage(rootPageID);

    // loop to first leaf
    while (!currentPage.Header()->isLeaf){
        InternalPage internal(currentPage);
        uint16_t firstOffset = internal.Offsets()[0];
        uint32_t pageID = internal.GetKeyAndPointer(firstOffset).childPointer;
        currentPage = this->ReadPage(pageID);
    }

    vector<string> keys;
    LeafPage leaf(currentPage);
    while (*leaf.Special()!=0) {
        for (int i = 0; i < leaf.Header()->numberOfCells; i++) {
            keys.push_back(leaf.GetKeyValue(leaf.Offsets()[i]).key);
        }
        leaf = ReadPage(*leaf.Special());
    }
    return keys;
}

vector<string> Database::GetKeys(const string &prefix) const {
    vector<string> keys = this->GetKeys();
    vector<string> filteredKeys;
    uint32_t prefixLength = prefix.length();
    for (const auto& key : keys) {
        string substr = key.substr(0, prefixLength);
        if (substr == prefix) {
            filteredKeys.push_back(key);
        }
    }
    return filteredKeys;
}

vector<leafNodeCell> Database::GetFF(const string &key) const {

    vector<leafNodeCell> keyValuePairs;

    //check if key exists in database
    if (key.length() > MAX_KEY_LENGTH || !this->Get(key).has_value()) {
        cout << "No such key in database!\n";
        return keyValuePairs;
    }

    // get root page id
    MetaPage CurrentMetaPage;
    CurrentMetaPage = this->ReadPage(0);
    uint32_t rootPageID = CurrentMetaPage.Header()->rootPageID;
    if (rootPageID == 0) {
        throw std::runtime_error("rootPageID is zero!");
    }

    // read root page
    BasicPage currentPage = this->ReadPage(rootPageID);

    // loop to leaf
    while (!currentPage.Header()->isLeaf){
        InternalPage internal(currentPage);
        uint32_t pageID = internal.FindPointerByKey(key);
        currentPage = this->ReadPage(pageID);
    }

    //get keys from leaf page
    LeafPage leaf(currentPage);
    int16_t index = leaf.FindKeyIndex(key);// protection??
    for (uint16_t i = index; i < leaf.Header()->numberOfCells; i++) {
        auto cell = leaf.GetKeyValue(leaf.Offsets()[i]);
        keyValuePairs.push_back(cell);
    }

    // traverse other leaves
    while (*leaf.Special()!=0) {
        leaf = ReadPage(*leaf.Special());
        for (int i = 0; i < leaf.Header()->numberOfCells; i++) {
            keyValuePairs.push_back(leaf.GetKeyValue(leaf.Offsets()[i]));
        }
    }
    return keyValuePairs;
}

vector<leafNodeCell> Database::GetFF100(const string &key) const {

    vector<leafNodeCell> keyValuePairs;

    //check if key exists in database
    if (key.length() > MAX_KEY_LENGTH || !this->Get(key).has_value()) {
        cout << "No such key in database!\n";
        return keyValuePairs;
    }

    // get root page id
    MetaPage CurrentMetaPage;
    CurrentMetaPage = this->ReadPage(0);
    uint32_t rootPageID = CurrentMetaPage.Header()->rootPageID;
    if (rootPageID == 0) {
        throw std::runtime_error("rootPageID is zero!");
    }

    // read root page
    BasicPage currentPage = this->ReadPage(rootPageID);

    // loop to leaf
    while (!currentPage.Header()->isLeaf){
        InternalPage internal(currentPage);
        uint32_t pageID = internal.FindPointerByKey(key);
        currentPage = this->ReadPage(pageID);
    }
    uint16_t counter = 0;
    //get keys from leaf page
    LeafPage leaf(currentPage);
    int16_t index = leaf.FindKeyIndex(key);// protection??
    for (uint16_t i = index; i < leaf.Header()->numberOfCells; i++) {
        auto cell = leaf.GetKeyValue(leaf.Offsets()[i]);
        keyValuePairs.push_back(cell);
        counter++;
        if (counter == 100) {
            return keyValuePairs;
        }
    }

    // traverse other leaves
    while (*leaf.Special()!=0) {
        leaf = ReadPage(*leaf.Special());
        for (int i = 0; i < leaf.Header()->numberOfCells; i++) {
            keyValuePairs.push_back(leaf.GetKeyValue(leaf.Offsets()[i]));
            counter++;
            if (counter == 100) {
                return keyValuePairs;
            }
        }
    }
    return keyValuePairs;
}

vector<leafNodeCell> Database::GetFB(const string &key) const {

    vector<leafNodeCell> keyValuePairs;

    //check if key exists in database
    if (key.length() > MAX_KEY_LENGTH || !this->Get(key).has_value()) {
        cout << "No such key in database!\n";
        return keyValuePairs;
    }

    MetaPage CurrentMetaPage;
    CurrentMetaPage = this->ReadPage(0);
    uint32_t rootPageID = CurrentMetaPage.Header()->rootPageID;
    if (rootPageID == 0) {
        throw std::runtime_error("rootPageID is zero!");
    }

    // read root page
    BasicPage currentPage = this->ReadPage(rootPageID);

    // loop to first leaf
    while (!currentPage.Header()->isLeaf){
        InternalPage internal(currentPage);
        uint16_t firstOffset = internal.Offsets()[0];
        uint32_t pageID = internal.GetKeyAndPointer(firstOffset).childPointer;
        currentPage = this->ReadPage(pageID);
    }

    LeafPage leaf(currentPage);
    bool stop = false;
    while (!stop) {
        for (int i = 0; i < leaf.Header()->numberOfCells; i++) {
            auto cell = leaf.GetKeyValue(leaf.Offsets()[i]);
            keyValuePairs.push_back(cell);
            if (cell.key == key) {
                stop = true;
                break;
            }
        }
        leaf = ReadPage(*leaf.Special());
    }

    vector<leafNodeCell> reversed(keyValuePairs.rbegin(), keyValuePairs.rend());
    return reversed;
}

vector<leafNodeCell> Database::GetFB100(const string &key) const {

    vector<leafNodeCell> keyValuePairs;

    //check if key exists in database
    if (key.length() > MAX_KEY_LENGTH || !this->Get(key).has_value()) {
        cout << "No such key in database!\n";
        return keyValuePairs;
    }

    MetaPage CurrentMetaPage;
    CurrentMetaPage = this->ReadPage(0);
    uint32_t rootPageID = CurrentMetaPage.Header()->rootPageID;
    if (rootPageID == 0) {
        throw std::runtime_error("rootPageID is zero!");
    }

    // read root page
    BasicPage currentPage = this->ReadPage(rootPageID);

    // loop to first leaf
    while (!currentPage.Header()->isLeaf){
        InternalPage internal(currentPage);
        uint16_t firstOffset = internal.Offsets()[0];
        uint32_t pageID = internal.GetKeyAndPointer(firstOffset).childPointer;
        currentPage = this->ReadPage(pageID);
    }

    LeafPage leaf(currentPage);
    bool stop = false;
    while (!stop) {
        for (int i = 0; i < leaf.Header()->numberOfCells; i++) {
            auto cell = leaf.GetKeyValue(leaf.Offsets()[i]);
            keyValuePairs.push_back(cell);
            if (cell.key == key) {
                stop = true;
                break;
            }
        }
        leaf = ReadPage(*leaf.Special());
    }

    vector<leafNodeCell> reversed(keyValuePairs.rbegin(), keyValuePairs.rbegin() + 100);
    return reversed;
}

bool Database::Remove(const string& key) {
    if (key.length() > MAX_KEY_LENGTH) {
        cout << "Key is too long!\n";
        return false;
    }

    // this->wal.LogDelete(key); nzn ar pries patikrinant duombazes struktura ar po.

    // get root page id
    MetaPage CurrentMetaPage;
    CurrentMetaPage = this->ReadPage(0);
    uint32_t rootPageID = CurrentMetaPage.Header()->rootPageID;
    if (rootPageID == 0) {
        throw std::runtime_error("rootPageID is zero!");
    }

    this->wal.LogDelete(key);

    // read root page
    BasicPage currentPage = this->ReadPage(rootPageID);

    // loop to leaf
    while (!currentPage.Header()->isLeaf){
        InternalPage internal(currentPage);
        uint32_t pageID = internal.FindPointerByKey(key);
        currentPage = this->ReadPage(pageID);
    }

    //get key from leaf page (if exists)
    LeafPage leaf(currentPage);
    auto cell = leaf.FindKey(key);
    if (!cell.has_value()) {
        return false;
    }

    leaf.RemoveKey(key);
    this->WriteBasicPage(leaf);
    cout << "Removed key: " << key << "\n";
    return true;

}

void Database::Optimize(){
    uintmax_t oldSize = 0;
    try {
        oldSize = std::filesystem::file_size(this->pathToDatabaseFile);
    } catch (const std::filesystem::filesystem_error& e) {
        std::cerr << "Error: " << e.what() << '\n';
    }
    Database OptimizedDb(this->name + "optimized");

    MetaPage CurrentMetaPage;
    CurrentMetaPage = this->ReadPage(0);
    uint32_t rootPageID = CurrentMetaPage.Header()->rootPageID;
    if (rootPageID == 0) {
        throw std::runtime_error("rootPageID is zero!");
    }

    // read root page
    BasicPage currentPage = this->ReadPage(rootPageID);

    // loop to first leaf
    while (!currentPage.Header()->isLeaf){
        InternalPage internal(currentPage);
        uint16_t firstOffset = internal.Offsets()[0];
        uint32_t pageID = internal.GetKeyAndPointer(firstOffset).childPointer;
        currentPage = this->ReadPage(pageID);
    }

    vector<string> keys;
    LeafPage leaf(currentPage);
    while (*leaf.Special()!=0) {
        for (int i = 0; i < leaf.Header()->numberOfCells; i++) {
            auto cell = leaf.GetKeyValue(leaf.Offsets()[i]);
            OptimizedDb.Set(cell.key, cell.value);
        }
        leaf = ReadPage(*leaf.Special());
    }

    uintmax_t newSize = 0;
    try {
        newSize = std::filesystem::file_size(OptimizedDb.pathToDatabaseFile);
    } catch (const std::filesystem::filesystem_error& e) {
        std::cerr << "Error: " << e.what() << '\n';
    }

    try {
        std::filesystem::rename(this->pathToDatabaseFile, this->name + "Old.db");
    } catch (const std::filesystem::filesystem_error& e) {
        std::cerr << "Error: " << e.what() << '\n';
    }

    try {
        std::filesystem::rename(OptimizedDb.pathToDatabaseFile, this->pathToDatabaseFile);
    } catch (const std::filesystem::filesystem_error& e) {
        std::cerr << "Error: " << e.what() << '\n';
    }

    try {
        std::filesystem::remove(this->name + "Old.db");
    } catch (const std::filesystem::filesystem_error& e) {
        std::cerr << "Error deleting file: " << e.what() << '\n';
    }
    cout << "Optimized successfully. Freed " << oldSize - newSize << " bytes.\n";
}

bool Database::RecoverFromWal() {
    auto records = this->wal.ReadAll();
    if (records.empty()) {
        std::cout << "RecoverFromWal: No WAL records to apply.\n";
        return true;
    }

    try {
        std::cout << "RecoverFromWal: Applying " << records.size() << " records...\n";

        for (const auto &record : records) {
            if (record.operation == WalOperation::SET) {
                if(!this->Set(record.key, record.value)) {
                    std::cerr << "RecoverFromWal: Failed to SET key: ["
                                  << record.key << "] with value: ["
                                  << record.value << "]\n";
                }
            } else if (record.operation == WalOperation::DELETE) {
                if(!this->Remove(record.key)) {
                    std::cerr << "RecoverFromWal: Failed to DELETE key: ["
                                  << record.key << "]\n";
                }
            }
        }

        this->wal.ClearAll();
        return true;
    } catch (const std::exception &e) {
        std::cerr << "Recovery failed: " << e.what() << "\n";
        return false;
    }
}

void Database::CoutDatabase() const {
    MetaPage CurrentMetaPage = ReadMetaPage();
    CurrentMetaPage.Header()->CoutHeader();
    uint32_t pagenum = CurrentMetaPage.Header()->lastPageID;
    cout << "pagenum = " << pagenum << "\n\n";
    for (uint32_t i = 1; i <= pagenum; i++) {
        BasicPage page = ReadPage(i);
        if (page.Header()->isLeaf) {
            LeafPage leaf(page);
            leaf.CoutPage();
        }
        else {
            InternalPage internal(page);
            internal.CoutPage();
        }
    }
}
