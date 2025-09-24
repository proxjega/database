#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <iostream>
#include <fstream>

class Database;

using std::cout;
using std::string;

struct PageHeader { // 24 bytes
    uint64_t lastSequenceNumber;
    bool isLeaf;
    uint32_t pageID;
    uint16_t offsetToStartOfFreeSpace;
    uint16_t offsetToEndOfFreeSpace;
    int16_t offsetToStartOfSpecialSpace;

    void CoutHeader();
};

class Page {
    friend class Database;
    public:
        static constexpr uint16_t PAGE_SIZE = 4096;

    private:
        char mData[PAGE_SIZE];

    public:

        Page();    
        Page(PageHeader header);

        PageHeader* Header();
        char* getData();
        void setData();

        bool readPageTest(char arr[4096]);
        void CoutPage();
};
