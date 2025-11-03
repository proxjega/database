#pragma once

#include <fstream>
#include <string>
#include <vector>
#include <filesystem>

using std::string;
using std::vector;
namespace fs = std::filesystem;

enum class WalOperation : uint8_t { SET, DELETE };

struct WalRecord {
    uint64_t lsn;
    WalOperation operation;
    string key;
    string value;           // empty string for DELETE

    WalRecord() = default;
    WalRecord(uint64_t seqNum, WalOperation op, const string &key, const string &value = "");
};

class WAL {
private:
    string name;
    fs::path walDirectory;
    fs::path currentWalPath;
    std::fstream walFile;
    uint64_t currentSequenceNumber;
    uint64_t currentSegmentNumber;
    size_t maxSegmentSize;
    size_t currentSegmentSize;


    bool RotateWAL();
    fs::path GetSegmentPath(uint64_t segmentNum) const;
    vector<fs::path> GetAllSegments() const;
    bool ShouldRotate() const;

    bool OpenWAL();
    uint64_t GetNextSequenceNumber();
    WalRecord ParseWalRecord(const string &line);

public:
    WAL(const string &databaseName, size_t MaxSegmentSizeBytes = 5 * 1024); // Default 5MB
    ~WAL();

    bool LogSet(const string &key, const string &value);
    bool LogDelete(const string &key);

    vector<WalRecord> ReadAll();
    vector<WalRecord> ReadFrom(const uint64_t lsn);

    uint64_t GetCurrentSequenceNumber() const { return currentSequenceNumber; }
    uint64_t GetCurrentSegmentNumber() const { return currentSegmentNumber; }

    bool ClearAll();
    bool ClearUpTo(uint64_t lsn);
    bool DeleteOldSegments(uint64_t beforeSegment);
};