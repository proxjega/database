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
    for (int i = 65; i <125; i++) {
        key="";
        for (int j = 0; j < 255; j++) {
            key.push_back(static_cast<char>(i));
        }
        try {
            DatabaseSetTest.Set(key, key);
        }
        catch (std::exception& e) {
            std::cerr << e.what() << endl;
        }
    }

    auto result1 = DatabaseSetTest.GetKeysPaging(20, 1);
    auto result2 = DatabaseSetTest.GetKeysValuesPaging(20, 2);
    for (auto a : result1.keys) {
        cout << "Key: " << a << "\n";
    }
    for (auto a : result2.keyValuePairs) {
        cout << "Key: " << a.key << "\n" << "value: " << a.value << "\n\n";
    }
}
