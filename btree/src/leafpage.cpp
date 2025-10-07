#pragma once
#include <algorithm>
#include <cstdint>

#include "../include/leafpage.h"

/**
 * @brief Basic constructor. Constructs new Empty LeafPage with given ID
 * 
 * @param ID 
 */
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

/**
 * @brief Insert key:value pair into LeafPage
 * 
 * @param key key to insert
 * @param value value to insert
 * @details Deserializes key and value strings. Copies them into end of the page. Inserts an offset to them into offset array (in sorted manner, binary search)
 */
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

    Header()->offsetToStartOfFreeSpace += sizeof(offset);
    Header()->offsetToEndOfFreeSpace -= cellLength;

    auto *pCurrentPosition = mData + offset;

    memcpy(pCurrentPosition, &keyLength, sizeof(keyLength));
    pCurrentPosition += sizeof(keyLength);

    memcpy(pCurrentPosition, key.data(), keyLength);
    pCurrentPosition += keyLength;

    memcpy(pCurrentPosition, &valueLength, sizeof(valueLength));
    pCurrentPosition += sizeof(valueLength);

    memcpy(pCurrentPosition, value.data(), valueLength);

    Header()->numberOfCells++;
}

/**
 * @brief Gets key value pair by offset.
 * 
 * @param offset offset to keyvalue pair
 * @return leafNodeCell 
 */
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

/**
 * @brief Searches for the key in the page and returns key and value if found. Else returns nothing
 * 
 * @param key 
 * @return std::optional<leafNodeCell> 
 */
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

/**
 * @brief Searches for the position to insert offset into offset array in O(log(n))
 * 
 * @param key 
 * @return uint16_t 
 */
uint16_t LeafPage::FindInsertPosition(const std::string& key) {
    auto begin = Offsets();
    auto end = Offsets() + Header()->numberOfCells;

    auto it = std::lower_bound(begin, end, key, [&](uint16_t offset, const std::string& k) {
        return GetKeyValue(offset).key < k;
    });

    return static_cast<uint16_t>(it - begin);
}

/**
 * @brief Find index in offsets array of the given key. Based on binary search.
 * 
 * @param key 
 * @return int16_t 
 */
int16_t LeafPage::FindKeyIndex(const string& key) {
    int low = 0;
    int high = Header()->numberOfCells - 1;
    while (low <= high) {
        int mid = low + (high - low) / 2;

        if (GetKeyValue(Offsets()[mid]).key == key)
            return mid;

        if (GetKeyValue(Offsets()[mid]).key < key)
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
void LeafPage::RemoveKey(const string &key){
    int16_t index = FindKeyIndex(key);
    if (index == -1) return;
    for (int i = index; i < Header()->numberOfCells - 1; i++) {
        Offsets()[i] = Offsets()[i + 1];
    }
    Header()->numberOfCells--;

}


void LeafPage::CoutPage() {
    cout << "---STARTCOUTPAGE---\n";
    this->Header()->CoutHeader();
    for (int i = 0; i < this->Header()->numberOfCells; i++) {
        cout << "offset: " << this->Offsets()[i] << ", key: "; 
        leafNodeCell cell = this->GetKeyValue(this->Offsets()[i]);
        cout << cell.key << ": " << cell.value << "\n";
    }
    cout << "Special: " << *this->Special() << "\n";
    cout << "---ENDCOUTPAGE---\n";
}
