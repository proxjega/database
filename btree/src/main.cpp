#include <exception>
#include <iostream>
#include <string>
#include "../include/page.h"
#include "../include/leafpage.h"
#include "../include/internalpage.h"
#include "../include/database.h"

using namespace std;


int main(){
    //WTEST();
    Database DatabaseSetTest("DataBaseSetTest");
    string key;
    DatabaseSetTest.Set("x", "x");
    DatabaseSetTest.Set("p", "p");
    DatabaseSetTest.Set("y", "y");
    DatabaseSetTest.Remove("z");
    auto result1 = DatabaseSetTest.GetFB("z", 100);
    for (auto a : result1) {
        cout << "Key: " << a.key << "\n" << "value: " << a.value << "\n\n";
    }
}
