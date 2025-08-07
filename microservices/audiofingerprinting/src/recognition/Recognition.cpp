#include "Recognition.h"
#include "../audio/AudioLoader.h"
#include "../processing/HashGenerator.h"
#include <iostream>
#include <filesystem>
#include <algorithm>
#include <thread>
#include <future>
#include <vector>
#include <map>
#include <sstream>
#include <taglib/fileref.h>
#include <taglib/tag.h>
#include <taglib/tstring.h>
#include <taglib/tpropertymap.h>

namespace AudioFingerprinting {

std::mutex SongRecognizer::dbMutex;

SongRecognizer::SongRecognizer(const std::string& dbPath) {
    db = std::make_unique<Database>(dbPath);
}

SongRecognizer::~SongRecognizer() = default;

bool SongRecognizer::initializeDatabase() {
    return db->open();
}

SongInfo SongRecognizer::extractMetadata(const std::string& filename) {
    SongInfo info;
    
    TagLib::FileRef file(filename.c_str());
    
    if (!file.isNull() && file.tag()) {
        TagLib::Tag* tag = file.tag();
        
        // Extract basic metadata
        info.title = tag->title().to8Bit(true);
        info.artist = tag->artist().to8Bit(true);
        info.album = tag->album().to8Bit(true);
        
        // Try to get album artist from extended properties
        TagLib::PropertyMap properties = file.file()->properties();
        
        // Look for album artist in various formats
        if (properties.contains("ALBUMARTIST")) {
            info.artist = properties["ALBUMARTIST"].front().to8Bit(true);
        } else if (properties.contains("ALBUM ARTIST")) {
            info.artist = properties["ALBUM ARTIST"].front().to8Bit(true);
        } else if (properties.contains("TPE2")) { // ID3v2 album artist tag
            info.artist = properties["TPE2"].front().to8Bit(true);
        }
        
        // Clean up empty fields
        if (info.title.empty()) {
            // Extract filename without extension as fallback
            size_t lastSlash = filename.find_last_of("/\\");
            size_t lastDot = filename.find_last_of(".");
            if (lastSlash != std::string::npos && lastDot != std::string::npos && lastDot > lastSlash) {
                info.title = filename.substr(lastSlash + 1, lastDot - lastSlash - 1);
            } else {
                info.title = "Unknown Title";
            }
        }
        
        if (info.artist.empty()) {
            info.artist = "Unknown Artist";
        }
        
        if (info.album.empty()) {
            info.album = "Unknown Album";
        }
        
        std::cout << "Extracted metadata:" << std::endl;
        std::cout << "  Title: " << info.title << std::endl;
        std::cout << "  Artist: " << info.artist << std::endl;
        std::cout << "  Album: " << info.album << std::endl;
        
    } else {
        std::cerr << "Warning: Could not read metadata from " << filename << std::endl;
        
        // Fallback to filename
        size_t lastSlash = filename.find_last_of("/\\");
        if (lastSlash != std::string::npos) {
            info.title = filename.substr(lastSlash + 1);
        } else {
            info.title = filename;
        }
        info.artist = "Unknown Artist";
        info.album = "Unknown Album";
    }
    
    // Generate song ID
    info.songId = db->generateSongIdFromPath(filename);
    
    return info;
}

int SongRecognizer::scoreMatch(const std::vector<MatchOffset>& offsets) {
    if (offsets.empty()) {
        return 0;
    }
    
    // Calculate time deltas
    std::vector<double> deltas;
    deltas.reserve(offsets.size());
    
    for (const auto& offset : offsets) {
        deltas.push_back(offset.dbOffset - offset.sampleOffset);
    }
    
    // Sort deltas to make histogram calculation easier
    std::sort(deltas.begin(), deltas.end());
    
    // Create histogram with 0.5 second bins
    const double binWidth = 0.5;
    std::map<int, int> histogram;
    
    for (double delta : deltas) {
        int bin = static_cast<int>(delta / binWidth);
        histogram[bin]++;
    }
    
    // Find maximum count in histogram
    int maxCount = 0;
    for (const auto& pair : histogram) {
        maxCount = std::max(maxCount, pair.second);
    }
    
    return maxCount;
}

// Helper struct for ranking matches
struct MatchRanking {
    std::string songId;
    SongInfo songInfo;
    int score;
    int matchCount;
    
    MatchRanking(const std::string& id, const SongInfo& info, int s, int count)
        : songId(id), songInfo(info), score(s), matchCount(count) {}
};

std::string SongRecognizer::bestMatch(const std::map<std::string, std::vector<MatchOffset>>& matches) {
    std::string bestSongId;
    int bestScore = 0;
    
    for (const auto& match : matches) {
        const std::string& songId = match.first;
        const std::vector<MatchOffset>& offsets = match.second;
        
        // Skip if we can't possibly beat the best score
        if (static_cast<int>(offsets.size()) < bestScore) {
            continue;
        }
        
        int score = scoreMatch(offsets);
        if (score > bestScore) {
            bestScore = score;
            bestSongId = songId;
        }
    }
    
    return bestSongId;
}

void SongRecognizer::displayTopMatches(const std::map<std::string, std::vector<MatchOffset>>& matches) {
    std::vector<MatchRanking> rankings;
    
    // Calculate scores for all matches
    for (const auto& match : matches) {
        const std::string& songId = match.first;
        const std::vector<MatchOffset>& offsets = match.second;
        
        int score = scoreMatch(offsets);
        int matchCount = static_cast<int>(offsets.size());
        
        // Get song info for display
        SongInfo info = db->getInfoForSongId(songId);
        
        rankings.emplace_back(songId, info, score, matchCount);
    }
    
    // Sort by score (descending), then by match count (descending)
    std::sort(rankings.begin(), rankings.end(), 
              [](const MatchRanking& a, const MatchRanking& b) {
                  if (a.score != b.score) {
                      return a.score > b.score;
                  }
                  return a.matchCount > b.matchCount;
              });
    
    // Display top 10 (or fewer if less than 10)
    std::cout << "Top potential matches:" << std::endl;
    int displayCount = std::min(static_cast<int>(rankings.size()), 10);
    
    for (int i = 0; i < displayCount; ++i) {
        const auto& ranking = rankings[i];
        std::cout << "  " << (i + 1) << ". " 
                  << ranking.songInfo.artist << " - " << ranking.songInfo.title
                  << " (Score: " << ranking.score 
                  << ", Matches: " << ranking.matchCount << ")" << std::endl;
    }
    std::cout << std::endl;
}

bool SongRecognizer::registerSong(const std::string& filename) {
    std::lock_guard<std::mutex> lock(dbMutex);
    
    if (db->songInDb(filename)) {
        std::cout << "Song already registered: " << filename << std::endl;
        return true;
    }
    
    try {
        std::cout << "Registering: " << filename << std::endl;
        
        // Use OPTIMIZED fingerprinting
        std::vector<HashResult> hashes = fingerprintFileParallelOptimized(filename);
        
        if (hashes.empty()) {
            std::cerr << "Failed to generate fingerprints for: " << filename << std::endl;
            return false;
        }
        
        // Extract metadata using TagLib
        SongInfo songInfo = extractMetadata(filename);
        
        // Store in database
        bool success = db->storeSong(hashes, songInfo);
        
        if (success) {
            std::cout << "Successfully registered: " << filename 
                      << " (" << hashes.size() << " hashes)" << std::endl;
            std::cout << "  Title: " << songInfo.title << std::endl;
            std::cout << "  Artist: " << songInfo.artist << std::endl;
            std::cout << "  Album: " << songInfo.album << std::endl;
        } else {
            std::cerr << "Failed to store song in database: " << filename << std::endl;
        }
        
        return success;
        
    } catch (const std::exception& e) {
        std::cerr << "Error registering " << filename << ": " << e.what() << std::endl;
        return false;
    }
}

bool SongRecognizer::registerDirectory(const std::string& path, int numWorkers) {
    std::vector<std::string> supportedFiles = getSupportedFiles(path);
    
    if (supportedFiles.empty()) {
        std::cout << "No supported audio files found in: " << path << std::endl;
        return false;
    }
    
    std::cout << "Found " << supportedFiles.size() << " supported files" << std::endl;
    
    if (numWorkers <= 1 || static_cast<int>(supportedFiles.size()) < numWorkers) {
        // Single-threaded processing
        bool allSuccess = true;
        for (const std::string& file : supportedFiles) {
            if (!registerSong(file)) {
                allSuccess = false;
            }
        }
        return allSuccess;
    } else {
        // Multi-threaded processing
        std::vector<std::future<bool>> futures;
        
        // Divide files among workers
        size_t filesPerWorker = supportedFiles.size() / static_cast<size_t>(numWorkers);
        size_t remainder = supportedFiles.size() % static_cast<size_t>(numWorkers);
        
        size_t startIdx = 0;
        for (int i = 0; i < numWorkers; ++i) {
            size_t endIdx = startIdx + filesPerWorker + (static_cast<size_t>(i) < remainder ? 1 : 0);
            
            std::vector<std::string> workerFiles(
                supportedFiles.begin() + startIdx,
                supportedFiles.begin() + endIdx
            );
            
            futures.push_back(std::async(std::launch::async, [this, workerFiles]() {
                bool success = true;
                for (const std::string& file : workerFiles) {
                    if (!registerSong(file)) {
                        success = false;
                    }
                }
                return success;
            }));
            
            startIdx = endIdx;
        }
        
        // Wait for all workers to complete
        bool allSuccess = true;
        for (auto& future : futures) {
            if (!future.get()) {
                allSuccess = false;
            }
        }
        
        // Checkpoint database for faster future reads
        db->checkpointDb();
        
        return allSuccess;
    }
}

SongInfo SongRecognizer::recognizeSong(const std::string& filename) {
    try {
        std::cout << "Recognizing: " << filename << std::endl;
        
        // Use OPTIMIZED fingerprinting for recognition too
        std::vector<HashResult> hashes = fingerprintFileParallelOptimized(filename);
        
        if (hashes.empty()) {
            std::cerr << "Failed to generate fingerprints for sample" << std::endl;
            return SongInfo();
        }
        
        return recognizeFromHashes(hashes);
        
    } catch (const std::exception& e) {
        std::cerr << "Error recognizing " << filename << ": " << e.what() << std::endl;
        return SongInfo();
    }
}

SongInfo SongRecognizer::recognizeFromHashes(const std::vector<HashResult>& hashes) {
    std::lock_guard<std::mutex> lock(dbMutex);
    
    // Get matches from database
    auto matches = db->getMatches(hashes);
    
    if (matches.empty()) {
        std::cout << "No matches found in database" << std::endl;
        return SongInfo();
    }
    
    std::cout << "Found potential matches in " << matches.size() << " songs" << std::endl;
    
    // Display top matches with rankings
    displayTopMatches(matches);
    
    // Find best match
    std::string bestSongId = bestMatch(matches);
    
    if (bestSongId.empty()) {
        std::cout << "No confident match found" << std::endl;
        return SongInfo();
    }
    
    // Get song information
    SongInfo info = db->getInfoForSongId(bestSongId);
    
    if (!info.songId.empty()) {
        int matchCount = matches[bestSongId].size();
        int score = scoreMatch(matches[bestSongId]);
        
        std::cout << "Match found: " << info.artist << " - " << info.title 
                  << " (Score: " << score << ", Matches: " << matchCount << ")" << std::endl;
    }
    
    return info;
}

void SongRecognizer::printDatabaseStats() {
    std::lock_guard<std::mutex> lock(dbMutex);
    
    int totalSongs = db->getTotalSongs();
    int totalHashes = db->getTotalHashes();
    
    std::cout << "\n=== Database Statistics ===" << std::endl;
    std::cout << "Total songs: " << totalSongs << std::endl;
    std::cout << "Total hashes: " << totalHashes << std::endl;
    
    if (totalSongs > 0) {
        std::cout << "Average hashes per song: " << (totalHashes / totalSongs) << std::endl;
    }
    
    std::cout << "==========================" << std::endl;
}

std::vector<std::string> SongRecognizer::getSupportedFiles(const std::string& directory) {
    std::vector<std::string> supportedFiles;
    
    try {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(directory)) {
            if (entry.is_regular_file() && isSupportedExtension(entry.path().string())) {
                supportedFiles.push_back(entry.path().string());
            }
        }
    } catch (const std::filesystem::filesystem_error& e) {
        std::cerr << "Error accessing directory " << directory << ": " << e.what() << std::endl;
    }
    
    return supportedFiles;
}

bool SongRecognizer::isSupportedExtension(const std::string& filename) {
    std::filesystem::path path(filename);
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
    
    const std::vector<std::string> supportedExtensions = {".mp3", ".wav", ".flac", ".m4a"};
    
    return std::find(supportedExtensions.begin(), supportedExtensions.end(), extension) 
           != supportedExtensions.end();
}

void registerSongThreadSafe(const std::string& filename, Database* database, std::mutex* mutex) {
    std::lock_guard<std::mutex> lock(*mutex);
    
    try {
        if (database->songInDb(filename)) {
            return;
        }
        
        // Use OPTIMIZED fingerprinting
        std::vector<HashResult> hashes = fingerprintFileParallelOptimized(filename);
        
        // Extract metadata using TagLib directly
        SongInfo songInfo;
        TagLib::FileRef file(filename.c_str());
        
        if (!file.isNull() && file.tag()) {
            TagLib::Tag* tag = file.tag();
            songInfo.title = tag->title().to8Bit(true);
            songInfo.artist = tag->artist().to8Bit(true);
            songInfo.album = tag->album().to8Bit(true);
            
            // Fallbacks for empty fields
            if (songInfo.title.empty()) songInfo.title = "Unknown Title";
            if (songInfo.artist.empty()) songInfo.artist = "Unknown Artist";
            if (songInfo.album.empty()) songInfo.album = "Unknown Album";
        } else {
            songInfo.title = "Unknown Title";
            songInfo.artist = "Unknown Artist";
            songInfo.album = "Unknown Album";
        }
        
        songInfo.songId = database->generateSongIdFromPath(filename);
        
        database->storeSong(hashes, songInfo);
        
    } catch (const std::exception& e) {
        std::cerr << "Error in thread-safe registration: " << e.what() << std::endl;
    }
}

} // namespace AudioFingerprinting