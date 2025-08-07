#ifndef RECOGNITION_H
#define RECOGNITION_H

#include "../utils/Types.h"
#include "../storage/Storage.h"
#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <memory>

namespace AudioFingerprinting {

class SongRecognizer {
private:
    std::unique_ptr<Database> db;
    static std::mutex dbMutex; // For thread-safe database operations
    
    // Helper methods
    int scoreMatch(const std::vector<MatchOffset>& offsets);
    std::string bestMatch(const std::map<std::string, std::vector<MatchOffset>>& matches);
    SongInfo extractMetadata(const std::string& filename);
    void displayTopMatches(const std::map<std::string, std::vector<MatchOffset>>& matches);
    
public:
    SongRecognizer(const std::string& dbPath = "fingerprints.db");
    ~SongRecognizer();
    
    // Database management
    bool initializeDatabase();
    
    // Song registration
    bool registerSong(const std::string& filename);
    bool registerDirectory(const std::string& path, int numWorkers = 4);
    
    // Song recognition
    SongInfo recognizeSong(const std::string& filename);
    SongInfo recognizeFromHashes(const std::vector<HashResult>& hashes);
    
    // Database statistics
    void printDatabaseStats();
    
    // Utility functions
    static std::vector<std::string> getSupportedFiles(const std::string& directory);
    static bool isSupportedExtension(const std::string& filename);
};

// Thread-safe registration function for multiprocessing
void registerSongThreadSafe(const std::string& filename, Database* database, std::mutex* mutex);

} // namespace AudioFingerprinting

#endif