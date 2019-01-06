#pragma once

#include <math.h>
#include "Types.h"
#include "GlobalSettings.h"

// Implemented as a singleton for simplicity ... use AudioClock::get
class AudioClock {
protected:
  bool transportChanged = false;
  bool isPlaying = false;
  unsigned long currentFrame = 0;

  AudioClock() {
  }

public:
  static inline AudioClock& get() {
    static AudioClock audioClock;
    return audioClock;
  }

  inline unsigned long getCurrentFrame() {
    return currentFrame;
  }

  inline bool getTransportChanged() {
    return transportChanged;
  }

  inline bool getIsPlaying() {
    return isPlaying;
  }

  // In VST lingo, PPQ is musical position in quarter note (e.g., 1.0 = 1 quarter note)
  inline double getPpqPos() {
    // This is dependent on two variables so better to always calculate it
    double samplesPerBeat = (60.0 / GlobalSettings::get().
      getTempo()) * GlobalSettings::get().getSampleRate();
    return (getCurrentFrame() / samplesPerBeat) + 1.0f;
  }

  // Start of bar as musical position
  inline double getBarStartPos(double ppqPos) {
    double currentBarPos = floor(ppqPos / 
      static_cast<double>(GlobalSettings::get().getBeatsPerMeasure()));
    return currentBarPos * static_cast<double>
      (GlobalSettings::get().getBeatsPerMeasure()) + 1.0;
  }

  void advance(unsigned long blockSize) {
    if (currentFrame == 0 || !isPlaying) {
      transportChanged = true;
      isPlaying = true;
    }
    else {
      transportChanged = false;
    }

    currentFrame += blockSize;
  }
};

