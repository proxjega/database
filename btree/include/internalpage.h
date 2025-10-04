#pragma once

#include "page.h"

class InternalPage : public BasicPage {
       friend class Database;
    public:
        using BasicPage::BasicPage;

        InternalPage(uint32_t ID);

        uint32_t FindPointerByKey(const string &key);
        uint16_t FindInsertPosition(const string& key);
        void InsertKeyAndPointer(string key, uint32_t pointer);
        internalNodeCell GetKeyAndPointer(uint16_t offset);
        void CoutPage();

};