#pragma once

#include <fstream>
#include <string>
#include <vector>
#include <filesystem>

using std::string;
using std::vector;
namespace fs = std::filesystem;

/**
 * Enumerator for WAL operations (LOG arba DELETE)
*/
enum class WalOperation : uint8_t { SET, DELETE };

/**
 * @brief Struct for WAL record.
 *
*/
struct WalRecord {
    uint64_t lsn{1};
    WalOperation operation{WalOperation::SET};
    string key;
    string value;           // empty string for DELETE

    WalRecord() = default;
    WalRecord(uint64_t seqNum, WalOperation operation, string key, string value = "");
};

/**
 * @brief Struct for WAL.
 *
 */
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
    fs::path GetSegmentPath(const uint64_t &segmentNum) const;
    vector<fs::path> GetAllSegments() const;
    bool ShouldRotate() const;

    bool OpenWAL();
    uint64_t GetNextSequenceNumber();
    static WalRecord ParseWalRecord(const string &line);

    bool WriteRecordToStream(const WalRecord& record);

public:
    static constexpr size_t DEFAULT_SEGMENT_SIZE = 1UL * 1024UL;

    explicit WAL(const string &databaseName, size_t MaxSegmentSizeBytes = DEFAULT_SEGMENT_SIZE); // Default 16MB. Same as Postgres

    bool LogSet(const string &key, const string &value);
    bool LogDelete(const string &key);

    bool LogWithLSN(WalRecord &walRecord);

    vector<WalRecord> ReadAll();
    vector<WalRecord> ReadFrom(const uint64_t &lsn);
    bool HasPendingRecords();

    uint64_t GetCurrentSequenceNumber() const { return currentSequenceNumber; }
    uint64_t GetCurrentSegmentNumber() const { return currentSegmentNumber; }

    bool ClearAll();
    bool ClearUpTo(const uint64_t &lsn);
    bool DeleteOldSegments(const uint64_t &beforeSegment);
};