#ifndef HASH_GENERATOR_H
#define HASH_GENERATOR_H

#include "../utils/Types.h"
#include "../core/Constants.h"
#include "PeakDetection.h"
#include "../audio/AudioLoader.h"
#include "../audio/AudioProcessor.h"
#include <string>
#include <vector>
#include <functional>
#include <sstream>
#include <thread>
#include <future>
#include <iostream>
#include <fftw3.h>
#include <unordered_set>

namespace AudioFingerprinting {

// Enhanced hash functions
uint64_t hashPointPairEnhanced(const Peak& p1, const Peak& p2);
std::vector<Peak> getTargetZoneOptimized(const Peak& anchor, const std::vector<Peak>& allPeaks);
std::vector<HashResult> hashPointsOptimized(const std::vector<Peak>& peaks, const std::string& filename);
std::vector<HashResult> fingerprintFileParallelOptimized(const std::string& filename);

// Keep original functions for compatibility
long hashPointPair(const Peak& p1, const Peak& p2);
std::vector<Peak> getTargetZone(const Peak& anchor, const std::vector<Peak>& allPeaks);
std::string generateSongId(const std::string& filename);
std::vector<HashResult> hashPoints(const std::vector<Peak>& peaks, const std::string& filename);
std::vector<HashResult> fingerprintFileParallel(const std::string& filename);

} // namespace AudioFingerprinting

#endif