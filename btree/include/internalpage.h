#pragma once

#include "page.h"

class InternalPage : public BasicPage {
       friend class Database;
    public:
        // Constructors
        InternalPage(uint32_t ID);
        using BasicPage::BasicPage;

        // Helpers
        uint32_t FindPointerByKey(const string &key);
        uint16_t FindInsertPosition(const string& key);
        int16_t FindKeyIndex(const string& key);
        bool WillFit(string key, uint32_t pointer);

        // Operations
        bool InsertKeyAndPointer(string key, uint32_t pointer);
        internalNodeCell GetKeyAndPointer(uint16_t offset);
        void UpdatePointerToTheRightFromKey(const string& key,uint32_t pointer);
        void RemoveKey(const string &key);

        // For debug
        void CoutPage();

};