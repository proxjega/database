#include <stdexcept>
#include <chrono>
#include "../include/logger.hpp"

using std::ios;


WalRecord::WalRecord(uint64_t seqNum, WALOperation op, const string &key, const string &value)
    : lsn(seqNum), operation(op), keyLength(key.length()),
      valueLength(value.length()), timestamp(0) {

};

WAL::WAL(const string &databaseName) : currentSequenceNumber(0) {
    this->name = databaseName;
    fs::path folderName = "data";
    fs::path fileName(databaseName + ".wal");
    this->pathToWALFile = folderName / fileName;

    fs::create_directories(folderName);
    if (fs::exists(this->pathToWALFile)) return;

    if (!OpenWAL()) {
        throw std::runtime_error("Failed to open WAL file");
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

    WalRecord record(GetCurrentSequenceNumber(), WALOperation::SET, key, value);
    record.timestamp = GetCurrentTimestamp();

    this->walFile << record.lsn << "|SET|" << record.timestamp << "|" << key << "|" << value << "\n";

    this->walFile.flush();

    return this->walFile.good();
}

bool WAL::LogDelete(const string &key) {
    if (this->walFile.is_open()) return false;

    WalRecord record(GetNextSequenceNumber(), WALOperation::DELETE, key);
    record.timestamp = GetCurrentTimestamp();

    this->walFile << record.lsn << "|DELETE|" << record.timestamp << "|" << key << "\n";

    this->walFile.flush();

    return this->walFile.good();
}