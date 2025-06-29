#include "Storage.h"
#include <iostream>
#include <sstream>
#include <algorithm>
#include <functional>
#include <iomanip>

namespace AudioFingerprinting {

Database::Database(const std::string& dbPath) : dbPath(dbPath), db(nullptr), isOpen(false) {}

Database::~Database() {
    close();
}

bool Database::open() {
    if (isOpen) {
        return true;
    }
    
    int rc = sqlite3_open(dbPath.c_str(), &db);
    if (rc != SQLITE_OK) {
        std::cerr << "Cannot open database: " << sqlite3_errmsg(db) << std::endl;
        return false;
    }
    
    // Set timeout for better concurrency
    sqlite3_busy_timeout(db, 30000);
    
    isOpen = true;
    return setupTables();
}

void Database::close() {
    if (isOpen && db) {
        sqlite3_close(db);
        db = nullptr;
        isOpen = false;
    }
}

bool Database::executeSQL(const std::string& sql) {
    char* errMsg = nullptr;
    int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &errMsg);
    
    if (rc != SQLITE_OK) {
        std::cerr << "SQL error: " << errMsg << std::endl;
        sqlite3_free(errMsg);
        return false;
    }
    return true;
}

bool Database::setupTables() {
    if (!isOpen) {
        std::cerr << "Database not open" << std::endl;
        return false;
    }
    
    // Create hash table
    std::string createHashTable = 
        "CREATE TABLE IF NOT EXISTS hash ("
        "hash INTEGER, "
        "offset REAL, "
        "song_id TEXT"
        ")";
    
    // Create song_info table
    std::string createSongTable = 
        "CREATE TABLE IF NOT EXISTS song_info ("
        "artist TEXT, "
        "album TEXT, "
        "title TEXT, "
        "song_id TEXT PRIMARY KEY"
        ")";
    
    // Create index for faster lookups
    std::string createIndex = 
        "CREATE INDEX IF NOT EXISTS idx_hash ON hash (hash)";
    
    // Enable WAL mode for better concurrency
    std::string enableWAL = "PRAGMA journal_mode=WAL";
    std::string setCheckpoint = "PRAGMA wal_autocheckpoint=300";
    
    return executeSQL(createHashTable) && 
           executeSQL(createSongTable) && 
           executeSQL(createIndex) && 
           executeSQL(enableWAL) && 
           executeSQL(setCheckpoint);
}

bool Database::checkpointDb() {
    return executeSQL("PRAGMA wal_checkpoint(FULL)");
}

std::string Database::generateSongIdFromPath(const std::string& filename) {
    std::hash<std::string> hasher;
    size_t hashValue = hasher(filename);
    
    std::ostringstream oss;
    oss << std::hex << hashValue;
    return oss.str();
}

bool Database::songInDb(const std::string& filename) {
    if (!isOpen) {
        return false;
    }
    
    std::string songId = generateSongIdFromPath(filename);
    
    sqlite3_stmt* stmt;
    const char* sql = "SELECT COUNT(*) FROM song_info WHERE song_id = ?";
    
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to prepare statement: " << sqlite3_errmsg(db) << std::endl;
        return false;
    }
    
    sqlite3_bind_text(stmt, 1, songId.c_str(), -1, SQLITE_STATIC);
    
    rc = sqlite3_step(stmt);
    int count = 0;
    if (rc == SQLITE_ROW) {
        count = sqlite3_column_int(stmt, 0);
    }
    
    sqlite3_finalize(stmt);
    return count > 0;
}

bool Database::storeSong(const std::vector<HashResult>& hashes, const SongInfo& songInfo) {
    if (!isOpen || hashes.empty()) {
        return false;
    }
    
    // Begin transaction
    if (!executeSQL("BEGIN TRANSACTION")) {
        return false;
    }
    
    // Insert hashes
    sqlite3_stmt* hashStmt;
    const char* hashSql = "INSERT INTO hash (hash, offset, song_id) VALUES (?, ?, ?)";
    
    int rc = sqlite3_prepare_v2(db, hashSql, -1, &hashStmt, nullptr);
    if (rc != SQLITE_OK) {
        executeSQL("ROLLBACK");
        return false;
    }
    
    for (const auto& hash : hashes) {
        sqlite3_bind_int64(hashStmt, 1, hash.hash);
        sqlite3_bind_double(hashStmt, 2, hash.timeOffset);
        sqlite3_bind_text(hashStmt, 3, hash.songId.c_str(), -1, SQLITE_STATIC);
        
        rc = sqlite3_step(hashStmt);
        if (rc != SQLITE_DONE) {
            std::cerr << "Failed to insert hash: " << sqlite3_errmsg(db) << std::endl;
            sqlite3_finalize(hashStmt);
            executeSQL("ROLLBACK");
            return false;
        }
        
        sqlite3_reset(hashStmt);
    }
    
    sqlite3_finalize(hashStmt);
    
    // Insert song info
    sqlite3_stmt* infoStmt;
    const char* infoSql = "INSERT OR REPLACE INTO song_info (artist, album, title, song_id) VALUES (?, ?, ?, ?)";
    
    rc = sqlite3_prepare_v2(db, infoSql, -1, &infoStmt, nullptr);
    if (rc != SQLITE_OK) {
        executeSQL("ROLLBACK");
        return false;
    }
    
    std::string artist = songInfo.artist.empty() ? "Unknown" : songInfo.artist;
    std::string album = songInfo.album.empty() ? "Unknown" : songInfo.album;
    std::string title = songInfo.title.empty() ? "Unknown" : songInfo.title;
    
    sqlite3_bind_text(infoStmt, 1, artist.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(infoStmt, 2, album.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(infoStmt, 3, title.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(infoStmt, 4, songInfo.songId.c_str(), -1, SQLITE_STATIC);
    
    rc = sqlite3_step(infoStmt);
    sqlite3_finalize(infoStmt);
    
    if (rc != SQLITE_DONE) {
        executeSQL("ROLLBACK");
        return false;
    }
    
    // Commit transaction
    return executeSQL("COMMIT");
}

std::map<std::string, std::vector<MatchOffset>> Database::getMatches(
    const std::vector<HashResult>& hashes, int threshold) {
    
    std::map<std::string, std::vector<MatchOffset>> resultDict;
    
    if (!isOpen || hashes.empty()) {
        return resultDict;
    }
    
    // Create hash lookup map
    std::map<long, double> hashDict;
    for (const auto& hash : hashes) {
        hashDict[hash.hash] = hash.timeOffset;
    }
    
    // Build IN clause for SQL query
    std::ostringstream inClause;
    inClause << "(";
    for (size_t i = 0; i < hashes.size(); ++i) {
        if (i > 0) inClause << ",";
        inClause << hashes[i].hash;
    }
    inClause << ")";
    
    std::string sql = "SELECT hash, offset, song_id FROM hash WHERE hash IN " + inClause.str();
    
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
    
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to prepare match query: " << sqlite3_errmsg(db) << std::endl;
        return resultDict;
    }
    
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        long hash = sqlite3_column_int64(stmt, 0);
        double dbOffset = sqlite3_column_double(stmt, 1);
        const char* songId = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        
        auto it = hashDict.find(hash);
        if (it != hashDict.end()) {
            resultDict[songId].emplace_back(dbOffset, it->second);
        }
    }
    
    sqlite3_finalize(stmt);
    
    // Filter results by threshold
    auto it = resultDict.begin();
    while (it != resultDict.end()) {
        if (static_cast<int>(it->second.size()) < threshold) {
            it = resultDict.erase(it);
        } else {
            ++it;
        }
    }
    
    return resultDict;
}

SongInfo Database::getInfoForSongId(const std::string& songId) {
    SongInfo info;
    
    if (!isOpen || songId.empty()) {
        return info;
    }
    
    sqlite3_stmt* stmt;
    const char* sql = "SELECT artist, album, title FROM song_info WHERE song_id = ?";
    
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        return info;
    }
    
    sqlite3_bind_text(stmt, 1, songId.c_str(), -1, SQLITE_STATIC);
    
    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        info.artist = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        info.album = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        info.title = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        info.songId = songId;
    }
    
    sqlite3_finalize(stmt);
    return info;
}

int Database::getTotalSongs() {
    if (!isOpen) return 0;
    
    sqlite3_stmt* stmt;
    const char* sql = "SELECT COUNT(*) FROM song_info";
    
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return 0;
    
    int count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = sqlite3_column_int(stmt, 0);
    }
    
    sqlite3_finalize(stmt);
    return count;
}

int Database::getTotalHashes() {
    if (!isOpen) return 0;
    
    sqlite3_stmt* stmt;
    const char* sql = "SELECT COUNT(*) FROM hash";
    
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return 0;
    
    int count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = sqlite3_column_int(stmt, 0);
    }
    
    sqlite3_finalize(stmt);
    return count;
}

} // namespace AudioFingerprinting