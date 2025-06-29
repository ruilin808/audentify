#include "AudioLoader.h"
#include "AudioProcessor.h"
#include "../core/Constants.h"
#include <filesystem>
#include <algorithm>
#include <iostream>
#include <stdexcept>

#define DR_WAV_IMPLEMENTATION
#define DR_MP3_IMPLEMENTATION  
#define DR_FLAC_IMPLEMENTATION
#include "../../lib/dr_libs/dr_wav.h"
#include "../../lib/dr_libs/dr_mp3.h"
#include "../../lib/dr_libs/dr_flac.h"

namespace AudioFingerprinting {

std::vector<double> loadWavFile(const std::string& filename) {
    unsigned int channels;
    unsigned int sampleRate;
    drwav_uint64 totalPCMFrameCount;
    
    float* pSampleData = drwav_open_file_and_read_pcm_frames_f32(
        filename.c_str(), &channels, &sampleRate, &totalPCMFrameCount, nullptr);
    
    if (pSampleData == nullptr) {
        throw std::runtime_error("Failed to load WAV file: " + filename);
    }
    
    std::cout << "  WAV info: " << channels << " channels, " 
              << sampleRate << " Hz, " << totalPCMFrameCount << " frames" << std::endl;
    
    // Convert to double vector
    size_t totalSamples = totalPCMFrameCount * channels;
    std::vector<double> audioData;
    audioData.reserve(totalSamples);
    
    for (size_t i = 0; i < totalSamples; i++) {
        audioData.push_back(static_cast<double>(pSampleData[i]));
    }
    
    drwav_free(pSampleData, nullptr);
    
    // Convert stereo to mono if necessary
    if (channels == 2) {
        std::cout << "  Converting stereo to mono" << std::endl;
        audioData = stereoToMono(audioData);
    }
    
    // Resample if necessary
    if (static_cast<int>(sampleRate) != SAMPLE_RATE) {
        std::cout << "  Resampling from " << sampleRate << " Hz to " << SAMPLE_RATE << " Hz" << std::endl;
        audioData = resample(audioData, static_cast<int>(sampleRate), SAMPLE_RATE);
    }
    
    return audioData;
}

std::vector<double> loadMp3File(const std::string& filename) {
    drmp3_config config;
    drmp3_uint64 totalPCMFrameCount;
    
    float* pSampleData = drmp3_open_file_and_read_pcm_frames_f32(
        filename.c_str(), &config, &totalPCMFrameCount, nullptr);
    
    if (pSampleData == nullptr) {
        throw std::runtime_error("Failed to load MP3 file: " + filename);
    }
    
    std::cout << "  MP3 info: " << config.channels << " channels, " 
              << config.sampleRate << " Hz, " << totalPCMFrameCount << " frames" << std::endl;
    
    size_t totalSamples = totalPCMFrameCount * config.channels;
    std::vector<double> audioData;
    audioData.reserve(totalSamples);
    
    for (size_t i = 0; i < totalSamples; i++) {
        audioData.push_back(static_cast<double>(pSampleData[i]));
    }
    
    drmp3_free(pSampleData, nullptr);
    
    if (config.channels == 2) {
        std::cout << "  Converting stereo to mono" << std::endl;
        audioData = stereoToMono(audioData);
    }
    
    if (static_cast<int>(config.sampleRate) != SAMPLE_RATE) {
        std::cout << "  Resampling from " << config.sampleRate << " Hz to " << SAMPLE_RATE << " Hz" << std::endl;
        audioData = resample(audioData, static_cast<int>(config.sampleRate), SAMPLE_RATE);
    }
    
    return audioData;
}

std::vector<double> loadFlacFile(const std::string& filename) {
    unsigned int channels;
    unsigned int sampleRate;
    drflac_uint64 totalPCMFrameCount;
    
    float* pSampleData = drflac_open_file_and_read_pcm_frames_f32(
        filename.c_str(), &channels, &sampleRate, &totalPCMFrameCount, nullptr);
    
    if (pSampleData == nullptr) {
        throw std::runtime_error("Failed to load FLAC file: " + filename);
    }
    
    std::cout << "  FLAC info: " << channels << " channels, " 
              << sampleRate << " Hz, " << totalPCMFrameCount << " frames" << std::endl;
    
    size_t totalSamples = totalPCMFrameCount * channels;
    std::vector<double> audioData;
    audioData.reserve(totalSamples);
    
    for (size_t i = 0; i < totalSamples; i++) {
        audioData.push_back(static_cast<double>(pSampleData[i]));
    }
    
    drflac_free(pSampleData, nullptr);
    
    if (channels == 2) {
        std::cout << "  Converting stereo to mono" << std::endl;
        audioData = stereoToMono(audioData);
    }
    
    if (static_cast<int>(sampleRate) != SAMPLE_RATE) {
        std::cout << "  Resampling from " << sampleRate << " Hz to " << SAMPLE_RATE << " Hz" << std::endl;
        audioData = resample(audioData, static_cast<int>(sampleRate), SAMPLE_RATE);
    }
    
    return audioData;
}

std::vector<double> loadAudioFile(const std::string& filename) {
    std::string extension = std::filesystem::path(filename).extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
    
    try {
        if (extension == ".wav") {
            return loadWavFile(filename);
        } else if (extension == ".mp3") {
            return loadMp3File(filename);
        } else if (extension == ".flac") {
            return loadFlacFile(filename);
        } else {
            throw std::runtime_error("Unsupported audio format: " + extension);
        }
    } catch (const std::exception& e) {
        throw std::runtime_error("Error loading " + filename + ": " + e.what());
    }
}

bool isSupportedFormat(const std::string& filename) {
    std::string extension = std::filesystem::path(filename).extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
    return extension == ".wav" || extension == ".mp3" || extension == ".flac";
}

} // namespace AudioFingerprinting