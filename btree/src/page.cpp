#include "../include/page.h"
#include "../include/database.h" 
#include <cstdint>
#include <cstring>
#include <fstream>
#include <vector>

// ---------------- PageHeader ----------------
void PageHeader::CoutHeader() {
    cout << "lastSequenceNumber: " << lastSequenceNumber << "\n"
         << "isLeaf: " << isLeaf << "\n"
         << "pageID: " << pageID << "\n"
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

BasicPage::BasicPage(uint32_t ID, bool leaf) {
    PageHeader pageHeader;
    if (leaf==false) {
        pageHeader.pageID = ID;
        pageHeader.isLeaf = false;
        pageHeader.lastSequenceNumber = 1; // get from logger class
        pageHeader.offsetToEndOfFreeSpace = this->PAGE_SIZE;
        pageHeader.offsetToStartOfFreeSpace = sizeof(PageHeader);
        pageHeader.offsetToStartOfSpecialSpace = -1;
    }
    if (leaf == true) {
        pageHeader.pageID = ID;
        pageHeader.isLeaf = true;
        pageHeader.lastSequenceNumber = 1;
        pageHeader.offsetToEndOfFreeSpace = this->PAGE_SIZE-(2*sizeof(uint32_t));
        pageHeader.offsetToStartOfFreeSpace = sizeof(PageHeader);
        pageHeader.offsetToStartOfSpecialSpace = this->PAGE_SIZE-(2*sizeof(uint32_t));
    }
    std::memcpy(mData, &pageHeader, sizeof(PageHeader));
}


PageHeader* BasicPage::Header() {
    return reinterpret_cast<PageHeader*>(mData);
}

vector<uint16_t>* BasicPage::Payload() {
    return reinterpret_cast<vector<uint16_t>*>(mData+sizeof(PageHeader));
}


void BasicPage::CoutPage() {
    for (int i = 0; i < PAGE_SIZE; i++) {
        cout << static_cast<int>(mData[i]);
        if (i == 23) {
            cout << "\nHEADER END\n";
        }
    }
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

