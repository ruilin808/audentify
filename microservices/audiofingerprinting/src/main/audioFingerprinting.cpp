#include <iostream>
#include <filesystem>
#include <chrono>
#include <thread>
#include <fftw3.h>
#include <iomanip>
#include <cstdlib>  // for getenv
#include <cstring>  // for strlen

#include "../audio/AudioLoader.h"
#include "../processing/HashGenerator.h"
#include "../storage/Storage.h"
#include "../recognition/Recognition.h"

void printUsage(const std::string& programName) {
    std::cout << "Usage: " << programName << " [command] [options]" << std::endl;
    std::cout << "\nCommands:" << std::endl;
    std::cout << "  register <directory>   - Register all songs in directory" << std::endl;
    std::cout << "  recognize <file>       - Recognize a song from file" << std::endl;
    std::cout << "  stats                  - Show database statistics" << std::endl;
    std::cout << "  fingerprint <file>     - Generate fingerprints (no database)" << std::endl;
    std::cout << "\nOptions:" << std::endl;
    std::cout << "  --workers <num>        - Number of worker threads (default: auto)" << std::endl;
    std::cout << "  --db <path>           - Database path (default: from DB_PATH env or fingerprints.db)" << std::endl;
    std::cout << "  --optimized           - Use optimized fingerprinting algorithm" << std::endl;
}

std::string getDefaultDatabasePath() {
    // First check environment variable
    const char* envDbPath = std::getenv("DB_PATH");
    if (envDbPath && strlen(envDbPath) > 0) {
        return std::string(envDbPath);
    }
    
    // Fallback to default
    return "fingerprints.db";
}

int main(int argc, char* argv[]) {
    try {
        // Initialize FFTW threads
        fftw_init_threads();
        fftw_plan_with_nthreads(std::thread::hardware_concurrency());
        
        if (argc < 2) {
            printUsage(argv[0]);
            return 1;
        }
        
        std::string command = argv[1];
        std::string dbPath = getDefaultDatabasePath();  // Use environment-aware default
        int numWorkers = std::thread::hardware_concurrency();
        bool useOptimized = false;
        
        // Parse options
        for (int i = 2; i < argc; i++) {
            std::string arg = argv[i];
            if (arg == "--workers" && i + 1 < argc) {
                numWorkers = std::stoi(argv[++i]);
            } else if (arg == "--db" && i + 1 < argc) {
                dbPath = argv[++i];
            } else if (arg == "--optimized") {
                useOptimized = true;
            }
        }
        
        std::cout << "Audio Fingerprinting System" << std::endl;
        std::cout << "Using " << numWorkers << " worker threads" << std::endl;
        std::cout << "Database: " << dbPath << std::endl;
        std::cout << "Algorithm: " << (useOptimized ? "Optimized" : "Standard") << std::endl;
        std::cout << std::string(50, '=') << std::endl;
        
        if (command == "register") {
            if (argc < 3) {
                std::cerr << "Error: Please specify a directory to register" << std::endl;
                printUsage(argv[0]);
                return 1;
            }
            
            std::string directory = argv[2];
            if (!std::filesystem::exists(directory)) {
                std::cerr << "Error: Directory does not exist: " << directory << std::endl;
                return 1;
            }
            
            AudioFingerprinting::SongRecognizer recognizer(dbPath);
            if (!recognizer.initializeDatabase()) {
                std::cerr << "Error: Failed to initialize database" << std::endl;
                return 1;
            }
            
            std::cout << "Registering songs from: " << directory << std::endl;
            
            auto startTime = std::chrono::high_resolution_clock::now();
            bool success = recognizer.registerDirectory(directory, numWorkers);
            auto endTime = std::chrono::high_resolution_clock::now();
            
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
            
            if (success) {
                std::cout << "Registration completed successfully in " << duration.count() << " ms" << std::endl;
            } else {
                std::cout << "Registration completed with some errors in " << duration.count() << " ms" << std::endl;
            }
            
            recognizer.printDatabaseStats();
            
        } else if (command == "recognize") {
            if (argc < 3) {
                std::cerr << "Error: Please specify a file to recognize" << std::endl;
                printUsage(argv[0]);
                return 1;
            }
            
            std::string filename = argv[2];
            if (!std::filesystem::exists(filename)) {
                std::cerr << "Error: File does not exist: " << filename << std::endl;
                return 1;
            }
            
            AudioFingerprinting::SongRecognizer recognizer(dbPath);
            if (!recognizer.initializeDatabase()) {
                std::cerr << "Error: Failed to initialize database" << std::endl;
                return 1;
            }
            
            auto startTime = std::chrono::high_resolution_clock::now();
            AudioFingerprinting::SongInfo result = recognizer.recognizeSong(filename);
            auto endTime = std::chrono::high_resolution_clock::now();
            
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
            
            std::cout << std::string(50, '=') << std::endl;
            std::cout << "RECOGNITION RESULT" << std::endl;
            std::cout << std::string(50, '=') << std::endl;
            
            if (!result.songId.empty()) {
                std::cout << "✓ Match found!" << std::endl;
                std::cout << "Artist: " << result.artist << std::endl;
                std::cout << "Album:  " << result.album << std::endl;
                std::cout << "Title:  " << result.title << std::endl;
                std::cout << "Song ID: " << result.songId << std::endl;
            } else {
                std::cout << "✗ No match found in database" << std::endl;
            }
            
            std::cout << "Recognition time: " << duration.count() << " ms" << std::endl;
            
        } else if (command == "stats") {
            AudioFingerprinting::SongRecognizer recognizer(dbPath);
            if (!recognizer.initializeDatabase()) {
                std::cerr << "Error: Failed to initialize database" << std::endl;
                return 1;
            }
            
            recognizer.printDatabaseStats();
            
        } else if (command == "fingerprint") {
            if (argc < 3) {
                std::cerr << "Error: Please specify a file to fingerprint" << std::endl;
                printUsage(argv[0]);
                return 1;
            }
            
            std::string filename = argv[2];
            if (!std::filesystem::exists(filename)) {
                std::cerr << "Error: File does not exist: " << filename << std::endl;
                return 1;
            }
            
            std::cout << "Generating fingerprints for: " << filename << std::endl;
            
            auto startTime = std::chrono::high_resolution_clock::now();
            
            std::vector<AudioFingerprinting::HashResult> hashes;
            if (useOptimized) {
                hashes = AudioFingerprinting::fingerprintFileParallelOptimized(filename);
            } else {
                hashes = AudioFingerprinting::fingerprintFileParallel(filename);
            }
            
            auto endTime = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
            
            std::cout << std::string(50, '=') << std::endl;
            std::cout << "FINGERPRINT RESULT" << std::endl;
            std::cout << std::string(50, '=') << std::endl;
            std::cout << "Generated " << hashes.size() << " hashes in " << duration.count() << " ms" << std::endl;
            std::cout << "Algorithm: " << (useOptimized ? "Optimized" : "Standard") << std::endl;
            
            if (!hashes.empty()) {
                std::cout << "\nSample hashes:" << std::endl;
                for (size_t i = 0; i < std::min(size_t(10), hashes.size()); i++) {
                    std::cout << "  " << hashes[i].toString() << std::endl;
                }
                
                if (useOptimized) {
                    std::cout << "\nOptimization benefits:" << std::endl;
                    std::cout << "  • Frequency range: 300-8000 Hz (ignoring noise)" << std::endl;
                    std::cout << "  • Peak quality filtering: 4x amplitude threshold" << std::endl;
                    std::cout << "  • Temporal limiting: max 15 peaks/second" << std::endl;
                    std::cout << "  • Hash deduplication: no duplicate hashes" << std::endl;
                    std::cout << "  • Expected reduction: ~70% fewer hashes vs standard" << std::endl;
                }
            }
            
        } else {
            std::cerr << "Error: Unknown command: " << command << std::endl;
            printUsage(argv[0]);
            return 1;
        }
        
        // Cleanup FFTW
        fftw_cleanup_threads();
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}