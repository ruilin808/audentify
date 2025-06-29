#include "AudioProcessor.h"
#include <algorithm>
#include <iostream>

namespace AudioFingerprinting {

AudioProcessor::AudioProcessor() {
    fftSize = static_cast<int>(SAMPLE_RATE * FFT_WINDOW_SIZE);
    
    // Pre-allocate buffers
    windowBuffer.resize(fftSize);
    hammingWindow = generateHammingWindow(fftSize);
    
    // Initialize FFTW
    fftw_in = (fftw_complex*) fftw_malloc(sizeof(fftw_complex) * fftSize);
    fftw_out = (fftw_complex*) fftw_malloc(sizeof(fftw_complex) * fftSize);
    fftw_plan_forward = fftw_plan_dft_1d(fftSize, fftw_in, fftw_out, FFTW_FORWARD, FFTW_MEASURE);
}

AudioProcessor::~AudioProcessor() {
    fftw_destroy_plan(fftw_plan_forward);
    fftw_free(fftw_in);
    fftw_free(fftw_out);
}

std::vector<double> AudioProcessor::generateHammingWindow(int length) {
    std::vector<double> window(length);
    const double factor = 2.0 * M_PI / (length - 1);
    for (int i = 0; i < length; i++) {
        window[i] = 0.54 - 0.46 * std::cos(factor * i);
    }
    return window;
}

std::vector<std::complex<double>> AudioProcessor::computeFFT(const std::vector<double>& input) {
    // Copy input to FFTW buffer with windowing
    for (size_t i = 0; i < static_cast<size_t>(fftSize) && i < input.size(); i++) {
        fftw_in[i][0] = input[i] * hammingWindow[i];  // Real part with window
        fftw_in[i][1] = 0.0;                          // Imaginary part
    }
    
    // Pad with zeros if input is smaller
    for (size_t i = input.size(); i < static_cast<size_t>(fftSize); i++) {
        fftw_in[i][0] = 0.0;
        fftw_in[i][1] = 0.0;
    }
    
    // Execute FFT
    fftw_execute(fftw_plan_forward);
    
    // Convert to std::complex
    std::vector<std::complex<double>> result(fftSize / 2 + 1);
    for (size_t i = 0; i < result.size(); i++) {
        result[i] = std::complex<double>(fftw_out[i][0], fftw_out[i][1]);
    }
    
    return result;
}

SpectrogramResult AudioProcessor::computeSpectrogramOptimized(const std::vector<double>& audio) {
    int nperseg = fftSize;
    int noverlap = nperseg / 2;
    int step = nperseg - noverlap;
    
    // Calculate number of time segments
    int numSegments = (audio.size() - noverlap) / step;
    int freqBins = nperseg / 2 + 1;
    
    std::vector<std::vector<double>> spectrogram(freqBins, std::vector<double>(numSegments));
    std::vector<double> frequencies(freqBins);
    std::vector<double> times(numSegments);
    
    // Generate frequency array
    const double freqStep = static_cast<double>(SAMPLE_RATE) / nperseg;
    for (int i = 0; i < freqBins; i++) {
        frequencies[i] = i * freqStep;
    }
    
    // Generate time array
    const double timeStep = static_cast<double>(step) / SAMPLE_RATE;
    for (int i = 0; i < numSegments; i++) {
        times[i] = i * timeStep;
    }
    
    // Process each segment
    for (int seg = 0; seg < numSegments; seg++) {
        int start = seg * step;
        
        // Extract segment
        std::vector<double> segment(nperseg, 0.0);
        int copySize = std::min(nperseg, static_cast<int>(audio.size() - start));
        std::copy(audio.begin() + start, audio.begin() + start + copySize, segment.begin());
        
        // Compute FFT
        auto fft = computeFFT(segment);
        
        // Calculate power spectrum
        for (int i = 0; i < freqBins; i++) {
            double magnitude = std::abs(fft[i]);
            spectrogram[i][seg] = magnitude * magnitude;
        }
    }
    
    return SpectrogramResult(frequencies, times, spectrogram);
}

std::vector<double> resample(const std::vector<double>& input, int originalSampleRate, int targetSampleRate) {
    if (originalSampleRate == targetSampleRate) {
        return input;
    }
    
    double ratio = static_cast<double>(originalSampleRate) / targetSampleRate;
    size_t outputSize = static_cast<size_t>(input.size() / ratio);
    std::vector<double> output;
    output.reserve(outputSize);
    
    for (size_t i = 0; i < outputSize; i++) {
        double sourceIndex = static_cast<double>(i) * ratio;
        size_t index = static_cast<size_t>(sourceIndex);
        
        if (index < input.size() - 1) {
            double fraction = sourceIndex - static_cast<double>(index);
            output.push_back(input[index] * (1.0 - fraction) + input[index + 1] * fraction);
        } else if (index < input.size()) {
            output.push_back(input[index]);
        }
    }
    
    return output;
}

std::vector<double> stereoToMono(const std::vector<double>& stereoData) {
    std::vector<double> monoData;
    monoData.reserve(stereoData.size() / 2);
    
    for (size_t i = 0; i < stereoData.size(); i += 2) {
        monoData.push_back((stereoData[i] + stereoData[i + 1]) * 0.5);
    }
    
    return monoData;
}

} // namespace AudioFingerprinting