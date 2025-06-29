#ifndef STORAGE_H
#define STORAGE_H

#include "../utils/Types.h"
#include <string>
#include <vector>
#include <map>
#include <sqlite3.h>
#include <memory>

namespace AudioFingerprinting {

struct SongInfo {
    std::string artist;
    std::string album;
    std::string title;
    std::string songId;
    
    SongInfo() = default;
    SongInfo(const std::string& artist, const std::string& album, 
             const std::string& title, const std::string& songId)
        : artist(artist), album(album), title(title), songId(songId) {}
};

struct MatchOffset {
    double dbOffset;
    double sampleOffset;
    
    MatchOffset(double dbOffset, double sampleOffset)
        : dbOffset(dbOffset), sampleOffset(sampleOffset) {}
};

class Database {
private:
    std::string dbPath;
    sqlite3* db;
    bool isOpen;
    
    // Helper methods
    bool executeSQL(const std::string& sql);

public:
    // Make this public so Recognition can use it
    std::string generateSongIdFromPath(const std::string& filename);
    
public:
    Database(const std::string& dbPath = "fingerprints.db");
    ~Database();
    
    // Database management
    bool open();
    void close();
    bool setupTables();
    bool checkpointDb();
    
    // Song operations
    bool songInDb(const std::string& filename);
    bool storeSong(const std::vector<HashResult>& hashes, const SongInfo& songInfo);
    SongInfo getInfoForSongId(const std::string& songId);
    
    // Matching operations
    std::map<std::string, std::vector<MatchOffset>> getMatches(
        const std::vector<HashResult>& hashes, int threshold = 5);
        
    // Statistics
    int getTotalSongs();
    int getTotalHashes();
};

} // namespace AudioFingerprinting

#endif