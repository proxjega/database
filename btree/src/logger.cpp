#include <stdexcept>
#include <chrono>
#include <fstream>
#include "../include/logger.hpp"
#include "../../btree/include/database.h"

using std::ios;
using std::ifstream;
using std::getline;
using std::istringstream;

WalRecord::WalRecord(uint64_t seqNum, WalOperation op, const string &key, const string &value)
    : lsn(seqNum), operation(op), key(key),
      value(value), timestamp(0) {

};

WAL::WAL(const string &databaseName) : name(databaseName), currentSequenceNumber(0) {
    fs::path folderName = "data";
    fs::path fileName(databaseName + ".log");
    this->pathToWALFile = folderName / fileName;

    fs::create_directories(folderName);
    if (fs::exists(this->pathToWALFile)) return;

    if (!OpenWAL()) {
        throw std::runtime_error("Failed to open Log file");
    }

    if (fs::exists(this->pathToWALFile) && fs::file_size(this->pathToWALFile) > 0) {
        auto records = this->ReadAllRecords();
        if (!records.empty()) {
            this->currentSequenceNumber = records.back().lsn;
        }
    }
}

WAL::~WAL() {
    if (this->walFile.is_open()) {
        this->walFile.close();
    }
}

bool WAL::OpenWAL() {
    this->walFile.open(this->pathToWALFile.string(), ios::in | ios::out | ios::binary | ios::app);

    if (!this->walFile) {
        this->walFile.clear();
        this->walFile.open(this->pathToWALFile.string(), ios::out | ios::binary);
        this->walFile.close();
        this->walFile.open(this->pathToWALFile.string(), ios::in | ios::out | ios::binary | ios::app);
    }

    return this->walFile.is_open();
}

uint64_t WAL::GetNextSequenceNumber() {
    return ++this->currentSequenceNumber;
}

uint64_t WAL::GetCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
}

bool WAL::LogSet(const string &key, const string &value) {
    if (!this->walFile.is_open()) return false;

    WalRecord record(GetNextSequenceNumber(), WalOperation::SET, key, value);
    record.timestamp = this->GetCurrentTimestamp();

    this->walFile << record.lsn << "|SET|" << record.timestamp << "|" << key << "|" << value << "\n";

    this->walFile.flush();

    return !this->walFile.fail();
}

bool WAL::LogDelete(const string &key) {
    if (!this->walFile.is_open()) return false;

    WalRecord record(GetNextSequenceNumber(), WalOperation::DELETE, key);
    record.timestamp = this->GetCurrentTimestamp();

    this->walFile << record.lsn << "|DELETE|" << record.timestamp << "|" << key << "\n";

    this->walFile.flush();

    return !this->walFile.fail();
}

vector<WalRecord> WAL::ReadAllRecords() {
    vector<WalRecord> records;

    if (!fs::exists(this->pathToWALFile)) {
        return records;
    }

    ifstream LogFile(this->pathToWALFile.string(), ios::in);
    if (!LogFile) {
        return records;
    }

    string line;
    while(getline(LogFile, line)) {
        if (line.empty()) continue;

        istringstream iss(line);
        string lsnStr, opStr, timestampStr, key, value;

        if (!getline(iss, lsnStr, '|')) continue;
        if (!getline(iss, opStr, '|')) continue;
        if (!getline(iss, timestampStr, '|')) continue;
        if (!getline(iss, key, '|')) continue;

        WalRecord record;
        record.lsn = std::stoull(lsnStr);
        record.timestamp = std::stoull(timestampStr);
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
            continue; // Unknown operation
        }

        records.push_back(record);
    }

    LogFile.close();
    return records;
}

bool WAL::Clear() {
    if (this->walFile.is_open()) {
        this->walFile.close();
    }

    try {
        std::ofstream clearFile(this->pathToWALFile.string(), ios::out | ios::trunc);
        clearFile.close();

        this->currentSequenceNumber = 0;

        if (!this->OpenWAL()) {
            return false;
        }

        return true;
    } catch (const std::exception &e) {
        return false;
    }
}