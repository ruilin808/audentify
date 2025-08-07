#ifndef CONSTANTS_H
#define CONSTANTS_H

namespace AudioFingerprinting {

// Settings
extern const int SAMPLE_RATE;
extern const double FFT_WINDOW_SIZE;
extern const int PEAK_BOX_SIZE;
extern const double POINT_EFFICIENCY;
extern const double TARGET_T;
extern const double TARGET_F;
extern const double TARGET_START;

// NEW: Optimized fingerprinting parameters
extern const int MIN_PEAK_AMPLITUDE_RATIO;   // Minimum peak strength ratio
extern const int MAX_PEAKS_PER_SECOND;       // Limit peaks per time window
extern const int HASH_BITS;                  // Bit size for hash (reduce collisions)
extern const double MIN_FREQUENCY_HZ;        // Ignore low frequencies
extern const double MAX_FREQUENCY_HZ;        // Ignore high frequencies
extern const int TARGET_ZONE_POINTS;         // Limit target zone size
extern const double PEAK_DECAY_THRESHOLD;    // Peak temporal filtering

} // namespace AudioFingerprinting

#endif