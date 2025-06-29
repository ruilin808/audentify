#include "HashGenerator.h"
#include "PeakDetection.h"
#include "../audio/AudioLoader.h"
#include "../audio/AudioProcessor.h"
#include <functional>
#include <sstream>
#include <thread>
#include <future>
#include <iostream>

namespace AudioFingerprinting {

inline long hashPointPair(const Peak& p1, const Peak& p2) {
    // Use bit manipulation for faster hashing
    long f1 = static_cast<long>(p1.frequency * 100);
    long f2 = static_cast<long>(p2.frequency * 100);
    long dt = static_cast<long>((p2.time - p1.time) * 1000);
    
    return (f1 << 20) | (f2 << 10) | (dt & 0x3FF);
}

std::vector<Peak> getTargetZone(const Peak& anchor, const std::vector<Peak>& allPeaks) {
    std::vector<Peak> targetPeaks;
    targetPeaks.reserve(100); // Pre-allocate reasonable size
    
    double xMin = anchor.time + TARGET_START;
    double xMax = xMin + TARGET_T;
    double yMin = anchor.frequency - (TARGET_F * 0.5);
    double yMax = yMin + TARGET_F;
    
    for (const Peak& peak : allPeaks) {
        if (peak.frequency >= yMin && peak.frequency <= yMax &&
            peak.time >= xMin && peak.time <= xMax) {
            targetPeaks.push_back(peak);
        }
    }
    
    return targetPeaks;
}

std::string generateSongId(const std::string& filename) {
    std::hash<std::string> hasher;
    size_t hashValue = hasher(filename);
    
    std::ostringstream oss;
    oss << std::hex << hashValue;
    return oss.str();
}

std::vector<HashResult> hashPoints(const std::vector<Peak>& peaks, const std::string& filename) {
    std::vector<HashResult> hashes;
    hashes.reserve(peaks.size() * 10); // Pre-allocate
    
    std::string songId = generateSongId(filename);
    
    for (const Peak& anchor : peaks) {
        std::vector<Peak> targetPeaks = getTargetZone(anchor, peaks);
        
        for (const Peak& target : targetPeaks) {
            long hash = hashPointPair(anchor, target);
            hashes.emplace_back(hash, anchor.time, songId);
        }
    }
    
    return hashes;
}

std::vector<HashResult> fingerprintFileParallel(const std::string& filename) {
    try {
        std::cout << "Processing (parallel): " << filename << std::endl;
        
        if (!isSupportedFormat(filename)) {
            std::cout << "  Skipping unsupported format" << std::endl;
            return std::vector<HashResult>();
        }
        
        // Load audio
        std::vector<double> audio = loadAudioFile(filename);
        std::cout << "  Loaded audio: " << audio.size() << " samples" << std::endl;
        
        // Use multithreading for large files (>30 seconds)
        if (audio.size() > static_cast<size_t>(SAMPLE_RATE * 30)) {
            const size_t numThreads = std::thread::hardware_concurrency();
            const size_t chunkSize = audio.size() / numThreads;
            
            std::vector<std::future<std::vector<Peak>>> futures;
            std::vector<AudioProcessor> processors(numThreads);
            
            for (size_t i = 0; i < numThreads; i++) {
                size_t start = i * chunkSize;
                size_t end = (i == numThreads - 1) ? audio.size() : start + chunkSize;
                
                std::vector<double> chunk(audio.begin() + start, audio.begin() + end);
                
                futures.push_back(std::async(std::launch::async, [&processors, i, chunk]() {
                    SpectrogramResult spec = processors[i].computeSpectrogramOptimized(chunk);
                    return findPeaksOptimized(spec);
                }));
            }
            
            // Combine results
            std::vector<Peak> allPeaks;
            for (auto& future : futures) {
                auto peaks = future.get();
                allPeaks.insert(allPeaks.end(), peaks.begin(), peaks.end());
            }
            
            std::cout << "  Found peaks (parallel): " << allPeaks.size() << std::endl;
            
            std::vector<HashResult> hashes = hashPoints(allPeaks, filename);
            std::cout << "  Generated hashes: " << hashes.size() << std::endl;
            
            return hashes;
        } else {
            // Single-threaded for smaller files
            AudioProcessor processor;
            SpectrogramResult spec = processor.computeSpectrogramOptimized(audio);
            std::cout << "  Spectrogram: " << spec.frequencies.size() << " x " << spec.times.size() << std::endl;
            
            std::vector<Peak> peaks = findPeaksOptimized(spec);
            std::cout << "  Found peaks: " << peaks.size() << std::endl;
            
            std::vector<HashResult> hashes = hashPoints(peaks, filename);
            std::cout << "  Generated hashes: " << hashes.size() << std::endl;
            
            return hashes;
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Error processing " << filename << ": " << e.what() << std::endl;
        return std::vector<HashResult>();
    }
}

} // namespace AudioFingerprinting
