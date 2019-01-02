#pragma once

#include <math.h>
#include "Types.h"

#define DEFAULT_SAMPLE_RATE 44100.0
#define DEFAULT_TEMPO 120.0
#define DEFAULT_TIMESIG_BEATS_PER_MEASURE 4
#define DEFAULT_TIMESIG_NOTE_VALUE 4

// Implemented as a singleton for simplicity ... use AudioClock::get
class AudioClock {
protected:
  bool transportChanged = false;
  bool isPlaying = false;
  unsigned long currentFrame = 0;
  double tempo = DEFAULT_TEMPO;
  double sampleRate = DEFAULT_SAMPLE_RATE;
  unsigned short beatsPerMeasure = DEFAULT_TIMESIG_BEATS_PER_MEASURE;
  unsigned short noteValue = DEFAULT_TIMESIG_NOTE_VALUE;

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

  inline double getTempo() {
    return tempo;
  }

  inline void setTempo(double tempo) {
    this->tempo = tempo;
  }

  inline double getSampleRate() {
    return sampleRate;
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

  // In VST lingo, PPQ is musical position in quarter note (e.g., 1.0 = 1 quarter note)
  inline double getPpqPos() {
    // This is dependent on two variables so better to always calculate it
    double samplesPerBeat = (60.0 / getTempo()) * getSampleRate();
    return (getCurrentFrame() / samplesPerBeat) + 1.0f;
  }

  // Start of bar as musical position
  inline double getBarStartPos(double ppqPos) {
    double currentBarPos = floor(ppqPos / static_cast<double>(getBeatsPerMeasure()));
    return currentBarPos * static_cast<double>(getBeatsPerMeasure()) + 1.0;
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

