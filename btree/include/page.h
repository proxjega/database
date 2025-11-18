#pragma once

#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <iostream>
#include <fstream>
#include <utility>
#include <vector>

class Database;

using std::cout;
using std::string;
using std::vector;

/**
 * @brief Struct for header of basic page.
 *
 */
struct PageHeader {
    bool isLeaf;
    uint32_t pageID;
    uint32_t parentPageID;
    uint16_t numberOfCells;
    uint16_t offsetToStartOfFreeSpace;
    uint16_t offsetToEndOfFreeSpace;
    uint16_t offsetToStartOfSpecialSpace;
    void CoutHeader();
};

/**
 * @brief Struct for header of meta page
 *
 */
struct MetaPageHeader {
    uint32_t rootPageID;
    uint32_t lastPageID;
    uint64_t keyNumber;
    void CoutHeader();
};

/**
 * @brief Struct of internal page's node (key:childpointer pair)
 *
 */
struct internalNodeCell {
    string key;
    uint32_t childPointer;
    internalNodeCell(string key, uint32_t pointer) : key(std::move(key)), childPointer(pointer) {}
};

/**
 * @brief struct for leaf page's node (key:value pair)
 *
 */
struct leafNodeCell {
    string key;
    string value;
    leafNodeCell(string key, string value) : key(std::move(key)), value(std::move(value)) {}
};

struct pagingResult {
    vector<leafNodeCell> keyValuePairs;
    uint32_t currentPage;
    uint32_t totalPages;
    uint32_t totalItems;
    bool hasNextPage;
    bool hasPreviousPage;
};


/**
 * @brief Base Page class. Has data array and few basic set get methods
 *
 */
class Page {
    friend class Database;
    public:
        static constexpr uint16_t PAGE_SIZE = 16384;
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
        uint32_t* Special1();
        uint32_t* Special2();

        //helpers
        int16_t FreeSpace();
};

/**
 * @brief MetaPage class for first page in database. Metapage stores some info about database.
 *
 */
class MetaPage : public Page {
       friend class Database;
public:
        // Constructors
        using Page::Page;
        MetaPage(MetaPageHeader header);
        MetaPage(Page page);
        MetaPage& operator=(Page& page);

        // Pointer
        MetaPageHeader* Header();
};
