#include "Storage.h"
#include <iostream>
#include <sstream>
#include <algorithm>
#include <functional>
#include <iomanip>
#include <chrono>
#include <thread>

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
    
    // Set timeout for better concurrency (30 seconds)
    sqlite3_busy_timeout(db, 30000);
    
    // Enable better concurrency settings
    executeSQL("PRAGMA journal_mode=WAL");
    executeSQL("PRAGMA synchronous=NORMAL");
    executeSQL("PRAGMA cache_size=10000");
    executeSQL("PRAGMA temp_store=MEMORY");
    
    isOpen = true;
    return setupTables();
}

void Database::close() {
    if (isOpen && db) {
        // Checkpoint WAL before closing
        sqlite3_exec(db, "PRAGMA wal_checkpoint(TRUNCATE)", nullptr, nullptr, nullptr);
        sqlite3_close(db);
        db = nullptr;
        isOpen = false;
    }
}

bool Database::executeSQL(const std::string& sql) {
    if (!isOpen || !db) {
        std::cerr << "Database not open" << std::endl;
        return false;
    }
    
    char* errMsg = nullptr;
    int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &errMsg);
    
    if (rc != SQLITE_OK) {
        std::cerr << "SQL error: " << errMsg << " (Code: " << rc << ")" << std::endl;
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
    
    return executeSQL(createHashTable) && 
           executeSQL(createSongTable) && 
           executeSQL(createIndex);
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
        std::cerr << "Database not open or no hashes provided" << std::endl;
        return false;
    }
    
    // Retry mechanism for database locks
    const int maxRetries = 3;
    for (int attempt = 0; attempt < maxRetries; attempt++) {
        if (attempt > 0) {
            std::cout << "  Retrying database operation (attempt " << (attempt + 1) << ")" << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(100 * attempt));
        }
        
        // Begin transaction with immediate lock
        if (!executeSQL("BEGIN IMMEDIATE TRANSACTION")) {
            if (attempt < maxRetries - 1) continue;
            std::cerr << "Failed to begin transaction after " << maxRetries << " attempts" << std::endl;
            return false;
        }
        
        bool success = true;
        
        // Insert song info first
        sqlite3_stmt* infoStmt;
        const char* infoSql = "INSERT OR REPLACE INTO song_info (artist, album, title, song_id) VALUES (?, ?, ?, ?)";
        
        int rc = sqlite3_prepare_v2(db, infoSql, -1, &infoStmt, nullptr);
        if (rc != SQLITE_OK) {
            std::cerr << "Failed to prepare song info statement: " << sqlite3_errmsg(db) << std::endl;
            executeSQL("ROLLBACK");
            if (attempt < maxRetries - 1) continue;
            return false;
        }
        
        std::string artist = songInfo.artist.empty() ? "Unknown" : songInfo.artist;
        std::string album = songInfo.album.empty() ? "Unknown" : songInfo.album;
        std::string title = songInfo.title.empty() ? "Unknown" : songInfo.title;
        
        sqlite3_bind_text(infoStmt, 1, artist.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(infoStmt, 2, album.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(infoStmt, 3, title.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(infoStmt, 4, songInfo.songId.c_str(), -1, SQLITE_TRANSIENT);
        
        rc = sqlite3_step(infoStmt);
        sqlite3_finalize(infoStmt);
        
        if (rc != SQLITE_DONE) {
            std::cerr << "Failed to insert song info: " << sqlite3_errmsg(db) << std::endl;
            executeSQL("ROLLBACK");
            if (attempt < maxRetries - 1) continue;
            return false;
        }
        
        // Insert hashes in batches
        sqlite3_stmt* hashStmt;
        const char* hashSql = "INSERT INTO hash (hash, offset, song_id) VALUES (?, ?, ?)";
        
        rc = sqlite3_prepare_v2(db, hashSql, -1, &hashStmt, nullptr);
        if (rc != SQLITE_OK) {
            std::cerr << "Failed to prepare hash statement: " << sqlite3_errmsg(db) << std::endl;
            executeSQL("ROLLBACK");
            if (attempt < maxRetries - 1) continue;
            return false;
        }
        
        // Process hashes in smaller batches to avoid locks
        const size_t batchSize = 1000;
        for (size_t i = 0; i < hashes.size(); i += batchSize) {
            size_t endIdx = std::min(i + batchSize, hashes.size());
            
            for (size_t j = i; j < endIdx; j++) {
                const auto& hash = hashes[j];
                
                sqlite3_bind_int64(hashStmt, 1, hash.hash);
                sqlite3_bind_double(hashStmt, 2, hash.timeOffset);
                sqlite3_bind_text(hashStmt, 3, hash.songId.c_str(), -1, SQLITE_TRANSIENT);
                
                rc = sqlite3_step(hashStmt);
                if (rc != SQLITE_DONE) {
                    std::cerr << "Failed to insert hash: " << sqlite3_errmsg(db) << " (Code: " << rc << ")" << std::endl;
                    success = false;
                    break;
                }
                
                sqlite3_reset(hashStmt);
            }
            
            if (!success) break;
            
            // Periodic progress update for large batches
            if (endIdx % 5000 == 0) {
                std::cout << "  Inserted " << endIdx << "/" << hashes.size() << " hashes..." << std::endl;
            }
        }
        
        sqlite3_finalize(hashStmt);
        
        if (success) {
            // Commit transaction
            if (executeSQL("COMMIT")) {
                std::cout << "  Successfully stored " << hashes.size() << " hashes" << std::endl;
                return true;
            } else {
                std::cerr << "Failed to commit transaction: " << sqlite3_errmsg(db) << std::endl;
            }
        }
        
        // Rollback on failure
        executeSQL("ROLLBACK");
        
        if (attempt < maxRetries - 1) {
            std::cout << "  Database operation failed, retrying..." << std::endl;
            continue;
        }
    }
    
    std::cerr << "Failed to store song after " << maxRetries << " attempts" << std::endl;
    return false;
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