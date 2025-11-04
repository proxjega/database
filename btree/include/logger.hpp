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
    uint64_t lsn{1};
    WalOperation operation{WalOperation::SET};
    string key;
    string value;           // empty string for DELETE

    WalRecord() = default;
    WalRecord(uint64_t seqNum, WalOperation operation, const string &key, const string &value = "");
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
    static WalRecord ParseWalRecord(const string &line);

public:
    static constexpr size_t DEFAULT_SEGMENT_SIZE = 5UL * 1024UL * 1024UL;

    explicit WAL(const string &databaseName, size_t MaxSegmentSizeBytes = DEFAULT_SEGMENT_SIZE); // Default 5MB

    bool LogSet(const string &key, const string &value);
    bool LogDelete(const string &key);

    vector<WalRecord> ReadAll();
    vector<WalRecord> ReadFrom(uint64_t lsn);
    bool HasPendingRecords();

    uint64_t GetCurrentSequenceNumber() const { return currentSequenceNumber; }
    uint64_t GetCurrentSegmentNumber() const { return currentSegmentNumber; }

    bool ClearAll();
    bool ClearUpTo(uint64_t lsn);
    bool DeleteOldSegments(uint64_t beforeSegment);
};