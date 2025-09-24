#pragma once

#include <filesystem>
#include <fstream>
#include <string>
#include <filesystem>

using std::string;
using std::ofstream;
namespace fs=std::filesystem;

/**
 * @brief Database class
 * 
 */
class Database {
    
    private:
        string name;
        fs::path pathToDatabaseFile;
    public:
    /**
     * @brief Construct a new Database object
     * 
     * @param name 
     */
    Database(string name) {
        this->name = name;
        fs::path fileName(name+".db");
        fs::path folderName = "data"; 
        this->pathToDatabaseFile = folderName / fileName;
        ofstream DatabaseFile(this->pathToDatabaseFile.string());
    }
};


