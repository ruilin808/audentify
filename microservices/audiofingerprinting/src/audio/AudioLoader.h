#ifndef AUDIO_LOADER_H
#define AUDIO_LOADER_H

#include <string>
#include <vector>

namespace AudioFingerprinting {

std::vector<double> loadWavFile(const std::string& filename);
std::vector<double> loadMp3File(const std::string& filename);
std::vector<double> loadFlacFile(const std::string& filename);
std::vector<double> loadAudioFile(const std::string& filename);
bool isSupportedFormat(const std::string& filename);

} // namespace AudioFingerprinting

#endif
