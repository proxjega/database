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

/**
 * @brief Find index in offsets array of the given key. Based on binary search.
 * 
 * @param key 
 * @return int16_t 
 */
int16_t InternalPage::FindKeyIndex(const string& key) {
    int low = 0;
    int high = Header()->numberOfCells - 1;
    while (low <= high) {
        int mid = low + (high - low) / 2;

        if (GetKeyAndPointer(Offsets()[mid]).key == key)
            return mid;

        if (GetKeyAndPointer(Offsets()[mid]).key < key)
            low = mid + 1;

        else
            high = mid - 1;
    }
    return -1;
}

/**
 * @brief Lazy deletion of key from the page. Doesn actually removes the key value pair, only offset to them.
 * 
 * @param key 
 */
void InternalPage::RemoveKey(const string &key){
    int16_t index = FindKeyIndex(key);
    if (index == -1) return;
    for (int i = index; i < Header()->numberOfCells - 1; i++) {
        Offsets()[i] = Offsets()[i + 1];
    }
    Header()->numberOfCells--;

}


void InternalPage::CoutPage() {
    cout << "---STARTCOUTPAGE---\n";
    this->Header()->CoutHeader();
    for (int i = 0; i < this->Header()->numberOfCells; i++) {
        cout << "offset: " << this->Offsets()[i] << ", key: "; 
        internalNodeCell cell = this->GetKeyAndPointer(this->Offsets()[i]);
        cout << cell.key << ": " << cell.childPointer << "\n";
    }
    cout << "Special: " << *this->Special() << "\n";
    cout << "---ENDCOUTPAGE---\n";
}