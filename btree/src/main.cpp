#include <cstdint>
#include <iostream>
#include "../include/page.h"
#include "../include/database.h"

using namespace std;

int main(){
    PageHeader header;
    header.pageID=0;
    header.isLeaf = false;
    header.lastSequenceNumber = 1;
    header.offsetToStartOfFreeSpace = sizeof(PageHeader);
    header.offsetToEndOfFreeSpace = 4096;
    header.offsetToStartOfSpecialSpace = -1;
    Database Duombaze("duombaze1");
    Page Page1(header);
    Page1.Header()->CoutHeader();
    Duombaze.WritePage(Page1);
    Page Page2 = Duombaze.ReadPage(0);
    Page2.Header()->CoutHeader();

}