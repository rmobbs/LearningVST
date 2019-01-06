#pragma once

#include "Types.h"

// Singleton. Use GlobalSettings::get() to get the instance
class GlobalSettings {
  static constexpr ulong kDefaultBlockSize = 512;
  static constexpr ushort kDefaultNumChannels = 2;
  static constexpr double kDefaultSampleRate = 44100.0;
  static constexpr double kDefaultTempo = 120.0;
  static constexpr ushort kDefaultBeatsPerMeasure = 4;
  static constexpr ushort kDefaultNoteValue = 4;

protected:
  ulong blockSize = kDefaultBlockSize;
  ushort numChannels = kDefaultNumChannels;
  double sampleRate = kDefaultSampleRate;
  double tempo = kDefaultTempo;
  ushort beatsPerMeasure = kDefaultBeatsPerMeasure;
  ushort noteValue = kDefaultNoteValue;

  GlobalSettings() {

  }
public:
  static GlobalSettings& get() {
    static GlobalSettings globalSettings;
    return globalSettings;
  }

  inline void setBlockSize(ulong blockSize) {
    this->blockSize = blockSize;
  }
  inline ulong getBlockSize() const {
    return blockSize;
  }

  inline void setNumChannels(ushort numChannels) {
    this->numChannels = numChannels;
  }
  inline ushort getNumChannels() const {
    return numChannels;
  }

  inline void setSampleRate(double sampleRate) {
    this->sampleRate = sampleRate;
  }
  inline double getSampleRate() {
    return sampleRate;
  }

  inline double getTempo() {
    return tempo;
  }

  inline void setTempo(double tempo) {
    this->tempo = tempo;
  }

  inline unsigned short getBeatsPerMeasure() {
    return beatsPerMeasure;
  }
  inline void setBeatsPerMeasure(unsigned short beatsPerMeasure) {
    this->beatsPerMeasure = beatsPerMeasure;
  }

  inline unsigned short getNoteValue() {
    return noteValue;
  }
  inline void setNoteValue(unsigned short noteValue) {
    this->noteValue = noteValue;
  }
};