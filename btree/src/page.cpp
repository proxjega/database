#include "../include/page.h"
#include "../include/database.h" 
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <algorithm>

using std::memcpy;

// ---------------- PageHeader ----------------
void PageHeader::CoutHeader() {
    cout << "lastSequenceNumber: " << lastSequenceNumber << "\n"
         << "isLeaf: " << isLeaf << "\n"
         << "pageID: " << pageID << "\n"
         << "numberOfCells: " << numberOfCells << "\n"
         << "offsetToStartOfFreeSpace: " << offsetToStartOfFreeSpace << "\n"
         << "offsetToEndOfFreeSpace: " << offsetToEndOfFreeSpace << "\n"
         << "offsetToStartOfSpecialSpace: " << offsetToStartOfSpecialSpace << "\n\n";
}

void MetaPageHeader::CoutHeader(){
    cout << "lastSequenceNumber: " << lastSequenceNumber << "\n"
         << "rootPageID: " << rootPageID << "\n"
         << "lastPageID: " << lastPageID << "\n\n";
}

// ---------------- Page ----------------
/**
 * @brief Default constructor (sets everything to zero)
 * 
 */
Page::Page(){
    std::memset(mData, 0, PAGE_SIZE);
}
/**
 * @brief Copy constructor
 * 
 * @param page 
 */
Page::Page(const Page &page) {
    std::memcpy(this->mData, page.mData, PAGE_SIZE);
}

char* Page::getData() {
    return mData;
}

// ---------------- BasicPage ----------------

/**
 * @brief Constructor when we know data of the page (header)
 * 
 * @param header 
 */
BasicPage::BasicPage(PageHeader header) {
    std::memcpy(mData, &header, sizeof(PageHeader));
}

BasicPage::BasicPage(Page page) {
    std::memcpy(this->mData, page.getData(), PAGE_SIZE);
}


PageHeader* BasicPage::Header() {
    return reinterpret_cast<PageHeader*>(mData);
}

uint16_t* BasicPage::Offsets() {
    return reinterpret_cast<uint16_t*>(mData+sizeof(PageHeader));
}

uint32_t* BasicPage::Special(){
    return reinterpret_cast<uint32_t*>(mData+Header()->offsetToStartOfSpecialSpace);
}

int16_t BasicPage::FreeSpace() {
    return this->Header()->offsetToEndOfFreeSpace - this->Header()->offsetToStartOfFreeSpace;
}




void BasicPage::CoutPage() {
    for (int i = 0; i < PAGE_SIZE; i++) {
        cout << static_cast<int>(mData[i]);
        if (i == 23) {
            cout << "\nHEADER END\n";
        }
    }
}

// ---------------- InternalPage ----------------


InternalPage::InternalPage(uint32_t ID) {
    PageHeader pageHeader;
    pageHeader.pageID = ID;
    pageHeader.isLeaf = false;
    pageHeader.numberOfCells = 0;
    pageHeader.lastSequenceNumber = 1; // get from logger class
    pageHeader.offsetToEndOfFreeSpace = this->PAGE_SIZE-sizeof(uint32_t);
    pageHeader.offsetToStartOfFreeSpace = sizeof(PageHeader);
    pageHeader.offsetToStartOfSpecialSpace = PAGE_SIZE-sizeof(uint32_t); // last child pointer
    std::memcpy(mData, &pageHeader, sizeof(PageHeader));
}

void InternalPage::InsertKeyAndPointer(string key, uint32_t pointer){
    uint16_t keyLength = key.length();
    uint16_t cellLength = keyLength + sizeof(keyLength) + sizeof(pointer);
    uint16_t offset = Header()->offsetToEndOfFreeSpace - cellLength;

    if (this->FreeSpace() < cellLength + sizeof(offset) ) {
        cout << "Not enough space\n";
        return;
    }

    //insert in sorted manner
    uint16_t positionToInsert = FindInsertPosition(key);

    for (int i = Header()->numberOfCells; i > positionToInsert; i--) {
        Offsets()[i] = Offsets()[i-1];
    }
    Offsets()[positionToInsert] = offset;

    Header()->offsetToEndOfFreeSpace -= cellLength;
    auto *pCurrentPosition = mData + Header()->offsetToEndOfFreeSpace;

    memcpy(pCurrentPosition, &keyLength, sizeof(keyLength));
    pCurrentPosition += sizeof(keyLength);

    memcpy(pCurrentPosition, key.data(), keyLength);
    pCurrentPosition += keyLength;

    memcpy(pCurrentPosition, &pointer, sizeof(pointer));


    Header()->numberOfCells++;
}

internalNodeCell InternalPage::GetKeyAndPointer(uint16_t offset){
    uint16_t keyLength;
    uint32_t pointer;
    char* pCurrentPosition = mData + offset;
   
    std::memcpy(&keyLength, pCurrentPosition, sizeof(keyLength));
    pCurrentPosition += sizeof(keyLength);
    
    string key(pCurrentPosition, keyLength);
    pCurrentPosition += keyLength;

    std::memcpy(&pointer, pCurrentPosition, sizeof(pointer));

    return internalNodeCell(key, pointer);
}

uint16_t InternalPage::FindInsertPosition(const std::string& key) {
    auto begin = Offsets();
    auto end = Offsets() + Header()->numberOfCells;

    auto it = std::lower_bound(begin, end, key, [&](uint16_t offset, const std::string& k) {
        return GetKeyAndPointer(offset).key < k;
    });

    return static_cast<uint16_t>(it - begin);
}

//fixed
uint32_t InternalPage::FindPointerByKey(const string &key){
    auto begin = Offsets();
    auto end = Offsets() + Header()->numberOfCells;

    auto it = std::lower_bound(begin, end, key, [&](uint16_t offset, const std::string& k) {
        return GetKeyAndPointer(offset).key < k;
    });
    if (it == end) return *Special(); //return special pointer if it the key is bigger than everyone else
    return GetKeyAndPointer(*it).childPointer;
}

// ---------------- LeafPage ----------------

LeafPage::LeafPage(uint32_t ID) {
    PageHeader pageHeader;
    pageHeader.pageID = ID;
    pageHeader.isLeaf = true;
    pageHeader.numberOfCells = 0;
    pageHeader.lastSequenceNumber = 1;
    pageHeader.offsetToEndOfFreeSpace = this->PAGE_SIZE-(sizeof(uint32_t));
    pageHeader.offsetToStartOfFreeSpace = sizeof(PageHeader);
    pageHeader.offsetToStartOfSpecialSpace = this->PAGE_SIZE-(sizeof(uint32_t)); // sibling pointer
    std::memcpy(mData, &pageHeader, sizeof(PageHeader));
}


void LeafPage::InsertKeyValue(string key, string value) {

    // if ( GetKey(key) == true) return; //check if key exists
    uint16_t keyLength = key.length();
    uint16_t valueLength = value.length();
    uint16_t cellLength = keyLength + valueLength + sizeof(keyLength) + sizeof(valueLength);
    uint16_t offset = Header()->offsetToEndOfFreeSpace - cellLength;

    if (this->FreeSpace() < cellLength + sizeof(offset) ) {
        cout << "Not enough space\n";
        return;
    }

    //insert in sorted manner
    uint16_t positionToInsert = FindInsertPosition(key);
    
    for (int i = Header()->numberOfCells; i > positionToInsert; i--) {
        Offsets()[i] = Offsets()[i-1];
    }
    Offsets()[positionToInsert] = offset;

    Header()->offsetToEndOfFreeSpace -= cellLength;
    auto *pCurrentPosition = mData + Header()->offsetToEndOfFreeSpace;

    memcpy(pCurrentPosition, &keyLength, sizeof(keyLength));
    pCurrentPosition += sizeof(keyLength);

    memcpy(pCurrentPosition, key.data(), keyLength);
    pCurrentPosition += keyLength;

    memcpy(pCurrentPosition, &valueLength, sizeof(valueLength));
    pCurrentPosition += sizeof(valueLength);

    memcpy(pCurrentPosition, value.data(), valueLength);

    Header()->numberOfCells++;
}

leafNodeCell LeafPage::GetKeyValue(uint16_t offset) {
    uint16_t keyLength, valueLength;
    char* pCurrentPosition = mData + offset;
   
    std::memcpy(&keyLength, pCurrentPosition, sizeof(keyLength));
    pCurrentPosition += sizeof(keyLength);
    
    string key(pCurrentPosition, keyLength);
    pCurrentPosition += keyLength;

    std::memcpy(&valueLength, pCurrentPosition, sizeof(valueLength));
    pCurrentPosition += sizeof(valueLength);

    string value(pCurrentPosition, valueLength);
    return leafNodeCell(key, value);
}

std::optional<leafNodeCell> LeafPage::FindKey(const string &key){
    int low = 0;
    int high = Header()->numberOfCells - 1;
    while (low <= high) {
        int mid = low + (high - low) / 2;

        if (GetKeyValue(Offsets()[mid]).key == key)
            return GetKeyValue(Offsets()[mid]);

        if (GetKeyValue(Offsets()[mid]).key < key)
            low = mid + 1;

        else
            high = mid - 1;
    }
    return std::nullopt;
}

uint16_t LeafPage::FindInsertPosition(const std::string& key) {
    auto begin = Offsets();
    auto end = Offsets() + Header()->numberOfCells;

    auto it = std::lower_bound(begin, end, key, [&](uint16_t offset, const std::string& k) {
        return GetKeyValue(offset).key < k;
    });

    return static_cast<uint16_t>(it - begin);
}

// ---------------- MetaPage ----------------

MetaPage::MetaPage(MetaPageHeader header) {
    std::memcpy(mData, &header, sizeof(MetaPageHeader));
}

MetaPage::MetaPage(Page page) {
    std::memcpy(mData, page.getData(), PAGE_SIZE);
}

MetaPageHeader* MetaPage::Header() {
    return reinterpret_cast<MetaPageHeader*>(mData);
}

