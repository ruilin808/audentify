#ifndef TYPES_H
#define TYPES_H

#include <vector>
#include <string>
#include <complex>
#include <sstream>
#include <iomanip>

namespace AudioFingerprinting {

struct SpectrogramResult {
    std::vector<double> frequencies;
    std::vector<double> times;
    std::vector<std::vector<double>> powerMatrix;
    
    SpectrogramResult(const std::vector<double>& f, 
                     const std::vector<double>& t, 
                     const std::vector<std::vector<double>>& Sxx)
        : frequencies(f), times(t), powerMatrix(Sxx) {}
};

struct Peak {
    int freqIdx;
    int timeIdx;
    double frequency;
    double time;
    
    Peak() : freqIdx(0), timeIdx(0), frequency(0.0), time(0.0) {}
    Peak(int freqIdx, int timeIdx, double freq, double time)
        : freqIdx(freqIdx), timeIdx(timeIdx), frequency(freq), time(time) {}
};

struct HashResult {
    long hash;
    double timeOffset;
    std::string songId;
    
    HashResult(long hash, double timeOffset, const std::string& songId)
        : hash(hash), timeOffset(timeOffset), songId(songId) {}
    
    std::string toString() const {
        std::ostringstream oss;
        oss << "Hash: " << hash << ", Time: " << std::fixed << std::setprecision(3) 
            << timeOffset << ", Song: " << songId;
        return oss.str();
    }
};

} // namespace AudioFingerprinting

#endif
