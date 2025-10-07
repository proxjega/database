#include <iostream>
#include <string>
#include "../include/page.h"
#include "../include/leafpage.h"
#include "../include/internalpage.h"
#include "../include/database.h"

using namespace std;

void TEST(){
    PageHeader header;
    header.pageID=1;
    header.isLeaf = false;
    header.lastSequenceNumber = 1;
    header.offsetToStartOfFreeSpace = sizeof(PageHeader);
    header.offsetToEndOfFreeSpace = 4096;
    header.offsetToStartOfSpecialSpace = -1;
    //test1:
    
    cout << "\n\nTEST1:\n";
    Database Duombaze("duombaze1");
    BasicPage Page1(header);
    Page1.Header()->CoutHeader();
    Duombaze.WriteBasicPage(Page1);
    BasicPage Page2 = Duombaze.ReadPage(1);
    Page2.Header()->CoutHeader();

    //test2:
    cout << "\n\nTEST2:\n";
    Database Duombaze2("duombaze2");
    MetaPage metapage = Duombaze2.ReadPage(0);
    BasicPage rootpage = Duombaze2.ReadPage(1);
    metapage.Header()->CoutHeader();
    rootpage.Header()->CoutHeader();
    rootpage.Header()->isLeaf=true;
    Duombaze2.UpdateMetaPage(metapage);
    Duombaze2.WriteBasicPage(rootpage);
    rootpage = Duombaze2.ReadPage(1);
    rootpage.Header()->CoutHeader();


    //test3: 
    cout << "\n\nTEST3:\n";
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

    //test4:
    cout << "\n\nTEST4:\n";
    InternalPage Page3(2);
    Page3.InsertKeyAndPointer("cc", 5);
    Page3.InsertKeyAndPointer("bb", 3);
    Page3.InsertKeyAndPointer("ss", 2);
    Page3.InsertKeyAndPointer("aa", 1);
    Page3.InsertKeyAndPointer("zz", 1);
    for (int i = 0; i < Page3.Header()->numberOfCells; i++) {
        cout << "offset: " << Page3.Offsets()[i] << ", "; 
        internalNodeCell cell = Page3.GetKeyAndPointer(Page3.Offsets()[i]);
        cout << cell.key << ": " << cell.childPointer << "\n";
    }

    //test5:
    cout << "\n\nTEST5:\n";
    InternalPage Page4(1);
    Page4.InsertKeyAndPointer("aa", 1);
    Page4.InsertKeyAndPointer("xx", 2);
    Page4.InsertKeyAndPointer("bb", 3);
    Page4.InsertKeyAndPointer("dd", 4);
    auto cell = Page4.FindPointerByKey("aa");
    cout << cell << "\n";
    cell = Page4.FindPointerByKey("dd");
    cout <<  cell << "\n";
    cell = Page4.FindPointerByKey("zz");
    cout << cell << "\n";

    //test6:
    Database Database4("db");
    cout << Database1.getPath().string() <<"\n";
    LeafPage page4 = Database1.ReadPage(1);
   
    page4.InsertKeyValue("z", "val2");
    page4.InsertKeyValue("key4", "val2");
    page4.InsertKeyValue("key2", "val2");
    page4.InsertKeyValue("key1", "val1");
    page4.InsertKeyValue("key3", "val3");
    page4.InsertKeyValue("a", "val2");
    bool check = Database1.WriteBasicPage(page1);
    if (!check) cout << "ERROR\n";
    auto get = Database1.Get("za"); //infinite loop!
    if (!get.has_value()) cout << "No key\n";
    else
    cout << get.value().key << ":" << get.value().value;

    //test 7:
    LeafPage leaf(1);
    InternalPage internal(1);
    leaf.InsertKeyValue("a", "1" );
    leaf.InsertKeyValue("b", "2" );
    leaf.InsertKeyValue("c", "3" );
    leaf.InsertKeyValue("d", "4" );
    internal.InsertKeyAndPointer("a", 1 );
    internal.InsertKeyAndPointer("b", 2 );
    internal.InsertKeyAndPointer("c", 3 );
    internal.InsertKeyAndPointer("d", 4 );
    leaf.CoutPage();
    leaf.RemoveKey("b");
    leaf.CoutPage();
    internal.CoutPage();
    internal.RemoveKey("d");
    internal.RemoveKey("a");
    internal.CoutPage();
}

int main(){
    //WTEST();
    Database DataBaseSetTest("DataBaseSetTest");
    for (long int i = 100000000000; i < 100000001000; i++) {
        DataBaseSetTest.Set(to_string(i), to_string(i));
    }
    auto cell = DataBaseSetTest.Get("100000000000");
    cout << cell->value;
    // DataBaseSetTest.CoutDatabase();

}