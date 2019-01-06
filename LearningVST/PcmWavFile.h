#pragma once

#include "Types.h"
#include <string>
#include <vector>
#include <sstream>

enum class AudioBitDepth {
  Type8 = 8,
  Type16 = 16,
  Type24 = 24,
  Type32 = 32,
};

template <typename T> class SampleBuffer;

class PcmWavFile {
public:
#pragma pack(push, 1)
  // Note that in all situations chunkSize means size after the tag and chunkSize members
  struct PcmHeader {
    struct Riff {
      char chunkId[4] = { 'R', 'I', 'F', 'F' };
      uint chunkSize = 0;
      char format[4] = { 'W', 'A', 'V', 'E' };
    } riff;

    struct PcmFormat {
      char chunkId[4] = { 'f', 'm', 't', ' ' };
      uint chunkSize = 16;
      ushort format = 1;
      ushort numChannels = 0;
      uint sampleRate = 0;
      uint byteRate = 0;
      ushort blockAlign = 0;
      ushort bitsPerSample = 0;
    } format;

    struct Data {
      char chunkId[4] = { 'd', 'a', 't', 'a' };
      uint chunkSize = 0;
    } data;

    // Followed by data
  };
#pragma pack(pop)
protected:
  PcmHeader header;
  AudioBitDepth bitDepth;
  std::vector<uchar> pcmBuffer;
  uint dataBytesWritten = 0;
  std::string fileName;
  std::ostringstream ss;

  //std::ofstream *ofs = nullptr;
public:
  bool openWrite(const std::string& fileName, uint numChannels, uint sampleRate, AudioBitDepth bitDepth);
  bool writeBuffer(const SampleBuffer<float>& sampleBuffer);
  bool closeWrite();
};