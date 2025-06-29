#ifndef CIRCULAR_BUFFER_H
#define CIRCULAR_BUFFER_H

#include <vector>

namespace AudioFingerprinting {

class CircularBuffer {
private:
    std::vector<double> buffer;
    size_t writePos = 0;
    size_t readPos = 0;
    size_t size;
    
public:
    CircularBuffer(size_t bufferSize);
    void write(const std::vector<double>& data);
    std::vector<double> read(size_t numSamples);
};

} // namespace AudioFingerprinting

#endif
