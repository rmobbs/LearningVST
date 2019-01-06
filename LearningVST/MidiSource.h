#pragma once

#include <stdio.h>
#include <string>
#include <vector>
#include <queue>
#include "Types.h"

struct MidiEvent {
  enum class EventType {
    Meta,
    Message,
    Sysex,
  };

  enum class MetaType {
    SequenceNumber,
    TextEvent,
    CopyrightNotice,
    SequenceOrTrackName,
    InstrumentName,
    Lyric,
    Marker,
    CuePoint,
    MidiChannelPrefix,
    EndOfTrack,
    SetTempo,
    SmtpeOffset,
    TimeSignature,
    KeySignature,
    SequencerSpecificMetaEvent,
  };

  enum class MessageType {
    Unknown,
    VoiceNoteOff,
    VoiceNoteOn,
    VoicePolyphonicKeyPressure,
    VoiceControllerChange,
    VoiceProgramChange,
    VoiceKeyPressure,
    VoicePitchBend,
    ModeAllSoundOff,
    ModeResetAllControllers,
    ModeLocalControl,
    ModeAllNotesOff,
    ModeOmniModeOff,
    ModeOmniModeOn,
    ModeMonoModeOn,
    ModePolyModeOn,
  };

  EventType eventType;

  union {
    struct {
      MetaType type;
    } meta;
    struct {
      MessageType type;
      uchar channel;
      uchar status;
    } message;
  }; 

  ulong timeStamp = 0;

  uchar* dataptr = nullptr;
  ushort datalen = 0;

  ulong delta = 0; // Ugh
};

struct MidiTrack {
  std::vector<MidiEvent> events;
  std::vector<uchar> eventData;
  std::queue<MidiEvent> sequence;
  unsigned int index;
};

class MidiSource {
protected:
  static constexpr ushort kDefaultTimeDivision = 96;

  enum class TimeDivisionType {
    Unknown,
    TicksPerQuarterNote,
    SmpteFrameData,
  };

  TimeDivisionType timeDivisionType = TimeDivisionType::Unknown;

  ushort formatType = 0;
  ushort timeDivision = kDefaultTimeDivision;

  std::vector<MidiTrack> tracks;

  bool readTrack(std::istream& is, unsigned int trackIndex);
  bool parseHeader(std::istream& is);
  bool parseChunk(std::istream& is, const std::string& expectedChunkId);

public:
  bool openFile(const std::string& fileName);

  inline const std::vector<MidiTrack> getTracks() {
    return tracks;
  }
  inline size_t getTrackCount() const {
    return tracks.size();
  }
  inline unsigned short getFormatType() const {
    return formatType;
  }
};
