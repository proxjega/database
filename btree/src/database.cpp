#include "../include/database.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <vector>
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
        header.keyNumber = 0;
        MetaPage Meta(header);
        if (!this->UpdateMetaPage(Meta)) {
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
/**
 * @brief Reads page from a disk.
 *
 * @param pageID pageID to read
 * @return
 */
Page Database::ReadPage(uint32_t pageID) const {
    Page page;

    // Open file for reading
    ifstream databaseFile(this->getPath().string(), ios::in | ios::binary);
    if (!databaseFile) {
        throw std::runtime_error("Failed to open database file for reading");
    }

    // Get to page's location
    databaseFile.seekg(pageID * Page::PAGE_SIZE, ios::beg);
    if (!databaseFile.good()) {
        throw std::runtime_error("Seek failed in ReadPage");
    }

    // Read page
    databaseFile.read(page.mData, Page::PAGE_SIZE);

    // Check errors
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

/**
 * @brief Reads meta page. Almost the same as ReadPage(0)
 *
 * @return
 */
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

/**
 * @brief Writes a BasicPage to a disk.
 *
 * @param pageToWrite
 * @return true on success
 */
bool Database::WriteBasicPage(BasicPage &pageToWrite) const {
    // Open database file
    ofstream databaseFile(this->getPath().string(), ios::in | ios::out | ios::binary);
    if (!databaseFile) {
        return false;
    }
    // Go to page location
    uint32_t pageID = pageToWrite.Header()->pageID;
    databaseFile.seekp(pageID * Page::PAGE_SIZE, ios::beg);
    if (!databaseFile.good()) {
        return false;
    }
    // Write page buffer
    databaseFile.write(pageToWrite.mData, Page::PAGE_SIZE);
    return !!databaseFile;
}

/**
 * @brief Updates meta page. Almost the same as WriteBasicPage
 *
 * @param PageToWrite
 * @return true on success
 */
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

/**
 * @brief Basic Get operation. Gets key:value pair
 *
 * @param key
 * @return leafNodeCell struct (key:value pair) or nullopt (null)
 */
std::optional<leafNodeCell> Database::Get(const string& key) const {
    // check keys length
    if (key.length() > MAX_KEY_LENGTH) {
        cout << "Key is too long!\n";
        return std::nullopt;
    }

    // get root page id
    MetaPage Meta;
    Meta = this->ReadPage(0);
    uint32_t rootPageID = Meta.Header()->rootPageID;
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

    // get key from leaf page (if exists)
    LeafPage leaf(currentPage);
    auto cell = leaf.FindKey(key);
    if (cell.has_value()){
        cout << cell->value << "\n";
        return cell;
    }

    cout << "Key not found!\n";
    return std::nullopt;
}
/**
 * @brief Basic Set operation. Sets value to a key. Overwrites older key:value pairs
 *
 * @param key
 * @param value
 * @return true on success
 */
bool Database::Set(const string& key, const string& value){
    // check strings length
    if (key.length() > MAX_KEY_LENGTH || value.length() > MAX_VALUE_LENGTH) {
        cout << "Key or value is too long!\n";
        return false;
    }

    // get root page id
    MetaPage Meta;
    Meta = this->ReadPage(0);
    uint32_t rootPageID = Meta.Header()->rootPageID;
    if (rootPageID == 0) {
        throw std::runtime_error("rootPageID is zero!");
    }

    //read root page
    BasicPage currentPage = this->ReadPage(rootPageID);

    //loop to leaf page
    while (!currentPage.Header()->isLeaf){
        InternalPage internal(currentPage);
        uint32_t pageID = internal.FindPointerByKey(key);
        currentPage = this->ReadPage(pageID);
    }
    // Should it increase key counter in metapage?
    bool increaseKeyCount = false;
    // Try to insert
    LeafPage leaf(currentPage);
    if (leaf.WillFit(key, value)) {
        increaseKeyCount = leaf.InsertKeyValue(key, value); //true if new key was added
        this->WriteBasicPage(leaf);
    }
    // If doesnt fit - optimize and then try
    else {
        leaf = leaf.Optimize();
        if (leaf.WillFit(key, value)) {
            increaseKeyCount = leaf.InsertKeyValue(key, value);
            this->WriteBasicPage(leaf);
        }
        // If still doesnt fit - split
        else {
            this->SplitLeafPage(leaf);
            this->Set(key, value);
            return true;
        }
    }
    // update keyCounter
    if (increaseKeyCount) {
        Meta = this->ReadPage(0);
        Meta.Header()->keyNumber++;
        this->UpdateMetaPage(Meta);
    }
    cout << "SET OK\n";
    return true;
}

/**
 * @brief Splits leaf page into 2 pages (b+tree node)
 *
 * @param LeafToSplit
 */
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
    uint32_t Child1ID = LeafToSplit.Header()->pageID; // Old page ID
    uint32_t Child2ID = Meta.Header()->lastPageID + 1; // Creating new page
    Meta.Header()->lastPageID++;

    // create 2 new children and assign sibling pointers
    LeafPage Child1(Child1ID);
    memcpy(Child1.Special1(), LeafToSplit.Special1(), sizeof(uint32_t));
    memcpy(Child1.Special2(), &Child2ID, sizeof(uint32_t));
    LeafPage Child2(Child2ID);
    memcpy(Child2.Special1(), &Child1ID, sizeof(uint32_t));
    memcpy(Child2.Special2(), LeafToSplit.Special2(), sizeof(uint32_t));

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
        memcpy(Parent.Special1(), &Child2ID, sizeof(Child2ID));

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
        Parent.InsertKeyAndPointer(keyToMoveToParent, Child1ID);
        Parent.UpdatePointerToTheRightFromKey(keyToMoveToParent, Child2ID);

        //write pages
        this->WriteBasicPage(Parent);
        this->WriteBasicPage(Child1);
        this->WriteBasicPage(Child2);
        this->UpdateMetaPage(Meta);
    }
}
/**
 * @brief Splits internal page (b+tree node)
 *
 * @param InternalToSplit
 */
void Database::SplitInternalPage(InternalPage& InternalToSplit){
#ifdef DEBUG
    std::cout << "Splitting internal page: " << InternalToSplit.Header()->pageID << std::endl;
#endif
    // Reread the page to get latest info
    InternalToSplit = this->ReadPage(InternalToSplit.Header()->pageID);

    // check which key to move to parent
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
    // fill first child
    for (uint16_t i = 0; i < mid; i++) {
        auto cell = InternalToSplit.GetKeyAndPointer(InternalToSplit.Offsets()[i]);
        Child1.InsertKeyAndPointer(cell.key, cell.childPointer);
    }
    //copy pointer of middle key (that will be moved to parent) to child1 special
    uint32_t pointerOfKeyToMoveToParent = InternalToSplit.GetKeyAndPointer(InternalToSplit.Offsets()[mid]).childPointer;
    memcpy(Child1.Special1(), &pointerOfKeyToMoveToParent, sizeof(pointerOfKeyToMoveToParent));

    //fill second child and assing special pointer (to the most right child)
    for (uint16_t i = mid + 1; i < total; i++) {
        auto cell = InternalToSplit.GetKeyAndPointer(InternalToSplit.Offsets()[i]);
        Child2.InsertKeyAndPointer(cell.key, cell.childPointer);
    }
    memcpy(Child2.Special1(), InternalToSplit.Special1(), sizeof(pointerOfKeyToMoveToParent));

    //update child2 children parentpointers (child1 id is the same so it not needed to be updated)
    for (uint16_t j = 0; j < Child2.Header()->numberOfCells; j++) {
        auto cell = Child2.GetKeyAndPointer(Child2.Offsets()[j]);
        BasicPage GrandchildPage = this->ReadPage(cell.childPointer);
        GrandchildPage.Header()->parentPageID = Child2ID;
        this->WriteBasicPage(GrandchildPage);
    }
    // update latest grandchild parentpointer
    {
    BasicPage GrandchildPage = this->ReadPage(*Child2.Special1());
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
        memcpy(Parent.Special1(), &Child2ID, sizeof(Child2ID));

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
/**
 * @brief Gets all keys from Database
 *
 * @return
 */
vector<string> Database::GetKeys() const {
    // Read meta page for root page id
    MetaPage Meta;
    Meta = this->ReadPage(0);
    uint32_t rootPageID = Meta.Header()->rootPageID;
    if (rootPageID == 0) {
        throw std::runtime_error("rootPageID is zero!");
    }

    // prepare key vector
    uint32_t keyNum = Meta.Header()->keyNumber;
    vector<string> keys;
    keys.reserve(keyNum);

    // read root page
    BasicPage currentPage = this->ReadPage(rootPageID);

    // loop to first leaf
    while (!currentPage.Header()->isLeaf){
        InternalPage internal(currentPage);
        uint16_t firstOffset = internal.Offsets()[0];
        uint32_t pageID = internal.GetKeyAndPointer(firstOffset).childPointer;
        currentPage = this->ReadPage(pageID);
    }

    //loop first leaf
    LeafPage leaf(currentPage);
    for (uint32_t i = 0; i < leaf.Header()->numberOfCells; i++) {
        keys.push_back(leaf.GetKeyValue(leaf.Offsets()[i]).key);
    }

    // loop other leaves
    while (*leaf.Special2()!=0) {
        leaf = ReadPage(*leaf.Special2());
        for (int i = 0; i < leaf.Header()->numberOfCells; i++) {
            keys.push_back(leaf.GetKeyValue(leaf.Offsets()[i]).key);
        }
    }
    return keys;
}

/**
 * @brief Get keys with given prefix
 *
 * @param prefix
 * @return
 */
vector<string> Database::GetKeys(const string &prefix) const {
    MetaPage Meta;
    Meta = this->ReadPage(0);
    uint32_t rootPageID = Meta.Header()->rootPageID;
    if (rootPageID == 0) {
        throw std::runtime_error("rootPageID is zero!");
    }
    // initialize variables
    vector<string> keys;
    uint32_t prefixLength = prefix.length();

    // read root page
    BasicPage currentPage = this->ReadPage(rootPageID);

    // loop to page where prefix would be
    while (!currentPage.Header()->isLeaf){
        InternalPage internal(currentPage);
        uint32_t pageID = internal.FindPointerByKey(prefix);
        currentPage = this->ReadPage(pageID);
    }
    LeafPage currentLeaf(currentPage);

    // loop to first key with that prefix
    auto index = currentLeaf.FindInsertPosition(prefix);
    // add all keys if they have the prefix
    for(auto i = index; i < currentLeaf.Header()->numberOfCells; i++) {
        string key = currentLeaf.GetKeyValue(currentLeaf.Offsets()[i]).key;
        string substr = key.substr(0, prefixLength);
        if (substr == prefix) {
            keys.push_back(key);
        }
        // stop if key does not have the prefix
        else {
            return keys;
        }
    }
    //loop other leaves
    while (*currentLeaf.Special2()!=0) {
        currentLeaf = ReadPage(*currentLeaf.Special2());
        for (int i = 0; i < currentLeaf.Header()->numberOfCells; i++) {
            string key = currentLeaf.GetKeyValue(currentLeaf.Offsets()[i]).key;
            string substr = key.substr(0, prefixLength);
            if (substr == prefix) {
                keys.push_back(key);
            }
            else {
                return keys;
            }
        }
    }
    return keys;
}
/**
 * @brief Gets n key value pairs from given key
 *
 * @param key
 * @param n
 * @return
 */
vector<leafNodeCell> Database::GetFF(const string &key, uint32_t n) const {

    vector<leafNodeCell> keyValuePairs;
    uint32_t counter = 0;

    //check if key exists in database
    if (key.length() > MAX_KEY_LENGTH || !this->Get(key).has_value()) {
        cout << "No such key in database!\n";
        return keyValuePairs;
    }

    // get root page id
    MetaPage Meta;
    Meta = this->ReadPage(0);
    uint32_t rootPageID = Meta.Header()->rootPageID;
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
    int16_t index = leaf.FindKeyIndex(key);
    for (uint16_t i = index; i < leaf.Header()->numberOfCells; i++) {
        auto cell = leaf.GetKeyValue(leaf.Offsets()[i]);
        keyValuePairs.push_back(cell);
        counter++;
        if (counter == n) {
            return keyValuePairs;
        }
    }

    // traverse other leaves
    while (*leaf.Special2()!=0) {
        leaf = ReadPage(*leaf.Special2());
        for (int i = 0; i < leaf.Header()->numberOfCells; i++) {
            keyValuePairs.push_back(leaf.GetKeyValue(leaf.Offsets()[i]));
            counter++;
            if (counter == n) {
                return keyValuePairs;
            }
        }
    }
    return keyValuePairs;
}

/**
 * @brief Gets n key value pairs from given key going backwards
 *
 * @param key
 * @param n
 * @return
 */
vector<leafNodeCell> Database::GetFB(const string &key, uint32_t n) const {

    vector<leafNodeCell> keyValuePairs;
    uint32_t counter = 0;

    //check if key exists in database
    if (key.length() > MAX_KEY_LENGTH || !this->Get(key).has_value()) {
        cout << "No such key in database!\n";
        return keyValuePairs;
    }

    // get root page id
    MetaPage Meta;
    Meta = this->ReadPage(0);
    uint32_t rootPageID = Meta.Header()->rootPageID;
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
    int16_t index = leaf.FindKeyIndex(key);
    for (int16_t i = index; i >= 0; i--) {
        auto cell = leaf.GetKeyValue(leaf.Offsets()[i]);
        keyValuePairs.push_back(cell);
        counter++;
        if (counter == n) {
            return keyValuePairs;
        }
    }

    // traverse other leaves
    while (*leaf.Special1()!=0) {
        leaf = ReadPage(*leaf.Special1());
        for (int i = leaf.Header()->numberOfCells - 1; i >= 0; i--) {
            keyValuePairs.push_back(leaf.GetKeyValue(leaf.Offsets()[i]));
            counter++;
            if (counter == n) {
                return keyValuePairs;
            }
        }
    }
    return keyValuePairs;
}

/**
 * @brief Getff with pageNum and pageSize
 *
 * @param pageSize size of desired page
 * @param pageNum number of page
 * @return pagingResult struct (see page.h)
 */
pagingResult Database::GetFF(uint32_t pageSize, uint32_t pageNum) const{
    // get root page id
    MetaPage Meta;
    Meta = this->ReadPage(0);
    uint32_t rootPageID = Meta.Header()->rootPageID;
    if (rootPageID == 0) {
        throw std::runtime_error("rootPageID is zero!");
    }
    // variables
    uint32_t totalKeys = Meta.Header()->keyNumber;
    uint32_t totalPages = std::ceil((double)totalKeys/pageSize);

    pagingResult results;

    // validation
    pageNum = std::min(pageNum, totalPages);

    // Calculate slice indexes
    uint32_t startIndex = (pageNum - 1) * pageSize;
    uint32_t endIndex = std::min(startIndex + pageSize, totalKeys);

    // read root page
    BasicPage currentPage = this->ReadPage(rootPageID);

    // loop to first leaf
    while (!currentPage.Header()->isLeaf){
        InternalPage internal(currentPage);
        uint16_t firstOffset = internal.Offsets()[0];
        uint32_t pageID = internal.GetKeyAndPointer(firstOffset).childPointer;
        currentPage = this->ReadPage(pageID);
    }

    // loop first leaf
    uint32_t counter = 0;
    LeafPage currentLeaf(currentPage);
    for (uint32_t i = 0; i < currentLeaf.Header()->numberOfCells; i++) {
        if (counter >= startIndex && counter < endIndex){
            results.keyValuePairs.push_back(currentLeaf.GetKeyValue(currentLeaf.Offsets()[i]));
        }
        counter++;
    }
    // loop all leaves and get data
    while (*currentLeaf.Special2()!=0) {
        currentLeaf = ReadPage(*currentLeaf.Special2());
        for (uint32_t i = 0; i < currentLeaf.Header()->numberOfCells; i++) {
            if (counter >= startIndex && counter < endIndex){
                results.keyValuePairs.push_back(currentLeaf.GetKeyValue(currentLeaf.Offsets()[i]));
            }
            counter++;
        }
        if (counter >= endIndex) {
            break;
        }
    }
    results.currentPage = pageNum;
    results.totalPages = totalPages;
    results.totalItems = totalKeys;
    results.hasNextPage = pageNum < totalPages;
    results.hasPreviousPage = pageNum > 1;
    return results;
}

/**
 * @brief Getfb with pageNum and pageSize
 *
 * @param pageSize size of desired page
 * @param pageNum number of page
 * @return pagingResult struct (see page.h)
 */
pagingResult Database::GetFB(uint32_t pageSize, uint32_t pageNum) const{
    // get root page id
    MetaPage Meta;
    Meta = this->ReadPage(0);
    uint32_t rootPageID = Meta.Header()->rootPageID;
    if (rootPageID == 0) {
        throw std::runtime_error("rootPageID is zero!");
    }
    // variables
    uint32_t totalKeys = Meta.Header()->keyNumber;
    uint32_t totalPages = std::ceil((double)totalKeys/pageSize);
    pagingResult results;

    // validation
    pageNum = std::min(pageNum, totalPages);

    // Calculate slice indexes
    uint32_t startIndex = (pageNum - 1) * pageSize;
    uint32_t endIndex = std::min(startIndex + pageSize, totalKeys);

    // read root page
    BasicPage currentPage = this->ReadPage(rootPageID);

    // loop to last leaf
    while (!currentPage.Header()->isLeaf){
        InternalPage internal(currentPage);
        uint32_t pageID = *internal.Special1();
        currentPage = this->ReadPage(pageID);
    }

    // loop last leaf
    uint32_t counter = 0;
    LeafPage currentLeaf(currentPage);
    for (int32_t i = currentLeaf.Header()->numberOfCells - 1; i >= 0; i--) {
        if (counter >= startIndex && counter < endIndex){
            results.keyValuePairs.push_back(currentLeaf.GetKeyValue(currentLeaf.Offsets()[i]));
        }
        counter++;
    }

    // loop all leaves and get data
    while (*currentLeaf.Special1()!=0) {
        currentLeaf = ReadPage(*currentLeaf.Special1());
        for (int32_t i = currentLeaf.Header()->numberOfCells - 1; i >= 0; i--) {
            if (counter >= startIndex && counter < endIndex){
                results.keyValuePairs.push_back(currentLeaf.GetKeyValue(currentLeaf.Offsets()[i]));
            }
            counter++;
        }
        if (counter >= endIndex) {
            break;
        }
    }
    results.currentPage = pageNum;
    results.totalPages = totalPages;
    results.totalItems = totalKeys;
    results.hasNextPage = pageNum < totalPages;
    results.hasPreviousPage = pageNum > 1;
    return results;
}

/**
 * @brief Removes key from database. Lazy deletion
 *
 * @param key
 * @return true on success
 */
bool Database::Remove(const string& key) {
    // validation
    if (key.length() > MAX_KEY_LENGTH) {
        cout << "Key is too long!\n";
        return false;
    }

    // get root page id
    MetaPage Meta;
    Meta = this->ReadPage(0);
    uint32_t rootPageID = Meta.Header()->rootPageID;
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
    if (!cell.has_value()) {
        return false;
    }

    // remove key if it exists and write pages
    leaf.RemoveKey(key);
    this->WriteBasicPage(leaf);
    Meta.Header()->keyNumber--;
    this->UpdateMetaPage(Meta);

    cout << "Removed key: " << key << "\n";
    return true;
}

/**
 * @brief Optimize database. Needed after many removals
 *
 */
void Database::Optimize(){

    // get old file size
    uintmax_t oldSize = 0;
    try {
        oldSize = std::filesystem::file_size(this->pathToDatabaseFile);
    } catch (const std::filesystem::filesystem_error& e) {
        std::cerr << "Error: " << e.what() << '\n';
    }

    // create new b+tree
    Database OptimizedDb(this->name + "optimized");

    // read meta page
    MetaPage Meta;
    Meta = this->ReadPage(0);
    uint32_t rootPageID = Meta.Header()->rootPageID;
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

    //loop first leaf and add data to new b+tree
    LeafPage leaf(currentPage);
    for (uint32_t i = 0; i < leaf.Header()->numberOfCells; i++) {
        auto cell = leaf.GetKeyValue(leaf.Offsets()[i]);
        OptimizedDb.Set(cell.key, cell.value);
    }

    // loop other leaves
    while (*leaf.Special2()!=0) {
        leaf = ReadPage(*leaf.Special2());
        for (int i = 0; i < leaf.Header()->numberOfCells; i++) {
            auto cell = leaf.GetKeyValue(leaf.Offsets()[i]);
            OptimizedDb.Set(cell.key, cell.value);
        }
    }
    // check newsize
    uintmax_t newSize = 0;
    try {
        newSize = std::filesystem::file_size(OptimizedDb.pathToDatabaseFile);
    } catch (const std::filesystem::filesystem_error& e) {
        std::cerr << "Error: " << e.what() << '\n';
    }

    // rename new database file and delete the old one
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
        return true;
    }

    bool allSuccess = true; // Tik tam, kad patikrinti ar visi įrašai iš WAL sėkmingai įsirašė į B+ medį.

    std::cout << "RecoverFromWal: Applying " << records.size() << " records...\n";

    for (const auto &record : records) {
        if (record.operation == WalOperation::SET) {
            if(!this->Set(record.key, record.value)) {
                std::cerr << "Failed to recover key: " << record.key << "\n";
                allSuccess = false; // Pažymime, jog nepavyko įrašas.
            }
        } else if (record.operation == WalOperation::DELETE) {
            if(!this->Remove(record.key)) {
                std::cerr << "Failed to recover delete: " << record.key << "\n";
                allSuccess = false; // Pažymime, jog nepavyko įrašas.
            }
        }
    }

    // Išvalome visus WAL, jeigu atsistatymo metu visi įrašai sėkmingai buvo įsirašyti į B+ medį.
    if (allSuccess) {
        this->wal.ClearAll();
        return true;
    }

    std::cerr << "CRITICAL: Recovery partially failed. WAL not cleared.\n";
    return false;

}

/**
 * @brief Log'ina operacija, įrašo į B+ medį, grąžina LSN.
 * @param key. Raktas.
 * @param value. Rakto reikšmė.
*/
uint64_t Database::ExecuteLogSetWithLSN(const string &key, const string &value) {
    // 1. Rašome į WAL.
    if (!this->wal.LogSet(key, value)) {
        std::cerr << "Critical Error: Failed to write to WAL during Set.\n";
        return 0;
    }

    // 2. Rašome į B+ medį.
    if (!this->Set(key, value)) {
        std::cerr << "Error: WAL written but B+Tree Set failed.\n";
        return 0;
    }

    // 3. Grąžiname LSN.
    return this->wal.GetCurrentSequenceNumber();
}

/**
 * @brief Log'ina operacija, ištrina iš B+ medžio, grąžina LSN.
 * @param key. Raktas, kuris trinamas.
*/
uint64_t Database::ExecuteLogDeleteWithLSN(const string &key) {
    // 1. Rašome į WAL.
    if (!this->wal.LogDelete(key)) {
        std::cerr << "Critical Error: Failed to write to WAL during Delete.\n";
        return 0;
    }

    // 2. Triname iš B+ medžio.
    this->Remove(key);

    // 3. Gražiname LSN.
    return this->wal.GetCurrentSequenceNumber();
}

/**
 * @brief Atliekama specifinė operacija, su specifiniu LSN.
 * Turėtų naudoti FOLLOWER'is, kad matchintų LEADER'į.
*/
bool Database::ApplyReplication(WalRecord walRecord) {
    // 1. Rašome į WAL su specifiniu LSN (nuo leader'io)
    if (!this->wal.LogWithLSN(walRecord)) {
        std::cerr << "Follower Error: Failed to write replication record to WAL.\n";
        return false;
    }

    // 2. Rašome į B+ medį.
    if (walRecord.operation == WalOperation::SET) {
        return this->Set(walRecord.key, walRecord.value);
    }

    return this->Remove(walRecord.key);
}

/**
 * @brief Cout whole database. For debug
 *
 */
void Database::CoutDatabase() const {
    MetaPage Meta = ReadMetaPage();
    Meta.Header()->CoutHeader();
    uint32_t pagenum = Meta.Header()->lastPageID;
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
