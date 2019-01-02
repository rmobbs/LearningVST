// LearningVST.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include "pch.h" // TODO: Re-enable PCH in project settings once MidiFile is a liberry
#include <iostream>
#include <string>
#include <filesystem>
#include <assert.h>
#include "MidiSource.h"
#include "AudioClock.h"
#include "GlobalSettings.h"

// GFlags
#include "gflags/gflags.h"

// VST2.X SDK
#define VST_FORCE_DEPRECATED 0 // TODO: See if we really need to do this
#include "aeffectx.h"

static const std::string kVendorName("Dry Cactus");
static const std::string kProgramName("LearningVST");
static unsigned int kVersionMajor = 0;
static unsigned int kVersionMinor = 1;
static unsigned int kVersionPatch = 0;

// Need a global instance b/c it is passed by pointer via callbacks
VstTimeInfo vstTimeInfo = { };

// VST2.X callbacks
typedef AEffect *(*Vst2xPluginEntryFunc)(audioMasterCallback host);
typedef VstIntPtr (*Vst2xPluginDispatcherFunc)(AEffect *effect, VstInt32 opCode, VstInt32 index, VstIntPtr value, void *ptr, float opt);
typedef float (*Vst2xPluginGetParameterFunc)(AEffect *effect, VstInt32 index);
typedef void (*Vst2xPluginSetParameterFunc)(AEffect *effect, VstInt32 index, float value);
typedef void (*Vst2xPluginProcessFunc)(AEffect *effect, float **inputs, float **outputs, VstInt32 sampleFrames);
extern "C" {
  VstIntPtr VSTCALLBACK pluginVst2xHostCallback(AEffect *effect, VstInt32 opCode, VstInt32 index, VstIntPtr value, void *dataPtr, float opt);
}

VstIntPtr VSTCALLBACK pluginVst2xHostCallback(AEffect *effect, VstInt32 opCode, VstInt32 index, VstIntPtr value, void *dataPtr, float opt) {
  VstIntPtr result = 0;

  switch (opCode) {
    case audioMasterAutomate:
      break;
    case audioMasterVersion:
      // We are VST 2.4 compatible
      result = 2400;
      break;
    case audioMasterCurrentId:
      // Welp, I guess this is always 0 because we are not currently supporting a chain?
      result = 0; // currentPluginUniqueId
      break;
    case audioMasterIdle:
      // Ignore
      result = 1;
      break;
    case audioMasterWantMidi:
      // This is deprecated but older instruments can make the call to tell us they're an instrument; we 
      // want to ignore it but not return a failure
      result = 1;
      break;
    case audioMasterGetVendorString:
      // Who we are
      strncpy(reinterpret_cast<char *>(dataPtr), kVendorName.c_str(), kVstMaxVendorStrLen);
      result = 1;
      break;
    case audioMasterGetProductString:
      // What we're doing
      strncpy(reinterpret_cast<char *>(dataPtr), kProgramName.c_str(), kVstMaxProductStrLen);
      result = 1;
      break;
    case audioMasterGetVendorVersion:
      // Semantic version as single string: A.B.C = ABCC
      result = kVersionMajor * 1000 + kVersionMinor * 100 + kVersionPatch;
      break;
    case audioMasterGetCurrentProcessLevel:
      // TODO: See if we can do better than this ...
      result = kVstProcessLevelUnknown;
      break;

    case audioMasterGetTime: {
      auto& audioClock = AudioClock::get();

      vstTimeInfo.samplePos = audioClock.getCurrentFrame();
      vstTimeInfo.sampleRate = audioClock.getSampleRate();

      // Set flags for transport state
      vstTimeInfo.flags = 0;
      if (audioClock.getTransportChanged()) {
        vstTimeInfo.flags |= kVstTransportChanged;
      }
      if (audioClock.getIsPlaying()) {
        vstTimeInfo.flags |= kVstTransportPlaying;
      }

      // See what other info was requested. Note that I'm only mentioning 
      // ones we actually handle, which could result in some unlogged cases.
      // TODO: add logging for requested data which we do not support

      if (value & kVstNanosValid) {
        // Oh boy ... we're running real-time, seems we should implement this ... but how ...
      }
      if (value & kVstPpqPosValid) {
        vstTimeInfo.ppqPos = audioClock.getPpqPos();
        vstTimeInfo.flags |= kVstPpqPosValid;
      }
      if (value & kVstTempoValid) {
        vstTimeInfo.tempo = audioClock.getTempo();
        vstTimeInfo.flags |= kVstTempoValid;
      }
      if (value & kVstBarsValid) {
        // Misbehaving plugins ...
        if (!(value & kVstPpqPosValid)) {
          std::cerr << "Plugin requested position in bars but not PPQ; calculation will be invalid" << std::endl;
        }

        vstTimeInfo.barStartPos = audioClock.getBarStartPos(vstTimeInfo.ppqPos);
        vstTimeInfo.flags |= kVstBarsValid;
      }
      if (value & kVstTimeSigValid) {
        vstTimeInfo.timeSigNumerator = audioClock.getBeatsPerMeasure();
        vstTimeInfo.timeSigDenominator = audioClock.getNoteValue();
        vstTimeInfo.flags |= kVstTimeSigValid;
      }

      result = reinterpret_cast<VstIntPtr>(&vstTimeInfo);
      break;
    }
  }
  return result;
}

// Helper function to convert GetLastError code to std::string
std::string GetLastErrorString()
{
  DWORD error = GetLastError();
  if (error)
  {
    LPVOID lpMsgBuf;
    DWORD bufLen = FormatMessage(
      FORMAT_MESSAGE_ALLOCATE_BUFFER |
      FORMAT_MESSAGE_FROM_SYSTEM |
      FORMAT_MESSAGE_IGNORE_INSERTS,
      NULL,
      error,
      MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
      (LPTSTR)&lpMsgBuf,
      0, NULL);
    if (bufLen)
    {
      LPCSTR lpMsgStr = (LPCSTR)lpMsgBuf;
      std::string result(lpMsgStr, lpMsgStr + bufLen);

      LocalFree(lpMsgBuf);

      return result;
    }
  }
  return std::string();
}

#define DEFAULT_BLOCKSIZE 5121

// Only one instrument can be present in a chain, so for now we'll just directly use
// a single plugin
// We can later add effects

enum class VstPluginType {
  Unknown,
  Effect,
  Instrument,
};

class VstPlugin {
protected:
  VstPluginType type = VstPluginType::Unknown;
  std::string name;
  std::string absolutePath;
  HMODULE handle;
  AEffect *plugin;

  void setupSpeakers(VstSpeakerArrangement& speakerArrangement, int numChannels) {
    memset(&speakerArrangement, 0, sizeof(speakerArrangement));

    if (numChannels <= 8) {
      speakerArrangement.numChannels = numChannels;
    }
    else {
      std::cerr << "Unable to configure more than 8 speakers" << std::endl;
      speakerArrangement.numChannels = 8;
    }

    VstInt32 speakerTypes[] = {
      kSpeakerArrEmpty,
      kSpeakerArrMono,
      kSpeakerArrStereo,
      kSpeakerArr30Music,
      kSpeakerArr40Music,
      kSpeakerArr50,
      kSpeakerArr60Music,
      kSpeakerArr70Music,
      kSpeakerArr80Music,
    };

    speakerArrangement.type = speakerTypes[speakerArrangement.numChannels];

    for (int i = 0; i < speakerArrangement.numChannels; ++i) {
      speakerArrangement.speakers[i].type = kSpeakerUndefined;
    }
  }

public:
  VstPlugin(std::string absolutePath) {
    this->absolutePath = absolutePath;

    // Parse out the name
    this->name = std::filesystem::path(absolutePath).filename().string();

    // We only support instruments at this time
    this->type = VstPluginType::Instrument;
  }

  virtual bool open() { 
    // Attempt to load the DLL
    this->handle = LoadLibraryExA((LPCSTR)absolutePath.c_str(), 
      nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (this->handle == nullptr) {
      std::cerr << "Unable to load specified VSTi: " << GetLastErrorString().c_str() << std::endl;
      return false;
    }

    // Find and execute the entry func to get the AEffect pointer
    std::string entryFuncNames[] = {
      "VSTPluginMain",
      "VSTPluginMain()",
      "main"
    };

    Vst2xPluginEntryFunc entryFunc = nullptr;
    for (const auto& entryFuncName : entryFuncNames) {
      entryFunc = reinterpret_cast<Vst2xPluginEntryFunc>
        (GetProcAddress(this->handle, entryFuncName.c_str()));
      if (entryFunc != nullptr) {
        break;
      }
    }

    if (entryFunc == nullptr) {
      std::cerr << "Unable to find entry func in specified VSTi" << std::endl;
      return false;
    }

    this->plugin = entryFunc(pluginVst2xHostCallback);

    if (this->plugin == nullptr) {
      std::cerr << "Specified VSTi returned null plugin instance" << std::endl;
      return false;
    }

    if (this->plugin->magic != kEffectMagic) {
      std::cerr << "Plugin loaded but magic number is incorrect" << std::endl;
      return false;
    }

    // See if we're an instrument or an effect
    if (plugin->flags & effFlagsIsSynth) {
      type = VstPluginType::Instrument;
    }
    else {
      type = VstPluginType::Effect;
      std::cerr << "Effect plugins are not currently supported" << std::endl;
      return false;
    }

    // We don't support shell plugins
    if (plugin->dispatcher(plugin, effGetPlugCategory, 0, 0, nullptr, 0.0f) == kPlugCategShell) {
      std::cerr << "Shell plugins are not supported" << std::endl;
      return false;
    }

    // Setup
    plugin->dispatcher(plugin, effOpen, 0, 0, nullptr, 0.0f);
    plugin->dispatcher(plugin, effSetSampleRate, 0, 0, 
      nullptr, static_cast<float>(AudioClock::get().getSampleRate()));
    plugin->dispatcher(plugin, effSetBlockSize, 0, 
      static_cast<VstIntPtr>(GlobalSettings::get().getBlockSize()), nullptr, 0.0f);

    VstSpeakerArrangement inSpeakers;
    setupSpeakers(inSpeakers, plugin->numInputs);
    VstSpeakerArrangement outSpeakers;
    setupSpeakers(outSpeakers, plugin->numOutputs);

    plugin->dispatcher(plugin, effSetSpeakerArrangement, 0, 
      reinterpret_cast<VstIntPtr>(&inSpeakers), &outSpeakers, 0.0f);

    return true;  
  }

  void resume() {
    std::cout << "Resuming plugin " << name << std::endl;

    plugin->dispatcher(plugin, effMainsChanged, 0, 1, nullptr, 0.0f);
    plugin->dispatcher(plugin, effStartProcess, 0, 0, nullptr, 0.0f);
  }

  void suspend() {
    std::cout << "Suspending plugin " << name << std::endl;

    plugin->dispatcher(plugin, effMainsChanged, 0, 0, nullptr, 0.0f);
    plugin->dispatcher(plugin, effStopProcess, 0, 0, nullptr, 0.0f);
  }
};

DEFINE_string(midi, "", "Full path to MIDI file");
DEFINE_string(vsti, "", "Full path to VST instrument plugin");

VstPlugin *instrumentPlugin = nullptr;

bool getBlockFromSequence(std::queue<MidiEvent>& midiSequence, ulong startTimeStamp, ulong endTimeStamp, std::queue<MidiEvent>& midiBlock)
{
  while (true) {
    // Finished sequence
    if (midiSequence.empty()) {
      return false;
    }

    auto nextEvent = midiSequence.front();

    // Discard any old events
    if (nextEvent.timeStamp < startTimeStamp) {
      std::cerr << "Expired time stamp while parsing MIDI events" << std::endl;
      midiSequence.pop();
      continue;
    }

    // Exit on first out-of-range event
    if (endTimeStamp < nextEvent.timeStamp) {
      break;
    }

    midiSequence.pop();

    nextEvent.delta = nextEvent.timeStamp - startTimeStamp;
    midiBlock.push(nextEvent);
  }
  return true;
}

bool processMetaEvents(std::queue<MidiEvent>& midiEvents) {
  bool finished = false;
  while (!midiEvents.empty()) {
    auto midiEvent = midiEvents.front();
    midiEvents.pop();
    if (midiEvent.eventType == MidiEvent::EventType::Meta) {
      switch (midiEvent.meta.type) {
        case MidiEvent::MetaType::SetTempo: {
          double tempo;
          unsigned long beatLengthInUs = static_cast<unsigned long>
            ((midiEvent.dataptr[0] << 16) | (midiEvent.dataptr[1] << 8) | (midiEvent.dataptr[2]));
          tempo = (1000000.0 / static_cast<double>(beatLengthInUs)) * 60.0;
          AudioClock::get().setTempo(tempo);
          break;
        }
        case MidiEvent::MetaType::TimeSignature: {
          AudioClock::get().setBeatsPerMeasure(midiEvent.dataptr[0]);
          AudioClock::get().setNoteValue(static_cast<unsigned short>(powl(2, midiEvent.dataptr[1])));
          break;
        }
        case MidiEvent::MetaType::EndOfTrack:
          finished = true;
          break;
      }
    }
  }
  return !finished;
}

int main(int argc, char *argv[])
{
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  if (FLAGS_midi.length() != 0) {
    MidiSource midiFile;
    if (midiFile.openFile(FLAGS_midi.c_str()) == true) {

      // Our current limitations
      if (midiFile.getTrackCount() != 1) {
        std::cerr << "Currently unable to support more than one track" << std::endl;
      }
      else if (midiFile.getFormatType() != 0) {
        std::cerr << "Currently unable to support MIDI other than type 0" << std::endl;
      }
      else {
        // Make a copy of the sequence queue
        std::queue<MidiEvent> midiSequence = midiFile.getTracks()[0].sequence;

        if (FLAGS_vsti.length() != 0) {
          instrumentPlugin = new VstPlugin(FLAGS_vsti);
          if (instrumentPlugin->open()) {

            // Start 'er up
            instrumentPlugin->resume();

            // This only works as a non-real-time process, because we are just 
            // repeatedly grabbing 'blocksize' events from the queue and pushing
            // them to the VSTi. We need to find a way to time sync.
            bool finishedSimulating = false;
            while (!finishedSimulating) {
              // Get next block
              std::queue<MidiEvent> midiBlock;
              finishedSimulating = !getBlockFromSequence(midiSequence, 
                AudioClock::get().getCurrentFrame(),
                AudioClock::get().getCurrentFrame() + GlobalSettings::get().getBlockSize(), 
                midiBlock);

              // This seems suspect ... processing a full block of events then 
              // processing the notes would seem to make things like tempo
              // changes happen at the wrong time. This seems to assume such
              // things will only happen at t=0

              // Process events
              auto metaQueue = midiBlock;
              if (!processMetaEvents(metaQueue)) {
                finishedSimulating = true;
              }

              // Send messages to plugin

              // Render output

              // Fixed clock advance rate
              AudioClock::get().advance(GlobalSettings::get().getBlockSize());
            }

            // TODO: CLEAN UP
          }
        }
      }
    }
  }

  return 0;
}

