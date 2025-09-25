#include "../include/page.h"
#include "../include/database.h" 
#include <cstdint>
#include <cstring>

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
    pageHeader.offsetToStartOfSpecialSpace = PAGE_SIZE-sizeof(uint32_t);
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
    uint16_t positionToInsert = 0;
    for (uint16_t i = 0; i < Header()->numberOfCells; i++) {
        if (key < GetKeyAndPointer(Offsets()[i]).key) {
            positionToInsert = i;
            break;
        }
    }
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

vector<internalNodeCell>* InternalPage::Data() {
    return reinterpret_cast<vector<internalNodeCell>*>(mData + Header()->offsetToEndOfFreeSpace);
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
    pageHeader.offsetToStartOfSpecialSpace = this->PAGE_SIZE-(sizeof(uint32_t));
    std::memcpy(mData, &pageHeader, sizeof(PageHeader));
}

vector<leafNodeCell>* LeafPage::Data() {
    return reinterpret_cast<vector<leafNodeCell>*>(mData + Header()->offsetToEndOfFreeSpace);
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
    uint16_t positionToInsert = 0;
    for (uint16_t i = 0; i < Header()->numberOfCells; i++) {
        if (key < GetKeyValue(Offsets()[i]).key) {
            positionToInsert = i;
            break;
        }
    }
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


// ---------------- MetaPage ----------------

MetaPage::MetaPage(MetaPageHeader header) {
    std::memcpy(mData, &header, sizeof(MetaPageHeader));
}

MetaPage::MetaPage(Page page) {
    std::memcpy(this->mData, page.getData(), PAGE_SIZE);
}

MetaPageHeader* MetaPage::Header() {
    return reinterpret_cast<MetaPageHeader*>(mData);
}

