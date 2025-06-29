#ifndef AUDIO_PROCESSOR_H
#define AUDIO_PROCESSOR_H

#include "../utils/Types.h"
#include "../core/Constants.h"
#include <fftw3.h>
#include <complex>
#include <vector>
#include <cmath>

namespace AudioFingerprinting {

class AudioProcessor {
private:
    std::vector<double> windowBuffer;
    std::vector<double> hammingWindow;
    fftw_complex *fftw_in;
    fftw_complex *fftw_out;
    fftw_plan fftw_plan_forward;
    int fftSize;
    
public:
    AudioProcessor();
    ~AudioProcessor();
    
    std::vector<double> generateHammingWindow(int length);
    std::vector<std::complex<double>> computeFFT(const std::vector<double>& input);
    SpectrogramResult computeSpectrogramOptimized(const std::vector<double>& audio);
};

// Utility functions
std::vector<double> resample(const std::vector<double>& input, int originalSampleRate, int targetSampleRate);
std::vector<double> stereoToMono(const std::vector<double>& stereoData);

} // namespace AudioFingerprinting

#endif
