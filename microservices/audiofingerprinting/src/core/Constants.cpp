#include "Constants.h"

namespace AudioFingerprinting {

const int SAMPLE_RATE = 22050;
const double FFT_WINDOW_SIZE = 0.046; // seconds
const int PEAK_BOX_SIZE = 15;
const double POINT_EFFICIENCY = 0.8;
const double TARGET_T = 1.0; // seconds
const double TARGET_F = 1000.0; // Hz
const double TARGET_START = 0.05; // seconds

} // namespace AudioFingerprinting
