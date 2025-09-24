#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <iostream>

using std::cout;
using std::string;

struct PageHeader { //24 bytes
    uint64_t lastSequenceNumber;
    bool isLeaf;
    uint32_t pageID;
    uint16_t offsetToStartOfFreeSpace;
    uint16_t offsetToEndOfFreeSpace;
    int16_t offsetToStartOfSpecialSpace;

    void CoutHeader() {
        cout << lastSequenceNumber << "\n" << isLeaf << "\n" << pageID << "\n" << offsetToStartOfFreeSpace << "\n"
        << offsetToEndOfFreeSpace << "\n" << offsetToStartOfSpecialSpace << "\n";
    }
};

class Page {
    private:
        static constexpr uint16_t PAGE_SIZE = 4096;
        char mData[PAGE_SIZE];
    public:
        /**
         * @brief Construct a new Page object
         * 
         * @param header 
         */
        Page(PageHeader header) {
            std::memcpy(mData, &header, sizeof(PageHeader));
        }
        /**
         * @brief 
         * 
         * @return PageHeader* 
         */
        PageHeader* Header() { return reinterpret_cast<PageHeader*>(mData); }
        char* getData(){
            return mData;
        }

        bool readPage (uint32_t pageID){

        }

        bool readPageTest (char arr[4096]){
            PageHeader header;
            std::memcpy(&header, arr, sizeof(PageHeader));
            header.CoutHeader();
            return true;
        }
            
        bool WritePage(string fileName){

        }

        void CoutPage(){
            for (int i = 0; i <PAGE_SIZE; i++) {
                cout << static_cast<int>(mData[i]);
                if (i==23){
                    cout << "\nHEADER END\n";
                    break;
                } 
            }
        }

};
