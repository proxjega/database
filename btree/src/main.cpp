#include "../include/page.h"
#include "../include/database.h"

#define KEYS_COUNT 100

using namespace std;


int main(){
    //WTEST();
    Database DatabaseSetTest("DataBaseSetTest");
    DatabaseSetTest.Set("x", "x");
    DatabaseSetTest.Set("p", "p");
    DatabaseSetTest.Set("y", "y");
    DatabaseSetTest.Remove("z");
    auto result = DatabaseSetTest.GetFB("z", KEYS_COUNT);
    for (const auto& cell : result) {
        cout << "Key: " << cell.key << "\n" << "value: " << cell.value << "\n\n";
    }
}
