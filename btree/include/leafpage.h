#pragma once

#include "page.h"

class LeafPage : public BasicPage {
       friend class Database;
    public:
        
        // Constructors
        using BasicPage::BasicPage;
        LeafPage(uint32_t ID);

        // Helpers
        uint16_t FindInsertPosition(const string& key);
        int16_t FindKeyIndex(const string& key);
        bool WillFit(string key, string value);

        // Operations
        bool InsertKeyValue(string key, string value);
        leafNodeCell GetKeyValue(uint16_t offset);
        std::optional<leafNodeCell> FindKey(const string &key);
        void RemoveKey(const string &key);
        
        // For debug
        void CoutPage();
};

