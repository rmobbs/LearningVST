#include "MidiSource.h"

#include <fstream>
#include <array>
#include <vector>
#include <streambuf>
#include <assert.h>
#include <map>
#include "AudioClock.h"

std::map<unsigned char, MidiEvent::EventType> ByteSignatureToReservedEventType = {
  { 0xFF, MidiEvent::EventType::Meta },
  { 0xF0, MidiEvent::EventType::Sysex },
  { 0xF7, MidiEvent::EventType::Sysex },
};

std::map<unsigned char, MidiEvent::MetaType> ByteSignatureToMidiMetaType = {
  { 0x00, MidiEvent::MetaType::SequenceNumber },
  { 0x01, MidiEvent::MetaType::TextEvent },
  { 0x02, MidiEvent::MetaType::CopyrightNotice },
  { 0x03, MidiEvent::MetaType::SequenceOrTrackName },
  { 0x04, MidiEvent::MetaType::InstrumentName },
  { 0x05, MidiEvent::MetaType::Lyric },
  { 0x06, MidiEvent::MetaType::Marker },
  { 0x07, MidiEvent::MetaType::CuePoint },
  { 0x20, MidiEvent::MetaType::MidiChannelPrefix },
  { 0x2F, MidiEvent::MetaType::EndOfTrack },
  { 0x51, MidiEvent::MetaType::SetTempo },
  { 0x54, MidiEvent::MetaType::SmtpeOffset },
  { 0x58, MidiEvent::MetaType::TimeSignature },
  { 0x59, MidiEvent::MetaType::KeySignature },
  { 0x7F, MidiEvent::MetaType::SequencerSpecificMetaEvent },
};

bool MidiSource::openFile(const std::string& fileName) {
  std::ifstream ifs(fileName, std::ios::binary);
  if (!ifs) {
    std::cerr << "Unable to open MIDI file " << fileName << std::endl;
    return false;
  }

  // Read the file into a buffer
  endian_bytestream ebs;
  ebs << ifs.rdbuf();
  ifs.close();

  // Header
  if (!parseHeader(ebs)) {
    std::cerr << "Unable to parse MIDI file header" << std::endl;
    return false;
  }

  // Tracks
  for (unsigned int trackIndex = 0; trackIndex < static_cast<unsigned int>(this->getTrackCount()); ++trackIndex) {
    if (!readTrack(ebs, trackIndex)) {
      return false;
    }
  }

  return true;
}

bool MidiSource::parseChunk(endian_bytestream& ebs, const std::string& expectedChunkId) {
  char chunkId[4] = { };

  ebs.read(chunkId, 4);

  if (!ebs.isGood("while reading chunk")) {
    return false;
  }

  if (strncmp(chunkId, expectedChunkId.c_str(), 4) != 0) {
    std::cerr << "Unexpected chunk ID " <<
      std::string(chunkId, chunkId + 4) <<
      " (expected " << expectedChunkId << ")" << std::endl;
    return false;
  }

  return true;
}

bool MidiSource::parseHeader(endian_bytestream& ebs) {
  // MThd character tag
  if (!parseChunk(ebs, "MThd")) {
    std::cerr << "Unable to find MIDI file header tag" << std::endl;
    return false;
  }

  // Header byte count, unsigned int
  unsigned int byteCount;
  ebs >> byteCount;
  if (!ebs.isGood("while reading header byte count")) {
    return false;
  }
  if (byteCount != 6) {
    std::cerr << "Unexpected header byte count of " << byteCount << " (expected 6)" << std::endl;
    return false;
  }

  // Format type
  ebs >> formatType;
  if (!ebs.isGood("while reading format type")) {
    return false;
  }

  // Number of tracks
  unsigned short trackCount;
  ebs >> trackCount;
  if (!ebs.isGood("while reading track count")) {
    return false;
  }
  tracks.resize(trackCount);

  // Time division
  ebs >> timeDivision;
  if (!ebs.isGood("while reading time division")) {
    return false;
  }

  // If MSB is set, it's SMTPE frame time
  if (timeDivision & 0x8000) {
    timeDivisionType = TimeDivisionType::SmpteFrameData;

    // Currently not supported
    std::cerr << "SMTPE frame based time division ebs currently not supported" << std::endl;
    return false;
  }
  else {
    timeDivisionType = TimeDivisionType::TicksPerQuarterNote;
  }

  return true;
}


bool MidiSource::readTrack(endian_bytestream& ebs, unsigned int trackIndex) {
  assert(trackIndex < tracks.size());

  ulong currentTimeInSampleFrames = 0;

  MidiTrack& currentTrack = tracks[trackIndex];

  currentTrack.index = trackIndex;

  // MTrk character tag
  if (!parseChunk(ebs, "MTrk")) {
    std::cerr << "Expected to find MTrk tag at start of track" << std::endl;
    return false;
  }

  unsigned int byteCount;
  ebs >> byteCount;
  if (!ebs.isGood("while reading track byte count")) {
    return false;
  }

  // To reduce fragmentation, all event data is stored in a contiguous block;
  // as this can resize several times we will store indices as we read events,
  // then fixup pointers
  std::vector<uint> eventDataIndex;

  unsigned int lastByte = static_cast<unsigned int>(ebs.tellg()) + byteCount;
  while (static_cast<unsigned int>(ebs.tellg()) < lastByte) {
    // First entry for each data element is a variable-length delta time stored
    // as a series of byte chunks.
    // If the MSB is set this byte contributes the next 7 bits to the delta time
    unsigned int deltaTime = 0;
    unsigned char readByte;
    do {
      ebs >> readByte;
      if (!ebs.isGood("while reading data event delta time")) {
        return false;
      }
      deltaTime = (deltaTime << 7) | (readByte & 0x7F);
    } while (readByte & 0x80);

    // Generate absolute timestamp from relative delta
    assert(timeDivisionType == TimeDivisionType::TicksPerQuarterNote);
    double ticksPerSecond = static_cast<double>(timeDivision) * GlobalSettings::get().getTempo() / 60.0;
    double sampleFramesPerTick = GlobalSettings::get().getSampleRate() / ticksPerSecond;
    currentTimeInSampleFrames += static_cast<long>(deltaTime * sampleFramesPerTick);

    // Next is the event type
    ebs >> readByte;
    if (!ebs.isGood("while reading event type")) {
      return false;
    }

    // Check for reserved event types
    const auto eventType = ByteSignatureToReservedEventType.find(readByte);
    if (eventType != ByteSignatureToReservedEventType.end()) {
      MidiEvent currentEvent;

      currentEvent.timeStamp = currentTimeInSampleFrames;
      currentEvent.eventType = eventType->second;

      switch (eventType->second) {
        case MidiEvent::EventType::Meta: {
          // Meta type
          ebs >> readByte;
          if (!ebs.isGood("while reading meta type")) {
            return false;
          }

          // Grab the actual meta type, to determine if we store or skip
          const auto metaType = ByteSignatureToMidiMetaType.find(readByte);

          // Data size
          ebs >> readByte;
          if (!ebs.isGood("while reading meta data size")) {
            return false;
          }

          // Only store recognized/requested types
          if (metaType != ByteSignatureToMidiMetaType.end()) {
            currentEvent.meta.type = metaType->second;

            if (readByte > 0) {
              // Data
              currentEvent.datalen = readByte;
              eventDataIndex.push_back(currentTrack.eventData.size());
              currentTrack.eventData.resize(currentTrack.eventData.size() + currentEvent.datalen);
              ebs.read(reinterpret_cast<char *>(currentTrack.
                eventData.data() + eventDataIndex.back()), currentEvent.datalen);
              if (!ebs.isGood("while reading meta data")) {
                return false;
              }
            }
            else {
              // Push empty marker into data index vector for bookkeeping
              eventDataIndex.push_back(-1);
            }
            currentTrack.events.push_back(currentEvent);
          }
          // Otherwise just skip it
          else if (readByte > 0) {
            ebs.seekg(readByte, std::ios_base::cur);
            if (!ebs.isGood("while skipping meta data")) {
              return false;
            }
          }
          break;
        }
        case MidiEvent::EventType::Sysex: {
          // Data size
          ebs >> readByte;
          if (!ebs.isGood("while reading sysex data size")) {
            return false;
          }

          // Just skip it
          ebs.seekg(readByte, std::ios_base::cur);
          if (!ebs.isGood("while skipping sysex data")) {
            return false;
          }
          break;
        }
      }
    }
    // All other event types are messages
    else {
      MidiEvent currentEvent;

      currentEvent.timeStamp = currentTimeInSampleFrames;
      currentEvent.eventType = MidiEvent::EventType::Message;

      // All messages have at least one byte of data
      uchar dataByte;
      ebs >> dataByte;
      if (!ebs.isGood("while reading message data 0")) {
        return false;
      }

      // Least significant nibble of the status byte is the channel
      currentEvent.message.channel = (readByte & 0x0F);

      // Determine the message type
      if ((readByte & 0xF0) == 0x80) {
        currentEvent.message.type = MidiEvent::MessageType::VoiceNoteOff;
      }
      else if ((readByte & 0xF0) == 0x90) {
        currentEvent.message.type = MidiEvent::MessageType::VoiceNoteOn;
      }
      else if ((readByte & 0xF0) == 0xA0) {
        currentEvent.message.type = MidiEvent::MessageType::VoicePolyphonicKeyPressure;
      }
      else if ((readByte & 0xF0) == 0xB0) {
        switch (dataByte) {
          case 0x78:
            currentEvent.message.type = MidiEvent::MessageType::ModeAllSoundOff;
            break;
          case 0x79:
            currentEvent.message.type = MidiEvent::MessageType::ModeResetAllControllers;
            break;
          case 0x7A:
            currentEvent.message.type = MidiEvent::MessageType::ModeLocalControl;
            break;
          case 0x7B:
            currentEvent.message.type = MidiEvent::MessageType::ModeAllNotesOff;
            break;
          case 0x7C:
            currentEvent.message.type = MidiEvent::MessageType::ModeOmniModeOff;
            break;
          case 0x7D:
            currentEvent.message.type = MidiEvent::MessageType::ModeOmniModeOn;
            break;
          case 0x7E:
            currentEvent.message.type = MidiEvent::MessageType::ModePolyModeOn;
            break;
          default:
            currentEvent.message.type = MidiEvent::MessageType::VoiceControllerChange;
            break;
        }
      }
      else if ((readByte & 0xF0) == 0xC0) {
        currentEvent.message.type = MidiEvent::MessageType::VoiceProgramChange;
      }
      else if ((readByte & 0xF0) == 0xD0) {
        currentEvent.message.type = MidiEvent::MessageType::VoiceKeyPressure;
      }
      else if ((readByte & 0xF0) == 0xE0) {
        currentEvent.message.type = MidiEvent::MessageType::VoicePitchBend;
      }
      else {
        currentEvent.message.type = MidiEvent::MessageType::Unknown;
      }

      if (currentEvent.message.type == MidiEvent::MessageType::Unknown) {
        // If we don't know what it is, we can attempt to skip it; can't be worse
        // than just returning
        std::cerr << "Encountered unknown message type ... "
          "skipping 2 bytes of data but errors could result" << std::endl;
        ebs.seekg(1, std::ios_base::cur);
        if (!ebs.isGood("while skipping unknown message data")) {
          return false;
        }
      }
      else {
        // Store the raw status byte for passing to VST
        currentEvent.message.status = readByte;

        // Mark location in data buffer
        eventDataIndex.push_back(currentTrack.eventData.size());

        // Ensure we have space in the data buffer
        if (currentEvent.message.type == MidiEvent::MessageType::VoiceProgramChange ||
            currentEvent.message.type == MidiEvent::MessageType::VoiceKeyPressure) {
          currentEvent.datalen = 1;
        }
        else {
          currentEvent.datalen = 2;
        }

        currentTrack.eventData.resize(currentTrack.
          eventData.size() + currentEvent.datalen);

        // Store bit 0
        currentTrack.eventData[eventDataIndex.back()] = dataByte;
        if (currentEvent.datalen > 1) {
          // Store bit 1
          ebs >> dataByte;
          currentTrack.eventData[eventDataIndex.back() + 1] = dataByte;
          if (!ebs.isGood("while reading message data 1")) {
            return false;
          }
        }

        currentTrack.events.push_back(currentEvent);
      }
    }
  }

  // Clean up memory
  currentTrack.eventData.shrink_to_fit();

  assert(eventDataIndex.size() == currentTrack.events.size());
  auto eventIter = currentTrack.events.begin();
  for (const auto& dataIndex : eventDataIndex) {
    // Fixup data pointers
    if (dataIndex != -1) {
      eventIter->dataptr = currentTrack.eventData.data() + dataIndex;
    }

    // Build playback sequence
    switch (eventIter->eventType) {
      case MidiEvent::EventType::Meta: {
        switch (eventIter->meta.type) {
          case MidiEvent::MetaType::SetTempo:
          case MidiEvent::MetaType::TimeSignature:
          case MidiEvent::MetaType::EndOfTrack:
            currentTrack.sequence.push(*eventIter);
            break;
        }
        break;
      }
      case MidiEvent::EventType::Message: {
        currentTrack.sequence.push(*eventIter);
        break;
      }
      default:
        break;
    }

    ++eventIter;
  }
  return true;
}

