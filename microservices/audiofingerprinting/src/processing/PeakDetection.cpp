#include "PeakDetection.h"
#include <algorithm>
#include <set>
#include <iostream>

namespace AudioFingerprinting {

// Original functions for compatibility
inline bool isLocalMaximum(const std::vector<std::vector<double>>& matrix, int i, int j) {
    double centerValue = matrix[i][j];
    int halfBox = PEAK_BOX_SIZE / 2;
    
    for (int di = -halfBox; di <= halfBox; di++) {
        for (int dj = -halfBox; dj <= halfBox; dj++) {
            if (di == 0 && dj == 0) continue;
            
            int ni = i + di;
            int nj = j + dj;
            
            if (ni >= 0 && ni < static_cast<int>(matrix.size()) && 
                nj >= 0 && nj < static_cast<int>(matrix[0].size())) {
                if (matrix[ni][nj] > centerValue) {
                    return false;
                }
            }
        }
    }
    return true;
}

std::vector<Peak> findPeaksOptimized(const SpectrogramResult& spec) {
    const auto& Sxx = spec.powerMatrix;
    size_t rows = Sxx.size();
    size_t cols = Sxx[0].size();
    
    std::vector<Peak> peaks;
    peaks.reserve(rows * cols / (PEAK_BOX_SIZE * PEAK_BOX_SIZE * 4)); // Pre-allocate
    
    // Calculate adaptive threshold
    double globalSum = 0.0;
    for (const auto& row : Sxx) {
        for (double val : row) {
            globalSum += val;
        }
    }
    double globalMean = globalSum / (rows * cols);
    double threshold = globalMean * 2.0; // Adaptive threshold
    
    // Find peaks with early termination
    int halfBox = PEAK_BOX_SIZE / 2;
    for (int i = halfBox; i < static_cast<int>(rows) - halfBox; i++) {
        for (int j = halfBox; j < static_cast<int>(cols) - halfBox; j++) {
            if (Sxx[i][j] > threshold && isLocalMaximum(Sxx, i, j)) {
                peaks.emplace_back(i, j, spec.frequencies[i], spec.times[j]);
            }
        }
    }
    
    // Sort by power and limit results
    std::sort(peaks.begin(), peaks.end(), 
              [&Sxx](const Peak& a, const Peak& b) {
                  return Sxx[a.freqIdx][a.timeIdx] > Sxx[b.freqIdx][b.timeIdx];
              });
    
    size_t peakTarget = static_cast<size_t>((rows * cols / (PEAK_BOX_SIZE * PEAK_BOX_SIZE)) * POINT_EFFICIENCY);
    if (peaks.size() > peakTarget) {
        peaks.resize(peakTarget);
    }
    
    return peaks;
}

// NEW ENHANCED FUNCTIONS

// Enhanced peak detection with amplitude-based filtering
bool isLocalMaximumEnhanced(const std::vector<std::vector<double>>& matrix, int i, int j, double& peakStrength) {
    double centerValue = matrix[i][j];
    int halfBox = PEAK_BOX_SIZE / 2;
    
    double maxNeighbor = 0.0;
    double sumNeighbors = 0.0;
    int neighborCount = 0;
    
    for (int di = -halfBox; di <= halfBox; di++) {
        for (int dj = -halfBox; dj <= halfBox; dj++) {
            if (di == 0 && dj == 0) continue;
            
            int ni = i + di;
            int nj = j + dj;
            
            if (ni >= 0 && ni < static_cast<int>(matrix.size()) && 
                nj >= 0 && nj < static_cast<int>(matrix[0].size())) {
                
                double neighborValue = matrix[ni][nj];
                
                if (neighborValue > centerValue) {
                    return false; // Not a local maximum
                }
                
                maxNeighbor = std::max(maxNeighbor, neighborValue);
                sumNeighbors += neighborValue;
                neighborCount++;
            }
        }
    }
    
    // Calculate peak strength ratio
    double avgNeighbor = (neighborCount > 0) ? sumNeighbors / neighborCount : 0.0;
    peakStrength = (avgNeighbor > 0) ? centerValue / avgNeighbor : 0.0;
    
    return true;
}

// Frequency filtering helper
bool isValidFrequency(double frequency) {
    return frequency >= MIN_FREQUENCY_HZ && frequency <= MAX_FREQUENCY_HZ;
}

// Temporal peak filtering to reduce redundancy
std::vector<Peak> filterTemporalPeaks(const std::vector<Peak>& rawPeaks) {
    std::vector<Peak> filteredPeaks;
    
    // Sort by time first
    std::vector<Peak> sortedPeaks = rawPeaks;
    std::sort(sortedPeaks.begin(), sortedPeaks.end(), 
              [](const Peak& a, const Peak& b) { return a.time < b.time; });
    
    // Group by time windows and limit peaks per window
    const double timeWindow = 1.0 / MAX_PEAKS_PER_SECOND; // Time window size
    double currentWindowStart = 0.0;
    std::vector<Peak> currentWindowPeaks;
    
    for (const auto& peak : sortedPeaks) {
        if (peak.time >= currentWindowStart + timeWindow) {
            // Process current window
            if (!currentWindowPeaks.empty()) {
                // Sort by amplitude and take top peaks
                std::sort(currentWindowPeaks.begin(), currentWindowPeaks.end(),
                         [](const Peak& a, const Peak& b) { 
                             return a.amplitude > b.amplitude;
                         });
                
                int maxPeaksInWindow = std::min(static_cast<int>(currentWindowPeaks.size()), 
                                               MAX_PEAKS_PER_SECOND);
                for (int i = 0; i < maxPeaksInWindow; i++) {
                    filteredPeaks.push_back(currentWindowPeaks[i]);
                }
            }
            
            // Start new window
            currentWindowStart = peak.time;
            currentWindowPeaks.clear();
        }
        currentWindowPeaks.push_back(peak);
    }
    
    // Process final window
    if (!currentWindowPeaks.empty()) {
        std::sort(currentWindowPeaks.begin(), currentWindowPeaks.end(),
                 [](const Peak& a, const Peak& b) { 
                     return a.amplitude > b.amplitude;
                 });
        
        int maxPeaksInWindow = std::min(static_cast<int>(currentWindowPeaks.size()), 
                                       MAX_PEAKS_PER_SECOND);
        for (int i = 0; i < maxPeaksInWindow; i++) {
            filteredPeaks.push_back(currentWindowPeaks[i]);
        }
    }
    
    return filteredPeaks;
}

std::vector<Peak> findPeaksOptimizedEnhanced(const SpectrogramResult& spec) {
    const auto& Sxx = spec.powerMatrix;
    size_t rows = Sxx.size();
    size_t cols = Sxx[0].size();
    
    std::vector<Peak> peaks;
    peaks.reserve(rows * cols / (PEAK_BOX_SIZE * PEAK_BOX_SIZE * 8)); // Smaller pre-allocation
    
    // Calculate adaptive threshold with frequency weighting
    double globalSum = 0.0;
    int validSamples = 0;
    
    for (size_t i = 0; i < rows; i++) {
        if (!isValidFrequency(spec.frequencies[i])) continue;
        
        for (size_t j = 0; j < cols; j++) {
            globalSum += Sxx[i][j];
            validSamples++;
        }
    }
    
    double globalMean = (validSamples > 0) ? globalSum / validSamples : 0.0;
    double adaptiveThreshold = globalMean * 3.0; // Increased threshold
    
    // Find peaks with enhanced filtering
    int halfBox = PEAK_BOX_SIZE / 2;
    for (int i = halfBox; i < static_cast<int>(rows) - halfBox; i++) {
        // Skip frequencies outside our range
        if (!isValidFrequency(spec.frequencies[i])) continue;
        
        for (int j = halfBox; j < static_cast<int>(cols) - halfBox; j++) {
            if (Sxx[i][j] > adaptiveThreshold) {
                double peakStrength = 0.0;
                
                if (isLocalMaximumEnhanced(Sxx, i, j, peakStrength) && 
                    peakStrength >= MIN_PEAK_AMPLITUDE_RATIO) {
                    
                    Peak peak(i, j, spec.frequencies[i], spec.times[j]);
                    peak.amplitude = Sxx[i][j]; // Store amplitude for filtering
                    peaks.push_back(peak);
                }
            }
        }
    }
    
    // Apply temporal filtering
    std::vector<Peak> temporalFiltered = filterTemporalPeaks(peaks);
    
    // Final selection by power
    std::sort(temporalFiltered.begin(), temporalFiltered.end(), 
              [&Sxx](const Peak& a, const Peak& b) {
                  return Sxx[a.freqIdx][a.timeIdx] > Sxx[b.freqIdx][b.timeIdx];
              });
    
    // Limit final peak count more aggressively
    size_t maxPeaks = std::min(temporalFiltered.size(), 
                              static_cast<size_t>((rows * cols / (PEAK_BOX_SIZE * PEAK_BOX_SIZE)) * POINT_EFFICIENCY));
    
    if (temporalFiltered.size() > maxPeaks) {
        temporalFiltered.resize(maxPeaks);
    }
    
    std::cout << "  Enhanced peak detection: " << peaks.size() 
              << " -> " << temporalFiltered.size() 
              << " (filtered " << (peaks.size() - temporalFiltered.size()) << ")" << std::endl;
    
    return temporalFiltered;
}

} // namespace AudioFingerprinting