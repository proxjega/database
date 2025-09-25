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
    //test1:
    Database Duombaze("duombaze1");
    BasicPage Page1(header);
    Page1.Header()->CoutHeader();
    Duombaze.WriteBasicPage(Page1);
    BasicPage Page2 = Duombaze.ReadPage(0);
    Page2.Header()->CoutHeader();

    //test2:
    Database Duombaze2("duombaze2");
    MetaPage metapage = Duombaze2.ReadPage(0);
    BasicPage rootpage = Duombaze2.ReadPage(1);
    metapage.Header()->CoutHeader();
    rootpage.Header()->CoutHeader();

}