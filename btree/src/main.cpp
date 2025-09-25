#include <cstdint>
#include <cstring>
#include <iostream>
#include "../include/page.h"
#include "../include/database.h"

using namespace std;

void TEST(){
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
    rootpage.Header()->isLeaf=true;
    Duombaze2.WriteBasicPage(rootpage);
    rootpage = Duombaze2.ReadPage(1);
    rootpage.Header()->CoutHeader();

    //test3:
    BasicPage page(2, false);
    cout << page.Payload()->size() << "\n";
}

int main(){
    
    //test4:
    Database Database1("db");
    LeafPage page1 = Database1.ReadPage(1);
    page1.InsertKey("2");
    page1.InsertKey("3");
    page1.InsertKey("4");
    cout << "SIZE: " << page1.Payload()->size() << "\n";
    for (auto a : *page1.Payload()) {
        cout << "a: " << a << "\n"; 
        leafNodeCell* cell;
        std::memcpy(&cell, page1.getData()+a,sizeof(leafNodeCell));
        cout << cell->key << "\n";
    }
    return 0;
    cout <<"written.\n";
    Database1.WriteBasicPage(page1);
    LeafPage page2 = Database1.ReadPage(1);
    for (auto a : *page2.Data()) {
        cout << a.key << "\n";
    }
}