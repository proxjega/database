#pragma once

#include "page.h"

class LeafPage : public BasicPage {
       friend class Database;
    public:
        using BasicPage::BasicPage;

        LeafPage(uint32_t ID);

        uint16_t FindInsertPosition(const string& key);
        void InsertKeyValue(string key, string value);
        leafNodeCell GetKeyValue(uint16_t offset);
        std::optional<leafNodeCell> FindKey(const string &key);
        void CoutPage();
};

