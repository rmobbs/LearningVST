#pragma once

#include "Types.h"
#include <vector>

template <typename T> class SampleBuffer {
protected:
  ushort numChannels;
  ulong blockSize;
  std::vector<uchar> data;
public:
  SampleBuffer(ushort numChannels, ulong blockSize) {
    this->numChannels = numChannels;
    this->blockSize = blockSize;

    // One T* and blockSize T for each channel
    data.resize((sizeof(T*) + (sizeof(T) * blockSize)) * numChannels);

    // Fixup pointers and zero
    auto p = reinterpret_cast<T**>(data.data());
    auto s = reinterpret_cast<T* >(data.data() + sizeof(T*) * numChannels);
    for (auto i = 0; i < numChannels; ++i) {
      p[i] = s + (i * blockSize);
      memset(p[i], 0, sizeof(T) * blockSize);
    }
  }

  ~SampleBuffer() {
  }

  void zero() {
    auto p = reinterpret_cast<T**>(data.data());
    for (auto i = 0; i < numChannels; ++i) {
      memset(p[i], 0, sizeof(T) * blockSize);
    }
  }

  inline T** getSamples() const {
    return reinterpret_cast<T**>(const_cast<uchar*>(data.data()));
  }

  inline T** getSamples() {
    return reinterpret_cast<T**>(data.data());
  }

  inline ulong getBlockSize() const {
    return blockSize;
  }

  inline ushort getNumChannels() const {
    return numChannels;
  }
};
