#include "PeakDetection.h"
#include <algorithm>

namespace AudioFingerprinting {

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

} // namespace AudioFingerprinting
