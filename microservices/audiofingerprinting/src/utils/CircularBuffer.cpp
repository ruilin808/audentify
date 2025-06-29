#include "CircularBuffer.h"

namespace AudioFingerprinting {

CircularBuffer::CircularBuffer(size_t bufferSize) : buffer(bufferSize), size(bufferSize) {}

void CircularBuffer::write(const std::vector<double>& data) {
    for (double sample : data) {
        buffer[writePos] = sample;
        writePos = (writePos + 1) % size;
    }
}

std::vector<double> CircularBuffer::read(size_t numSamples) {
    std::vector<double> result(numSamples);
    for (size_t i = 0; i < numSamples; i++) {
        result[i] = buffer[readPos];
        readPos = (readPos + 1) % size;
    }
    return result;
}

} // namespace AudioFingerprinting
