#include "../include/page.h"
#include "../include/database.h"
#include <cstdint>
#include <cstring>

using std::memcpy;

// This file has implementations for:
// All structs
// Page class
// BasicPage class
// MetaPage class

// ---------------- PageHeaders ----------------

void PageHeader::CoutHeader()  {
    cout << "isLeaf: " << isLeaf << "\n"
         << "pageID: " << pageID << "\n"
         << "parentPageID: " << parentPageID << "\n"
         << "numberOfCells: " << numberOfCells << "\n"
         << "offsetToStartOfFreeSpace: " << offsetToStartOfFreeSpace << "\n"
         << "offsetToEndOfFreeSpace: " << offsetToEndOfFreeSpace << "\n"
         << "offsetToStartOfSpecialSpace: " << offsetToStartOfSpecialSpace << "\n\n";
}

void MetaPageHeader::CoutHeader(){
    cout << "rootPageID: " << rootPageID << "\n"
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
/**
 * @brief Return pointer to page's data
 *
 * @return char*
 */
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

/**
 * @brief Copy constructor from Page object
 *
 * @param page
 */
BasicPage::BasicPage(Page page) {
    std::memcpy(this->mData, page.getData(), PAGE_SIZE);
}

/**
 * @brief Pointer to page's header
 *
 * @return PageHeader*
 */
PageHeader* BasicPage::Header() {
    return reinterpret_cast<PageHeader*>(mData);
}


/**
 * @brief Pointer to the start of offsets array. Used with Header()->numberOfCells
 *
 * @return uint16_t*
 */
uint16_t* BasicPage::Offsets() {
    return reinterpret_cast<uint16_t*>(mData+sizeof(PageHeader));
}

/**
 * @brief Pointer to the special place in the BasicPage. (for LeafPage - pointer to sibling Page, for InternalPage - pointer to last child page)
 *
 * @return uint32_t*
 */
uint32_t* BasicPage::Special(){
    return reinterpret_cast<uint32_t*>(mData + Header()->offsetToStartOfSpecialSpace);
}

/**
 * @brief Calculates how many free bytes does page have.
 *
 * @return int16_t
 */
int16_t BasicPage::FreeSpace() {
    return this->Header()->offsetToEndOfFreeSpace - this->Header()->offsetToStartOfFreeSpace;
}



// ---------------- MetaPage ----------------

/**
 * @brief Construct a new MetaPage object when we know info
 *
 * @param header
 */
MetaPage::MetaPage(MetaPageHeader header) {
    std::memcpy(mData, &header, sizeof(MetaPageHeader));
}

/**
 * @brief Copy MetaPage from base class Page
 *
 * @param page
 */
MetaPage::MetaPage(Page page) {
    std::memcpy(mData, page.getData(), PAGE_SIZE);
}

/**
 * @brief Get pointer to MetaPage Header
 *
 * @return MetaPageHeader*
 */
MetaPageHeader* MetaPage::Header() {
    return reinterpret_cast<MetaPageHeader*>(mData);
}

MetaPage& MetaPage::operator=(Page& page) {
    if (this!= &page) {
        std::memcpy(mData, page.getData(), PAGE_SIZE);
    }
    return *this;
}
