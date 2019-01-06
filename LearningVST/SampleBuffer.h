#pragma once

#include "Types.h"
#include <vector>

class SampleBuffer {
protected:
  ushort numChannels;
  ulong blockSize;

  // Let the baby have his way for now, but I think this can be a single float* alloc
  float **samples;
public:
  SampleBuffer(ushort numChannels, ulong blockSize) {
    this->numChannels = numChannels;
    this->blockSize = blockSize;

    samples = new float*[numChannels];

    // Allocate and zero
    for (auto i = 0; i < numChannels; ++i) {
      samples[i] = new float[blockSize]();
    }
  }

  ~SampleBuffer() {
    for (auto i = 0; i < numChannels; ++i) {
      delete[] samples[i];
    }
    delete[] samples;
  }

  void zero() {
    for (auto i = 0; i < numChannels; ++i) {
      memset(samples[i], 0, sizeof(float) * blockSize);
    }
  }

  inline float** getSamples() const {
    return samples;
  }

  inline ulong getBlockSize() const {
    return blockSize;
  }

  inline ushort getNumChannels() const {
    return numChannels;
  }
};