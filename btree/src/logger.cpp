#include <stdexcept>
#include <fstream>
#include <algorithm>
#include "../include/logger.hpp"
#include "../../btree/include/database.h"

using std::ios;
using std::ifstream;
using std::getline;
using std::istringstream;

WalRecord::WalRecord(uint64_t seqNum, WalOperation op, const string &key, const string &value)
    : lsn(seqNum), operation(op), key(key), value(value) {};

WAL::WAL(const string &name, size_t MaxSegmentSizeBytes)
    : name(name), currentSequenceNumber(0), currentSegmentNumber(0),
      maxSegmentSize(MaxSegmentSizeBytes), currentSegmentSize(0) {


    fs::path dataFolderName = "data";
    fs::path WALFolderName = "log";
    this->walDirectory = dataFolderName / WALFolderName / name;

    fs::create_directories(this->walDirectory);

    auto segments = this->GetAllSegments();
    if (!segments.empty()) {
        uint64_t maxSegment = 0;
        for (const auto &segPath : segments) {
            string filename = segPath.filename().string();
            size_t underscorePos = filename.find('_');
            size_t dotPos = filename.find('.');

            if (underscorePos != string::npos && dotPos != string::npos) {
                uint64_t segNum = std::stoull(filename.substr(underscorePos + 1, dotPos - underscorePos - 1));
                maxSegment = std::max(maxSegment, segNum);
            }
        }

        this->currentSegmentNumber = maxSegment;
        this->currentWalPath = this->GetSegmentPath(this->currentSegmentNumber);

        auto records = this->ReadAll();
        if (!records.empty()) {
            this->currentSequenceNumber = records.back().lsn;
        }

        if (fs::exists(this->currentWalPath)) {
            this->currentSegmentSize = fs::file_size(this->currentWalPath);
        }
    } else {
        this->currentWalPath = this->GetSegmentPath(this->currentSegmentNumber);
    }

    if (!this->OpenWAL()) {
        throw std::runtime_error("Failed to open Log file");
    }
}

WAL::~WAL() {
    if (this->walFile.is_open()) {
        this->walFile.close();
    }
}

fs::path WAL::GetSegmentPath(uint64_t segmentNum) const {
    return this->walDirectory / (this->name + "_" + std::to_string(segmentNum) + ".log");
}

vector<fs::path> WAL::GetAllSegments() const {
    vector<fs::path> segments;

    if (!fs::exists(this->walDirectory)) {
        return segments;
    }

    for (const auto &entry : fs::directory_iterator(walDirectory)) {
        if (entry.is_regular_file() && entry.path().extension() == ".log") {
            segments.push_back(entry.path());
        }
    }

    std::sort(segments.begin(), segments.end(), [](const fs::path &a, const fs::path &b) {
        string filenameA = a.filename().string();
        string filenameB = b.filename().string();

        size_t underscoreA = filenameA.find('_');
        size_t dotA = filenameA.find('.');

        size_t underscoreB = filenameB.find('_');
        size_t dotB = filenameB.find('.');

        if (underscoreA != string::npos && dotA != string::npos &&
            underscoreB != string::npos && dotB != string::npos) {
                u_int64_t numA = std::stoull(filenameA.substr(underscoreA + 1, dotA - underscoreA - 1));
                u_int64_t numB = std::stoull(filenameB.substr(underscoreB + 1, dotB - underscoreB - 1));
                return numA < numB;
        }
        return false;
    });

    return segments;
}

bool WAL::ShouldRotate() const {
    return this->currentSegmentSize >= this->maxSegmentSize;
}

bool WAL::RotateWAL() {
    if (this->walFile.is_open()) {
        this->walFile.close();
    }

    this->currentSegmentNumber++;
    this->currentWalPath = this->GetSegmentPath(this->currentSegmentNumber);
    this->currentSegmentSize = 0;

    return this->OpenWAL();
}

bool WAL::OpenWAL() {
    this->walFile.open(this->currentWalPath.string(), ios::in | ios::out | ios::binary | ios::app);

    if (!this->walFile) {
        this->walFile.clear();
        this->walFile.open(this->currentWalPath.string(), ios::out | ios::binary);
        this->walFile.close();
        this->walFile.open(this->currentWalPath.string(), ios::in | ios::out | ios::binary | ios::app);
    }

    return this->walFile.is_open();
}

uint64_t WAL::GetNextSequenceNumber() {
    return ++this->currentSequenceNumber;
}

WalRecord WAL::ParseWalRecord(const string &line) {
    WalRecord record;

    if (line.empty()) {
        record.lsn = 0;
        return record;
    }

    istringstream iss(line);
    string lsnStr, opStr, key, value;

    if (!getline(iss, lsnStr, '|')) {
        record.lsn = 0;
        return record;
    }
    if (!getline(iss, opStr, '|')) {
        record.lsn = 0;
        return record;
    }
    if (!getline(iss, key, '|')) {
        record.lsn = 0;
        return record;
    }

    record.lsn = std::stoull(lsnStr);
    record.key = key;

    if (opStr == "SET") {
        record.operation = WalOperation::SET;
        if (getline(iss, value)) {
            record.value = value;
        } else {
            record.value = "";
        }
    } else if (opStr == "DELETE") {
        record.operation = WalOperation::DELETE;
        record.value = "";
    } else {
        record.lsn = 0; // Unknown operation
    }

    return record;
}

bool WAL::LogSet(const string &key, const string &value) {
    if (!this->walFile.is_open()) return false;

    if (this->ShouldRotate()) {
        if (!this->RotateWAL()) {
            return false;
        }
    }

    WalRecord record(GetNextSequenceNumber(), WalOperation::SET, key, value);

    auto startPos = this->walFile.tellp();

    this->walFile << record.lsn << "|SET|" << record.key << "|" << record.value << "\n";
    this->walFile.flush();

    auto endPos = this->walFile.tellp();
    this->currentSegmentSize += (endPos - startPos);

    return !this->walFile.fail();
}

bool WAL::LogDelete(const string &key) {
    if (!this->walFile.is_open()) return false;

    if (this->ShouldRotate()) {
        if (!this->RotateWAL()) {
            return false;
        }
    }

    WalRecord record(GetNextSequenceNumber(), WalOperation::DELETE, key);

    auto startPos = this->walFile.tellp();

    this->walFile << record.lsn << "|DELETE|" << record.key << "\n";
    this->walFile.flush();

    auto endPos = this->walFile.tellp();
    this->currentSegmentSize += (endPos - startPos);

    return !this->walFile.fail();
}

vector<WalRecord> WAL::ReadAll() {
    vector<WalRecord> records;
    auto segments = this->GetAllSegments();

    for (const auto &segmentPath : segments) {
        if (!fs::exists(segmentPath)) continue;

        ifstream logFile(segmentPath.string(), ios::in);
        if (!logFile) continue;

        string line;
        while(getline(logFile, line)) {
            auto record = this->ParseWalRecord(line);
            if (record.lsn != 0) records.push_back(record);
        }

        logFile.close();
    }

    return records;
}

vector<WalRecord> WAL::ReadFrom(const uint64_t lsn) {
    vector<WalRecord> records;
    auto segments = this->GetAllSegments();

    for (const auto &segmentPath : segments) {
        if (!fs::exists(segmentPath)) continue;

        ifstream logFile(segmentPath.string(), ios::in);
        if (!logFile) continue;

        string line;
        while(getline(logFile, line)) {
            auto record = this->ParseWalRecord(line);
            if (record.lsn != 0 && record.lsn > lsn) {
                records.push_back(record);
            }
        }

        logFile.close();
    }

    return records;
}

bool WAL::HasPendingRecords() {
    auto records = this->ReadAll();
    return !records.empty();
}


bool WAL::ClearAll() {
    if (this->walFile.is_open()) {
        this->walFile.close();
    }

    try {
        auto segments = this->GetAllSegments();
        for (const auto &segment : segments) {
            fs::remove(segment);
        }

        this->currentSequenceNumber = 0;
        this->currentSegmentNumber = 0;
        this->currentSegmentSize = 0;
        this->currentWalPath = this->GetSegmentPath(currentSegmentNumber);

        if (!this->OpenWAL()) {
            return false;
        }

        return true;
    } catch (const std::exception &e) {
        return false;
    }
}

bool WAL::ClearUpTo(uint64_t lsn) {
    auto segments = GetAllSegments();
    vector<WalRecord> recordsToKeep;

    for (const auto &segmentPath : segments) {
        if (!fs::exists(segmentPath)) continue;

        ifstream logFile(segmentPath.string(), ios::in);
        if (!logFile) continue;

        string line;
        while(getline(logFile, line)) {
            auto record = this->ParseWalRecord(line);
            if (record.lsn != 0 && record.lsn > lsn) {
                recordsToKeep.push_back(record);
            }
        }

        logFile.close();
    }

    if (this->walFile.is_open()) {
        this->walFile.close();
    }

    for (const auto& segment : segments) {
        fs::remove(segment);
    }

    this->currentSegmentNumber = 0;
    this->currentSegmentSize = 0;
    this->currentWalPath = this->GetSegmentPath(this->currentSegmentNumber);

    if (!this->OpenWAL()) {
        return false;
    }

    for (const auto& record : recordsToKeep) {
        if (record.operation == WalOperation::SET) {
            this->walFile << record.lsn << "|SET|" << record.key << "|" << record.value << "\n";
        } else {
            this->walFile << record.lsn << "|DELETE|" << record.key << "\n";
        }
    }

    this->walFile.flush();

    if (fs::exists(currentWalPath)) {
        this->currentSegmentSize = fs::file_size(currentWalPath);
    }

    return true;
}

bool WAL::DeleteOldSegments(uint64_t beforeSegment) {
    try {
        auto segments = this->GetAllSegments();

        for (const auto& segmentPath : segments) {
            string filename = segmentPath.filename().string();
            size_t underscorePos = filename.find('_');
            size_t dotPos = filename.find('.');

            if (underscorePos != string::npos && dotPos != string::npos) {
                uint64_t segNum = std::stoull(filename.substr(underscorePos + 1, dotPos - underscorePos - 1));
                if (segNum < beforeSegment) {
                    fs::remove(segmentPath);
                }
            }
        }

        return true;
    } catch (const std::exception &e) {
        return false;
    }
}