#include "../include/page.h"
#include "../include/database.h" 
#include <fstream>

// ---------------- PageHeader ----------------
void PageHeader::CoutHeader() {
    cout << "lastSequenceNumber: " << lastSequenceNumber << "\n"
         << "isLeaf: " << isLeaf << "\n"
         << "pageID: " << pageID << "\n"
         << "offsetToStartOfFreeSpace: " << offsetToStartOfFreeSpace << "\n"
         << "offsetToEndOfFreeSpace: " << offsetToEndOfFreeSpace << "\n"
         << "offsetToStartOfSpecialSpace: " << offsetToStartOfSpecialSpace << "\n\n";
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
 * @brief Constructor when we know data of the page (header)
 * 
 * @param header 
 */
Page::Page(PageHeader header) {
    std::memcpy(mData, &header, sizeof(PageHeader));
}

PageHeader* Page::Header() {
    return reinterpret_cast<PageHeader*>(mData);
}

char* Page::getData() {
    return mData;
}



bool Page::readPageTest(char arr[4096]) {
    PageHeader header;
    std::memcpy(&header, arr, sizeof(PageHeader));
    header.CoutHeader();
    return true;
}



void Page::CoutPage() {
    for (int i = 0; i < PAGE_SIZE; i++) {
        cout << static_cast<int>(mData[i]);
        if (i == 23) {
            cout << "\nHEADER END\n";
        }
    }
}
