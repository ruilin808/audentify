#ifndef PEAK_DETECTION_H
#define PEAK_DETECTION_H

#include "../utils/Types.h"
#include "../core/Constants.h"

namespace AudioFingerprinting {

// Enhanced peak detection functions
bool isLocalMaximumEnhanced(const std::vector<std::vector<double>>& matrix, int i, int j, double& peakStrength);
bool isValidFrequency(double frequency);
std::vector<Peak> filterTemporalPeaks(const std::vector<Peak>& rawPeaks);
std::vector<Peak> findPeaksOptimizedEnhanced(const SpectrogramResult& spec);

// Keep original function for compatibility
bool isLocalMaximum(const std::vector<std::vector<double>>& matrix, int i, int j);
std::vector<Peak> findPeaksOptimized(const SpectrogramResult& spec);

} // namespace AudioFingerprinting

#endif