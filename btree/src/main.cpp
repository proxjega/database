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
    Database Database1("db");
    LeafPage page1 = Database1.ReadPage(1);
   
    page1.InsertKeyValue("z", "val2");
    page1.InsertKeyValue("key4", "val2");
    page1.InsertKeyValue("key2", "val2");
    page1.InsertKeyValue("key1", "val1");
    page1.InsertKeyValue("key3", "val3");
    page1.InsertKeyValue("a", "val2");
    for (int i = 0; i < page1.Header()->numberOfCells; i++) {
        cout << "offset: " << page1.Offsets()[i] << "\n"; 
        leafNodeCell cell = page1.GetKeyValue(page1.Offsets()[i]);
        cout << cell.key << ": " << cell.value << "\n";
    }
    Database1.WriteBasicPage(page1);
    cout <<"written.\n";
    LeafPage page2 = Database1.ReadPage(1);
    for (int i = 0; i < page2.Header()->numberOfCells; i++) {
        cout << "offset: " << page2.Offsets()[i] << "\n"; 
        leafNodeCell cell = page2.GetKeyValue(page2.Offsets()[i]);
        cout << cell.key << ": " << cell.value << "\n";
    }
}

int main(){
    
    InternalPage Page1(2);
    Page1.InsertKeyAndPointer("zz", 5);
    Page1.InsertKeyAndPointer("bb", 3);
    Page1.InsertKeyAndPointer("ss", 2);
    Page1.InsertKeyAndPointer("aa", 1);
    for (int i = 0; i < Page1.Header()->numberOfCells; i++) {
        cout << "offset: " << Page1.Offsets()[i] << "\n"; 
        internalNodeCell cell = Page1.GetKeyAndPointer(Page1.Offsets()[i]);
        cout << cell.key << ": " << cell.childPointer << "\n";
    }
}