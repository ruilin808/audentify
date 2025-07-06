#include "HashGenerator.h"
#include "PeakDetection.h"
#include "../audio/AudioLoader.h"
#include "../audio/AudioProcessor.h"
#include <functional>
#include <sstream>
#include <thread>
#include <future>
#include <iostream>
#include <unordered_set>

namespace AudioFingerprinting {

// ORIGINAL FUNCTIONS FOR COMPATIBILITY

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

// NEW ENHANCED FUNCTIONS

// Enhanced hash function with reduced collision probability
inline uint64_t hashPointPairEnhanced(const Peak& p1, const Peak& p2) {
    // Use higher precision and more bits to reduce collisions
    uint64_t f1 = static_cast<uint64_t>(p1.frequency * 10) & 0x3FFF;  // 14 bits
    uint64_t f2 = static_cast<uint64_t>(p2.frequency * 10) & 0x3FFF;  // 14 bits
    uint64_t dt = static_cast<uint64_t>((p2.time - p1.time) * 10000) & 0xFFF; // 12 bits
    
    // Create 40-bit hash with better distribution
    uint64_t hash = (f1 << 26) | (f2 << 12) | dt;
    
    // Add amplitude information for additional uniqueness (optional)
    // uint64_t amp_ratio = static_cast<uint64_t>((p2.amplitude / p1.amplitude) * 100) & 0xFF;
    // hash = (hash << 8) | amp_ratio; // Would make it 48-bit
    
    return hash & 0xFFFFFFFFFF; // Ensure 40-bit result
}

// Optimized target zone with limited points
std::vector<Peak> getTargetZoneOptimized(const Peak& anchor, const std::vector<Peak>& allPeaks) {
    std::vector<Peak> targetPeaks;
    targetPeaks.reserve(TARGET_ZONE_POINTS * 2); // Pre-allocate conservatively
    
    double xMin = anchor.time + TARGET_START;
    double xMax = xMin + TARGET_T;
    double yMin = anchor.frequency - (TARGET_F * 0.5);
    double yMax = yMin + TARGET_F;
    
    // Collect candidate peaks
    for (const Peak& peak : allPeaks) {
        if (peak.frequency >= yMin && peak.frequency <= yMax &&
            peak.time >= xMin && peak.time <= xMax) {
            targetPeaks.push_back(peak);
        }
    }
    
    // Limit target zone size and select best peaks
    if (targetPeaks.size() > TARGET_ZONE_POINTS) {
        // Sort by amplitude (strongest peaks first)
        std::sort(targetPeaks.begin(), targetPeaks.end(),
                 [](const Peak& a, const Peak& b) {
                     return a.amplitude > b.amplitude;
                 });
        targetPeaks.resize(TARGET_ZONE_POINTS);
    }
    
    return targetPeaks;
}

// Enhanced hash generation with deduplication
std::vector<HashResult> hashPointsOptimized(const std::vector<Peak>& peaks, const std::string& filename) {
    std::vector<HashResult> hashes;
    std::unordered_set<uint64_t> seenHashes; // Prevent duplicate hashes
    
    std::string songId = generateSongId(filename);
    
    // Limit anchor points for efficiency
    size_t maxAnchors = std::min(peaks.size(), static_cast<size_t>(peaks.size() * 0.8));
    
    for (size_t i = 0; i < maxAnchors; i++) {
        const Peak& anchor = peaks[i];
        std::vector<Peak> targetPeaks = getTargetZoneOptimized(anchor, peaks);
        
        for (const Peak& target : targetPeaks) {
            uint64_t hash = hashPointPairEnhanced(anchor, target);
            
            // Skip if we've seen this hash before (deduplication)
            if (seenHashes.find(hash) == seenHashes.end()) {
                seenHashes.insert(hash);
                hashes.emplace_back(static_cast<long>(hash), anchor.time, songId);
            }
        }
    }
    
    std::cout << "  Generated " << hashes.size() 
              << " unique hashes from " << peaks.size() 
              << " peaks (deduped " << (seenHashes.size() - hashes.size()) << ")" << std::endl;
    
    return hashes;
}

// Enhanced fingerprinting with quality control and parallel processing
std::vector<HashResult> fingerprintFileParallelOptimized(const std::string& filename) {
    try {
        std::cout << "Processing (optimized): " << filename << std::endl;
        
        if (!isSupportedFormat(filename)) {
            std::cout << "  Skipping unsupported format" << std::endl;
            return std::vector<HashResult>();
        }
        
        // Load audio
        std::vector<double> audio = loadAudioFile(filename);
        std::cout << "  Loaded audio: " << audio.size() << " samples" << std::endl;
        
        // Skip very short files (less than 10 seconds)
        if (audio.size() < static_cast<size_t>(SAMPLE_RATE * 10)) {
            std::cout << "  Skipping short file (< 10 seconds)" << std::endl;
            return std::vector<HashResult>();
        }
        
        // Use multithreading for large files (>60 seconds) with optimized algorithm
        if (audio.size() > static_cast<size_t>(SAMPLE_RATE * 60)) {
            const size_t numThreads = std::min(static_cast<size_t>(4), static_cast<size_t>(std::thread::hardware_concurrency()));
            const size_t chunkSize = audio.size() / numThreads;
            const size_t overlapSize = static_cast<size_t>(SAMPLE_RATE * 2); // 2 second overlap
            
            std::vector<std::future<std::vector<Peak>>> futures;
            std::vector<AudioProcessor> processors(numThreads);
            
            for (size_t i = 0; i < numThreads; i++) {
                size_t start = (i == 0) ? 0 : (i * chunkSize - overlapSize);
                size_t end = (i == numThreads - 1) ? audio.size() : ((i + 1) * chunkSize + overlapSize);
                end = std::min(end, audio.size());
                
                std::vector<double> chunk(audio.begin() + start, audio.begin() + end);
                double timeOffset = static_cast<double>(start) / SAMPLE_RATE;
                
                futures.push_back(std::async(std::launch::async, [&processors, i, chunk, timeOffset]() {
                    SpectrogramResult spec = processors[i].computeSpectrogramOptimized(chunk);
                    
                    // Adjust time values for chunk offset
                    SpectrogramResult adjustedSpec = spec;
                    for (auto& time : adjustedSpec.times) {
                        time += timeOffset;
                    }
                    
                    return findPeaksOptimizedEnhanced(adjustedSpec);
                }));
            }
            
            // Combine results and remove overlapping peaks
            std::vector<Peak> allPeaks;
            for (auto& future : futures) {
                auto peaks = future.get();
                allPeaks.insert(allPeaks.end(), peaks.begin(), peaks.end());
            }
            
            // Remove duplicate/overlapping peaks from chunk boundaries
            std::sort(allPeaks.begin(), allPeaks.end(), 
                     [](const Peak& a, const Peak& b) { 
                         return a.time < b.time; 
                     });
            
            // Remove peaks that are too close in time and frequency
            std::vector<Peak> dedupedPeaks;
            const double timeThreshold = 0.1; // 100ms
            const double freqThreshold = 50.0; // 50Hz
            
            for (const auto& peak : allPeaks) {
                bool isDuplicate = false;
                for (const auto& existing : dedupedPeaks) {
                    if (std::abs(peak.time - existing.time) < timeThreshold &&
                        std::abs(peak.frequency - existing.frequency) < freqThreshold) {
                        isDuplicate = true;
                        break;
                    }
                }
                if (!isDuplicate) {
                    dedupedPeaks.push_back(peak);
                }
            }
            
            std::cout << "  Found peaks (parallel): " << allPeaks.size() 
                      << " -> " << dedupedPeaks.size() << " (after dedup)" << std::endl;
            
            // Quality check
            if (dedupedPeaks.size() < 100) {
                std::cout << "  Warning: Too few peaks detected (" << dedupedPeaks.size() 
                          << "), file may be problematic" << std::endl;
            }
            
            // Generate optimized hashes
            std::vector<HashResult> hashes = hashPointsOptimized(dedupedPeaks, filename);
            std::cout << "  Generated hashes: " << hashes.size() << std::endl;
            
            return hashes;
            
        } else {
            // Single-threaded processing for smaller files
            AudioProcessor processor;
            SpectrogramResult spec = processor.computeSpectrogramOptimized(audio);
            std::cout << "  Spectrogram: " << spec.frequencies.size() << " x " << spec.times.size() << std::endl;
            
            // Use enhanced peak detection
            std::vector<Peak> peaks = findPeaksOptimizedEnhanced(spec);
            std::cout << "  Found peaks: " << peaks.size() << std::endl;
            
            // Quality check - ensure minimum number of peaks
            if (peaks.size() < 50) {
                std::cout << "  Warning: Too few peaks detected (" << peaks.size() 
                          << "), file may be problematic" << std::endl;
            }
            
            // Generate optimized hashes
            std::vector<HashResult> hashes = hashPointsOptimized(peaks, filename);
            std::cout << "  Generated hashes: " << hashes.size() << std::endl;
            
            return hashes;
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Error processing " << filename << ": " << e.what() << std::endl;
        return std::vector<HashResult>();
    }
}

} // namespace AudioFingerprinting