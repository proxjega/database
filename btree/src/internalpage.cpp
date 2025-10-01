#include "../include/internalpage.h"
#include <algorithm>

// ---------------- InternalPage ----------------

/**
 * @brief Construct a new InternalPage object with given ID
 * 
 * @param ID 
 */
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

/**
 * @brief Insert a key pointer pair into internal Page. 
    Inserts pair to the end and inserts offset to them into offset array (in sorted manner)
 * 
 * @param key 
 * @param pointer 
 */
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

    Header()->offsetToStartOfFreeSpace += sizeof(offset);
    Header()->offsetToEndOfFreeSpace -= cellLength;

    auto *pCurrentPosition = mData + offset;

    memcpy(pCurrentPosition, &keyLength, sizeof(keyLength));
    pCurrentPosition += sizeof(keyLength);

    memcpy(pCurrentPosition, key.data(), keyLength);
    pCurrentPosition += keyLength;

    memcpy(pCurrentPosition, &pointer, sizeof(pointer));


    Header()->numberOfCells++;
}

/**
 * @brief Get key pointer pair by offset
 * 
 * @param offset 
 * @return internalNodeCell 
 */
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

/**
 * @brief Searches for the position in offset array to insert a new offset to key
 * 
 * @param key
 * @return uint16_t offset (in bytes)
 */
uint16_t InternalPage::FindInsertPosition(const std::string& key) {
    auto begin = Offsets();
    auto end = Offsets() + Header()->numberOfCells;

    auto it = std::lower_bound(begin, end, key, [&](uint16_t offset, const std::string& k) {
        return GetKeyAndPointer(offset).key < k;
    });

    return static_cast<uint16_t>(it - begin);
}

/**
 * @brief Returns the pointer to the child with given key. If the key is present in this node, gives pointer to smaller (left)child
 * 
 * @param key key that needed to be found
 * @return uint32_t PageID with that key 
 */
uint32_t InternalPage::FindPointerByKey(const string &key){
    auto begin = Offsets();
    auto end = Offsets() + Header()->numberOfCells;

    auto it = std::lower_bound(begin, end, key, [&](uint16_t offset, const std::string& k) {
        return GetKeyAndPointer(offset).key < k;
    });
    if (it == end) return *Special(); //return special pointer if it the key is bigger than everyone else
    return GetKeyAndPointer(*it).childPointer;
}