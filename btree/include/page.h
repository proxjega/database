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

struct MetaPageHeader {
    uint32_t rootPageID;
    uint32_t lastPageID;
    uint64_t lastSequenceNumber;
    void CoutHeader();
};

struct internalNodeCell {
    string key;
    uint32_t childPointer;
    internalNodeCell(string key, uint32_t pointer) {
        this->key = key;
        this->childPointer = pointer;
    }
};

struct leafNodeCell {
    string key;
    string value;
    leafNodeCell(string key, string value) {
        this->key = key;
        this->value = value;
    }
};



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

class InternalPage : public BasicPage {
    public:
        using BasicPage::BasicPage;

        InternalPage(uint32_t ID);

        uint32_t FindPointerByKey(const string &key);
        uint16_t FindInsertPosition(const string& key);
        void InsertKeyAndPointer(string key, uint32_t pointer);
        internalNodeCell GetKeyAndPointer(uint16_t offset);
};

class LeafPage : public BasicPage {
    public:
        using BasicPage::BasicPage;

        LeafPage(uint32_t ID);

        uint16_t FindInsertPosition(const string& key);
        void InsertKeyValue(string key, string value);
        leafNodeCell GetKeyValue(uint16_t offset);
        std::optional<leafNodeCell> FindKey(const string &key);
};

class MetaPage : public Page {
    public:
        using Page::Page;
        MetaPage(MetaPageHeader header);
        MetaPage(Page page);
        MetaPageHeader* Header();
};
