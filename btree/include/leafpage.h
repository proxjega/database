#pragma once

#include "page.h"

class LeafPage : public BasicPage {
       friend class Database;
    public:
        using BasicPage::BasicPage;

        LeafPage(uint32_t ID);

        uint16_t FindInsertPosition(const string& key);
        int16_t FindKeyIndex(const string& key);

        bool InsertKeyValue(string key, string value);
        leafNodeCell GetKeyValue(uint16_t offset);
        std::optional<leafNodeCell> FindKey(const string &key);
        void RemoveKey(const string &key);
        void CoutPage();
};

