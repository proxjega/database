#pragma once

#include "page.h"

class InternalPage : public BasicPage {
       friend class Database;
    public:
        using BasicPage::BasicPage;

        InternalPage(uint32_t ID);

        uint32_t FindPointerByKey(const string &key);
        uint16_t FindInsertPosition(const string& key);
        int16_t FindKeyIndex(const string& key);
        void UpdatePointerToTheRightFromKey(const string& key,uint32_t pointer);

        
        bool InsertKeyAndPointer(string key, uint32_t pointer);
        internalNodeCell GetKeyAndPointer(uint16_t offset);
        void RemoveKey(const string &key);
        void CoutPage();

};