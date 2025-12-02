#include <algorithm>
#include <stdexcept>
#include <utility>
#include <iostream>
#include <sstream>
#include "../include/logger.hpp"
#include "../../Replication/include/common.hpp"

using std::ios;
using std::ifstream;
using std::getline;
using std::istringstream;

namespace {
// Helper: Replace \n with a placeholder (e.g., \0) to keep WAL on one line
string EscapeValue(string value) {
    for (auto &character : value) {
        if (character == '\n') {
            character = '\0'; // Replace newline with null char (or use a placeholder like 0x1F)
        }
    }
    return value;
}

// Helper: Restore \n from placeholder
string UnescapeValue(string value) {
    for (auto &character : value) {
        if (character == '\0') {
            character = '\n';
        }
    }
    return value;
}
}

WalRecord::WalRecord(uint64_t seqNum, WalOperation operation, string key, string value)
    : lsn(seqNum), operation(operation), key(std::move(key)), value(std::move(value)) {};

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

        // Skaitome visus WAL failus.
        auto records = this->ReadAll();
        if (!records.empty()) {
            // Priskiriame paskutinio įrašo kaip LSN
            this->currentSequenceNumber = records.back().lsn;
        }

        if (fs::exists(this->currentWalPath)) {
            // Nustatome dabartinio WAL dydį
            this->currentSegmentSize = fs::file_size(this->currentWalPath);
        }
    } else {
        // Gauname dabartinio WAL direktoriją.
        this->currentWalPath = this->GetSegmentPath(this->currentSegmentNumber);
    }

    if (!this->OpenWAL()) {
        throw std::runtime_error("Failed to open Log file");
    }
}

fs::path WAL::GetSegmentPath(const uint64_t &segmentNum) const {
    return this->walDirectory / (this->name + "_" + std::to_string(segmentNum) + ".log");
}

/**
 * @brief Get all segments paths
 * @return vector of paths.
*/
vector<fs::path> WAL::GetAllSegments() const {
    vector<fs::path> segments;

    if (!fs::exists(this->walDirectory)) {
        return segments;
    }

    for (const auto &entry : fs::directory_iterator(walDirectory)) {
        // Patikriname ar tai yra WAL failas.
        if (entry.is_regular_file() && entry.path().extension() == ".log") {
            segments.push_back(entry.path());
        }
    }

    // Rūšiuojame didėjimo tvarka pagal segmento numerį.
    std::sort(segments.begin(), segments.end(), [](const fs::path &pathA, const fs::path &pathB) {
        string filenameA = pathA.filename().string();
        string filenameB = pathB.filename().string();

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

/**
 * @brief Checks if a new WAL should be started/opened
*/
bool WAL::ShouldRotate() const {
    return this->currentSegmentSize >= this->maxSegmentSize;
}

/**
 * @brief Rotates to a new WAL (Uždaro dabartinį WAL ir atidaro nauja WAL su +1 segmento numeriu)
 * @return true if rotation happened successfully.
*/
bool WAL::RotateWAL() {
    if (this->walFile.is_open()) {
        this->walFile.close();
    }

    this->currentSegmentNumber++;
    this->currentWalPath = this->GetSegmentPath(this->currentSegmentNumber);
    this->currentSegmentSize = 0;

    return this->OpenWAL();
}

/**
 *@brief Opens WAL.
 @return true if opened WAL successfully.
*/
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

/**
 @brief Išanalizduoja (parse) duotą eilutę ir grąžina WalRecord objektą. Tik, kad jeigu kažkas negerai su ta eilute, tai grąžinamo WalRecord objekto LSN bus 0.
*/
WalRecord WAL::ParseWalRecord(const string &line) {
    WalRecord record;

    // Patikriname ar eilutė netuščia, jei tuščia, tai LSN nurodome, kad yra 0 (klaidos žyma šiuo atveju).
    if (line.empty()) {
        record.lsn = 0;
        return record;
    }

    istringstream iss(line);
    string lsnStr;
    string opStr;
    string key;
    string value;

    // Tikriname ar eilutėje yra LSN, OPERATION tipas (SET arba DELETE) ir raktas (key).
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
            record.value = UnescapeValue(value);
        } else {
            record.value = "";
        }
    } else if (opStr == "DELETE") {
        record.operation = WalOperation::DELETE;
        // Kadangi tipas yra DELETE, tai reikšmė šiame įrašę yra tuščia.
        record.value = "";
    } else {
        record.lsn = 0; // Unknown operation
    }

    return record;
}

bool WAL::WriteRecordToStream(const WalRecord& record) {
    auto startPos = this->walFile.tellp();

    this->walFile << record.lsn << "|";
    if (record.operation == WalOperation::SET) {
        this->walFile << "SET|" << record.key << "|" << format_length_prefixed_value(EscapeValue(record.value)) << "\n";
    } else {
        this->walFile << "DELETE|" << record.key << "\n";
    }
    this->walFile.flush();

    auto endPos = this->walFile.tellp();
    this->currentSegmentSize += (endPos - startPos);
    return !this->walFile.fail();
}

/**
 * @brief Logs SET operation to WAL.
 * @param key raktas
 * @param value reikšmė
 * @return true if SET operation was logged successfully.
*/
bool WAL::LogSet(const string &key, const string &value) {
    if (!this->walFile.is_open()) {
        return false;
    }

    // Prieš log'inimą patikriname ar nereikia rotuoti į naują WAL.
    if (this->ShouldRotate()) {
        if (!this->RotateWAL()) {
            return false;
        }
    }

    WalRecord record(GetNextSequenceNumber(), WalOperation::SET, key, value);

    // Gauname dabartinę poziciją/vietą nuo kurios bus rašomą i WAL.
    auto startPos = this->walFile.tellp();

    // Log'iname ir iškarto flush'iname į WAL'ą (.log failą).
    this->walFile << record.lsn << "|SET|" << record.key << "|" << format_length_prefixed_value(EscapeValue(record.value)) << "\n";
    this->walFile.flush();

    // Gauname dabartinę poziciją/vietą į kurią būtų rašoma. (Kitaip, vieta faile, kurioje buvo pabaigta rašyti).
    auto endPos = this->walFile.tellp();

    // Pridedamame prie dabartinio WAL dydžio skirtumą tarp vietos, kurioje buvo baigta rašyti/log'inti ir vietos nuo kurios buvo pradėta rašyti/log'inti.
    this->currentSegmentSize += (endPos - startPos);

    return !this->walFile.fail();
}

/**
 * @brief Logs DELETE operation to WAL.
 * @param key raktas
 * @return true if DELETE operation was logged successfully.
*/
bool WAL::LogDelete(const string &key) {
    if (!this->walFile.is_open()) {
        return false;
    }

    // Prieš log'inimą patikriname ar nereikia rotuoti į naują WAL.
    if (this->ShouldRotate()) {
        if (!this->RotateWAL()) {
            return false;
        }
    }

    WalRecord record(GetNextSequenceNumber(), WalOperation::DELETE, key);

    // Gauname dabartinę poziciją/vietą nuo kurios bus rašomą i WAL.
    auto startPos = this->walFile.tellp();

    // Log'iname ir iškarto flush'iname į WAL'ą (.log failą).
    this->walFile << record.lsn << "|DELETE|" << record.key << "\n";
    this->walFile.flush();

    // Gauname dabartinę poziciją/vietą į kurią būtų rašoma. (Kitaip, vieta faile, kurioje buvo pabaigta rašyti).
    auto endPos = this->walFile.tellp();

    // Pridedamame prie dabartinio WAL dydžio skirtumą tarp vietos, kurioje buvo baigta rašyti/log'inti ir vietos nuo kurios buvo pradėta rašyti/log'inti.
    this->currentSegmentSize += (endPos - startPos);

    return !this->walFile.fail();
}

bool WAL::LogWithLSN(WalRecord &walRecord) {
    if (!this->walFile.is_open()) {
        return false;
    }

    if (this->ShouldRotate()) {
        if (!this->RotateWAL()) {
            return false;
        }
    }

    // Update internal sequence number to track the highest seen LSN
    this->currentSequenceNumber = std::max(walRecord.lsn, this->currentSequenceNumber);

    WalRecord record(walRecord.lsn, walRecord.operation, walRecord.key, walRecord.value);
    return WriteRecordToStream(record);
}

/**
 @brief Read every WAL.
 @return vector of WalRecord. Literally every record of every WAL.
*/
vector<WalRecord> WAL::ReadAll() {
    vector<WalRecord> records;
    auto segments = this->GetAllSegments();


    // Iteruojame per visus segmentus.
    for (const auto &segmentPath : segments) {
        if (!fs::exists(segmentPath)) {
            continue;
        }

        ifstream logFile(segmentPath.string(), ios::in);
        if (!logFile) {
            continue;
        }

        // Skaitome kiekvien1 eilutę ir nuskaitomę įrašą.
        string line;
        while(getline(logFile, line)) {
            auto record = ParseWalRecord(line);
            if (record.lsn != 0) {
                records.push_back(record);
            }
        }

        logFile.close();
    }

    return records;
}

/**
 * @brief Nuskaito visus įrašus, kurių LSN didesnis už nurodytą
 * @param lsn LSN riba
 * @return įrašų, kurių LSN > nurodyto LSN, vektorius
*/
vector<WalRecord> WAL::ReadFrom(const uint64_t &lsn) {
    vector<WalRecord> records;
    auto segments = this->GetAllSegments();

    for (const auto &segmentPath : segments) {
        if (!fs::exists(segmentPath)) {
            continue;
        }

        ifstream logFile(segmentPath.string(), ios::in);
        if (!logFile) {
            continue;
        }

        string line;
        while(getline(logFile, line)) {
            auto record = ParseWalRecord(line);
            // Pridedame tik validžius įrašus ir tuos kurių LSN yra didesnis už nurodytą parametruose.
            if (record.lsn != 0 && record.lsn > lsn) {
                records.push_back(record);
            }
        }

        logFile.close();
    }

    return records;
}

/**
 * @brief Patikrina ar yra laukiančių įrašų WAL
 * @return true, jei yra bent vienas įrašas
*/
bool WAL::HasPendingRecords() {
    return !this->ReadAll().empty();
}

/**
 * @brief Išvalo visus WAL segmentus ir perkuria WAL sistemą
 * @return true, jei valymas buvo sėkmingas
*/
bool WAL::ClearAll() {
    // Uždarome dabartinį failą, jeigu jis yra atidarytas.
    if (this->walFile.is_open()) {
        this->walFile.close();
    }

    try {
        // Ištriname visus segmentų failus.
        auto segments = this->GetAllSegments();
        for (const auto &segment : segments) {
            fs::remove(segment);
        }

        // Atstatome pradinius parametrus.
        this->currentSequenceNumber = 0;
        this->currentSegmentNumber = 0;
        this->currentSegmentSize = 0;
        this->currentWalPath = this->GetSegmentPath(currentSegmentNumber);

        return this->OpenWAL();
    } catch (const std::exception &e) {
        return false;
    }
}

/**
 * @brief Išvalo WAL įrašus iki nurodyto LSN ir perkuria WAL su likusiais įrašais
 * @param lsn LSN riba
 * @return true, jei valymas buvo sėkmingas
*/
bool WAL::ClearUpTo(const uint64_t &lsn) {
    auto segments = GetAllSegments();
    vector<WalRecord> recordsToKeep;

    // Surenkame visus įrašus, kurių LSN didesnis už nurodytą
    for (const auto &segmentPath : segments) {
        if (!fs::exists(segmentPath)) {
            continue;
        }

        ifstream logFile(segmentPath.string(), ios::in);
        if (!logFile) {
            continue;
        }

        string line;
        while(getline(logFile, line)) {
            auto record = ParseWalRecord(line);
            if (record.lsn != 0 && record.lsn > lsn) {
                recordsToKeep.push_back(record);
            }
        }

        logFile.close();
    }

    // Uždarome dabartinį failą, jeigu jis yra atidarytas.
    if (this->walFile.is_open()) {
        this->walFile.close();
    }

    // Ištriname visus senus segmentus.
    for (const auto& segment : segments) {
        fs::remove(segment);
    }

    // Atstatome pradinius parametrus.
    this->currentSegmentNumber = 0;
    this->currentSegmentSize = 0;
    this->currentWalPath = this->GetSegmentPath(this->currentSegmentNumber);

    if (!this->OpenWAL()) {
        return false;
    }

    // Perrašome likusius įrašus į naują WAL.
    for (const auto& record : recordsToKeep) {
        if (record.operation == WalOperation::SET) {
            this->walFile << record.lsn << "|SET|" << record.key << "|" << record.value << "\n";
        } else {
            this->walFile << record.lsn << "|DELETE|" << record.key << "\n";
        }
    }

    this->walFile.flush();

    // Atnaujiname segmento dydį.
    if (fs::exists(currentWalPath)) {
        this->currentSegmentSize = fs::file_size(currentWalPath);
    }

    return true;
}

/**
 * @brief Ištrina senus WAL segmentus, kurių numeris mažesnis už nurodytą
 * @param beforeSegment segmento numerio riba
 * @return true, jei trynimas buvo sėkmingas
*/
bool WAL::DeleteOldSegments(const uint64_t &beforeSegment) {
    try {
        auto segments = this->GetAllSegments();

        for (const auto& segmentPath : segments) {
            string filename = segmentPath.filename().string();
            size_t underscorePos = filename.find('_');
            size_t dotPos = filename.find('.');

            // Ištraukiame segmento numerį ir triname, jei jis mažesnis už nurodytą
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
