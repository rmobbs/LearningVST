#include "PcmWavFile.h"
#include <iostream>
#include <fstream>
#include "SampleBuffer.h"

bool PcmWavFile::openWrite(const std::string& fileName, uint numChannels, uint sampleRate, AudioBitDepth bitDepth) {
  this->bitDepth = bitDepth;
  this->fileName = fileName;

  // Setup header and write it out. Writing placeholders for file-size-related data,
  // will fixup later
  header.format.numChannels = numChannels;
  header.format.sampleRate = sampleRate;
  header.format.bitsPerSample = static_cast<ushort>(bitDepth);

  header.format.byteRate = header.format.sampleRate *
    (header.format.numChannels * header.format.bitsPerSample / 8);
  header.format.blockAlign = static_cast<ushort>
    (header.format.numChannels * header.format.bitsPerSample / 8);

  ss.write(reinterpret_cast<char *>(&header), sizeof(header));

  return true;
}

bool PcmWavFile::closeWrite() {
  // Seek to header.data.chunkSize and write the actual amount of data
  int offset1 = reinterpret_cast<int>(&header.data.chunkSize) - reinterpret_cast<int>(&header);
  ss.seekp(offset1, std::ios::beg);
  ss.write(reinterpret_cast<char*>(&dataBytesWritten), sizeof(dataBytesWritten));

  int offset2 = reinterpret_cast<int>(&header.riff.chunkSize) - reinterpret_cast<int>(&header);
  ss.seekp(offset2, std::ios::beg);
  uint chunkSize = dataBytesWritten + sizeof(header) - 8;
  ss.write(reinterpret_cast<char *>(&chunkSize), sizeof(chunkSize));

  std::ofstream ofs(this->fileName, std::ios::binary | std::ios::trunc);

  // Of course it is really rough to have this error kill the process at the
  // last step ... but it's also rough to hold onto a write handle the entire
  // time, or to write directly to a file rather than a buffer.
  if (!ofs) {
    std::cerr << "Unable to create WAV file" << std::endl;
  }

  ofs.write(ss.str().c_str(), ss.str().length());
  ofs.flush();
  ofs.close();

  return true;
}

bool PcmWavFile::writeBuffer(const SampleBuffer<float>& sampleBuffer) {
  auto numSamplesToWrite = sampleBuffer.getNumChannels() * sampleBuffer.getBlockSize();

  // Data from the VST SDK is channel sequential (channel 1 bufsize samples, channel 2 bufsize samples, ..., channel N bufsize samples)
  // PCM is encoded as channel-interleaved (channel 1,2,...,N sample 0, channel 1,2,...,N sample 1, ..., channel 1,2,...,N sample bufsize)
  // Here we convert and interleave the data

  auto byteDepth = static_cast<int>(this->bitDepth) / 8;

  // Ensure we have room to store the data
  pcmBuffer.resize(numSamplesToWrite * byteDepth);

  // Maximum value of a PCM sample
  auto pcmSampleMaxValue = pow(2.0,
    static_cast<double>(header.format.bitsPerSample - 1)) - 1.0;

  // Incoming values from VST are [-1.0,1.0]
  switch (this->bitDepth) {
    // 8-bit PCM samples are [0,255] - the only unsigned bit depth
    case AudioBitDepth::Type8: {
      uchar* outBuf = pcmBuffer.data();
      for (ulong s = 0; s < sampleBuffer.getBlockSize(); ++s) {
        for (ushort c = 0; c < sampleBuffer.getNumChannels(); ++c) {
          *outBuf++ = static_cast<uchar>((sampleBuffer.
            getSamples()[c][s] + 1.0f) * pcmSampleMaxValue);
        }
      }
      break;
    }
    // All other bit depths are standard two's complement signed
    case AudioBitDepth::Type16: {
      short* outBuf = reinterpret_cast<short*>(pcmBuffer.data());
      for (ulong s = 0; s < sampleBuffer.getBlockSize(); ++s) {
        for (ushort c = 0; c < sampleBuffer.getNumChannels(); ++c) {
          *outBuf++ = static_cast<short>(sampleBuffer.
            getSamples()[c][s] * pcmSampleMaxValue);
        }
      }
      break;
    }
    case AudioBitDepth::Type24: {
      uchar* outBuf = pcmBuffer.data();
      for (ulong s = 0; s < sampleBuffer.getBlockSize(); ++s) {
        for (ushort c = 0; c < sampleBuffer.getNumChannels(); ++c) {
          int sampleAsInt = static_cast<int>
            (sampleBuffer.getSamples()[c][s] * pcmSampleMaxValue);

          *outBuf++ = static_cast<uchar>((sampleAsInt) & 0xFF);
          *outBuf++ = static_cast<uchar>((sampleAsInt >> 8) & 0xFF);
          *outBuf++ = static_cast<uchar>((sampleAsInt >> 16) & 0xFF);
        }
      }
      break;
    }
    case AudioBitDepth::Type32: {
      int* outBuf = reinterpret_cast<int*>(pcmBuffer.data());
      for (ulong s = 0; s < sampleBuffer.getBlockSize(); ++s) {
        for (ushort c = 0; c < sampleBuffer.getNumChannels(); ++c) {
          *outBuf++ = static_cast<int>
            (sampleBuffer.getSamples()[c][s] * pcmSampleMaxValue);
        }
      }
      break;
    }
  }

  ss.write(reinterpret_cast<char*>(pcmBuffer.data()), numSamplesToWrite * byteDepth);

  this->dataBytesWritten += numSamplesToWrite * byteDepth;

  return true;
}