#pragma once

#include "page.h"


/**
 * @brief Internal page class for internal b+tree nodes. Stores key:pointer.
 * Stored pointers are page ids.
 * Pointer points to page, that has keys smaller than key stored with pointer
 * Special1 stores pointer to last page. Special2 doesn't store anything
 */
class InternalPage : public BasicPage {
       friend class Database;
    public:
        // Constructors
        explicit InternalPage(uint32_t pageID);
        using BasicPage::BasicPage;

        // Helpers
        uint32_t FindPointerByKey(const string &key);
        uint16_t FindInsertPosition(const string& key);
        int16_t FindKeyIndex(const string& key);
        bool WillFit(const string &key, uint32_t pointer);

        // Operations
        bool InsertKeyAndPointer(string key, uint32_t pointer);
        internalNodeCell GetKeyAndPointer(uint16_t offset);
        void UpdatePointerToTheRightFromKey(const string& key, uint32_t pointer);
        void RemoveKey(const string &key);

        // For debug
        void CoutPage();

};
