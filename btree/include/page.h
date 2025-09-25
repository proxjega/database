#pragma once

#include <cstdint>
#include <cstring>
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

class Page {
    friend class Database;
    public:
        static constexpr uint16_t PAGE_SIZE = 4096;
    protected:
        char mData[PAGE_SIZE];
    public:
        Page();    
        Page(const Page &page);
        void* Header();
        char* getData();
        void setData();
};

class BasicPage : public Page{
    friend class Database;
    public:
        using Page::Page;

        BasicPage(Page page);
        BasicPage(uint32_t ID, bool leaf);
        BasicPage(PageHeader header);

        PageHeader* Header();
        vector<uint16_t>* Payload();

        void CoutPage();
};

class MetaPage : public Page {
    public:
        using Page::Page;
        MetaPage(MetaPageHeader header);
        MetaPage(Page page);
        MetaPageHeader* Header();
};
