#include "Constants.h"

namespace AudioFingerprinting {

const int SAMPLE_RATE = 22050;
const double FFT_WINDOW_SIZE = 0.046; // seconds

// OPTIMIZED: More selective peak detection
const int PEAK_BOX_SIZE = 20;  // Increased from 15 for better peak isolation
const double POINT_EFFICIENCY = 0.3; // Reduced from 0.8 - fewer but better peaks

// OPTIMIZED: Smaller, more focused target zone
const double TARGET_T = 0.5; // Reduced from 1.0 seconds
const double TARGET_F = 500.0; // Reduced from 1000.0 Hz
const double TARGET_START = 0.02; // Reduced from 0.05 seconds

// NEW: Hash collision reduction parameters
const int MIN_PEAK_AMPLITUDE_RATIO = 4; // Peak must be 4x stronger than neighbors
const int MAX_PEAKS_PER_SECOND = 15;     // Limit to 15 peaks per second max
const int HASH_BITS = 40;                // Use 40-bit hashes (vs 32-bit)
const double MIN_FREQUENCY_HZ = 300.0;   // Ignore frequencies below 300Hz
const double MAX_FREQUENCY_HZ = 8000.0;  // Ignore frequencies above 8kHz
const int TARGET_ZONE_POINTS = 5;        // Max 5 target points per anchor
const double PEAK_DECAY_THRESHOLD = 0.7; // Temporal peak filtering

} // namespace AudioFingerprinting