#pragma once

#include "page.h"

/**
 * @brief LeafPage class for leaf nodes in b+tree. Stores key:value pairs.
 * Special1 stores pointer to previous leaf. Special2 stores pointer to next leaf
 *
 */
class LeafPage : public BasicPage {
       friend class Database;
    public:

        // Constructors
        using BasicPage::BasicPage;
        explicit LeafPage(uint32_t pageID);

        // Helpers
        uint16_t FindInsertPosition(const string& key);
        int16_t FindKeyIndex(const string& key);
        bool WillFit(const string &key, const string &value);
        LeafPage Optimize();

        // Operations
        bool InsertKeyValue(string key, string value);
        leafNodeCell GetKeyValue(uint16_t offset);
        std::optional<leafNodeCell> FindKey(const string &key);
        void RemoveKey(const string &key);

        // For debug
        void CoutPage();
};
