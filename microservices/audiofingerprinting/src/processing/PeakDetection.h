#ifndef PEAK_DETECTION_H
#define PEAK_DETECTION_H

#include "../utils/Types.h"
#include "../core/Constants.h"

namespace AudioFingerprinting {

bool isLocalMaximum(const std::vector<std::vector<double>>& matrix, int i, int j);
std::vector<Peak> findPeaksOptimized(const SpectrogramResult& spec);

} // namespace AudioFingerprinting

#endif
