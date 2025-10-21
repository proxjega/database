#pragma once

#include <fstream>
#include <string>
#include <vector>
#include <filesystem>

using std::string;
using std::vector;
namespace fs = std::filesystem;

enum class WalOperation : uint8_t {
    SET = 1,
    DELETE = 2,
};

struct WalRecord {
    uint64_t lsn;
    WalOperation operation;
    string key;
    string value;           // empty string for DELETE
    uint64_t timestamp;

    WalRecord() = default;
    WalRecord(uint64_t seqNum, WalOperation op, const string &key, const string &value = "");
};

class WAL {
private:
    string name;
    fs::path pathToWALFile;
    std::fstream walFile;
    uint64_t currentSequenceNumber;

    bool OpenWAL();
    uint64_t GetNextSequenceNumber();
    uint64_t GetCurrentTimestamp();

public:
    WAL(const string &databaseName);
    ~WAL();

    bool LogSet(const string &key, const string &value);
    bool LogDelete(const string &key);

    vector<WalRecord> ReadAllRecords();

    uint64_t GetCurrentSequenceNumber() const { return currentSequenceNumber; }

    bool Clear();
};