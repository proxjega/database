#pragma once

#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <iostream>
#include <fstream>
#include <vector>

class Database;

using std::cout;
using std::string;
using std::vector;

/**
 * @brief Struct for header of basic page.
 * 
 */
struct PageHeader { // 24 bytes
    uint64_t lastSequenceNumber;
    bool isLeaf;
    uint32_t pageID;
    uint16_t numberOfCells;
    uint16_t offsetToStartOfFreeSpace;
    uint16_t offsetToEndOfFreeSpace;
    int16_t offsetToStartOfSpecialSpace;
    void CoutHeader();
};

/**
 * @brief Struct for header of meta page
 * 
 */
struct MetaPageHeader {
    uint32_t rootPageID;
    uint32_t lastPageID;
    uint64_t lastSequenceNumber;
    void CoutHeader();
};

/**
 * @brief Struct of internal page's node (key:childpointer pair)
 * 
 */
struct internalNodeCell {
    string key;
    uint32_t childPointer;
    internalNodeCell(string key, uint32_t pointer) {
        this->key = key;
        this->childPointer = pointer;
    }
};

/**
 * @brief struct for leaf page's node (key:value pair)
 * 
 */
struct leafNodeCell {
    string key;
    string value;
    leafNodeCell(string key, string value) {
        this->key = key;
        this->value = value;
    }
};

/**
 * @brief Base Page class. Has data array and few basic set get methods
 * 
 */
class Page {
    friend class Database;
    public:
        static constexpr uint16_t PAGE_SIZE = 4096;
    protected:
        char mData[PAGE_SIZE];
    public:
        Page();    
        Page(const Page &page);
        char* getData();
        void setData();
};

/**
 * @brief BasicPage class for all the pages in database excluding first one (metapage). Is base class for InternalPage and LeafPage.
 * 
 */
class BasicPage : public Page{
    friend class Database;
    public:
        using Page::Page;

        //constructors
        BasicPage(Page page);
        BasicPage(PageHeader header);

        // pointers to data
        PageHeader* Header();
        uint16_t* Offsets();
        uint32_t* Special();

        //helpers
        int16_t FreeSpace();

        //operations

        void CoutPage();
};

/**
 * @brief MetaPage class for first page in database. Metapage stores some info about database.
 * 
 */
class MetaPage : public Page {
    public:
        using Page::Page;
        MetaPage(MetaPageHeader header);
        MetaPage(Page page);
        MetaPageHeader* Header();
};
