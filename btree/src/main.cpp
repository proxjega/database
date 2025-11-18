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
    for (int i = 65; i <125; i++) {
        key="";
        for (int j = 0; j < 255; j++) {
            key.push_back(static_cast<char>(i));
        }
        DatabaseSetTest.Set(key, key);
    }


    auto result = DatabaseSetTest.GetKeys();
    for (auto &key : result){
        cout << key<< "\n";
    }
}
