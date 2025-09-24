#include <cstdint>
#include <iostream>
#include "../include/page.h"
#include "../include/database.h"

using namespace std;

int main(){
    PageHeader header;
    header.pageID=1;
    header.isLeaf = false;
    header.lastSequenceNumber = 1;
    header.offsetToStartOfFreeSpace = sizeof(PageHeader);
    header.offsetToEndOfFreeSpace = 4096;
    header.offsetToStartOfSpecialSpace = -1;
    Page page1(header);
    page1.Header()->CoutHeader();
    cout << "\n";
    page1.CoutPage();
    page1.Header()->pageID = 2;
    cout << "\n\n";
    page1.Header()->CoutHeader();
    cout << "\n";
    page1.CoutPage();
}