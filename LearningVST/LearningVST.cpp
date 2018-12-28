// LearningVST.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include "pch.h"
#include <iostream>
#include <string>
#include <filesystem>
#include "gflags/gflags.h"

static const std::string kVendorName("Dry Cactus");
static const std::string kProgramName("LearningVST");

// Almost all of these indicate work still to be done
#define DEFAULT_SAMPLE_RATE 44100.0
#define DEFAULT_NUM_CHANNELS 2
#define DEFAULT_BLOCKSIZE 512l
#define DEFAULT_TIME_DIVISION 96
#define DEFAULT_TEMPO 120.0
#define DEFAULT_TIMESIG_BEATS_PER_MEASURE 4
#define DEFAULT_TIMESIG_NOTE_VALUE 4

// Need a global instance b/c it is passed by pointer via callbacks
VstTimeInfo vstTimeInfo = { };

// Implemented as a singleton for simplicity ... use AudioClock::get
class AudioClock {
protected:
  bool transportChanged = false;
  bool isPlaying = false;
  unsigned long currentFrame = 0;
  double tempo = DEFAULT_TEMPO;
  double sampleRate = DEFAULT_SAMPLE_RATE;

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

  inline double getSampleRate() {
    return sampleRate;
  }

  inline unsigned short getBeatsPerMeasure() {
    return DEFAULT_TIMESIG_BEATS_PER_MEASURE;
  }

  inline unsigned short getNoteValue() {
    return DEFAULT_TIMESIG_NOTE_VALUE;
  }

  // In VST lingo, PPQ is musical position in quarter note (e.g., 1.0 = 1 quarter note)
  inline double getPpqPos() {
    // This is dependent on two variables so better to always calculate it
    double samplesPerBeat = (60.0 / getTempo()) * getSampleRate();
    return (getCurrentFrame() / samplesPerBeat) + 1.0f;
  }

  // Start of bar as musical position
  inline double getBarStartPos(float ppqPos) {
    double currentBarPos = floor(ppqPos / static_cast<double>(getBeatsPerMeasure()));
    return currentBarPos * static_cast<double>(getBeatsPerMeasure()) + 1.0;
  }
};

// VST2.X SDK
#define VST_FORCE_DEPRECATED 0 // TODO: See if we really need to do this
#include "aeffectx.h"

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
      strncpy(reinterpret_cast<char *>(dataPtr), kVendorName.c_str(), kVendorName.length());
      result = 1;
      break;
    case audioMasterGetProductString:
      // What we're doing
      strncpy(reinterpret_cast<char *>(dataPtr), kProgramName.c_str(), kProgramName.length());
      result = 1;
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

// Only one instrument can be present in a chain, so for now we'll just directly use
// a single plugin
// We can later add effects

enum class VstPluginType {
  Unknown,
  Unsupported,
  Effect,
  Instrument,
  Count
};

class VstPlugin {
protected:
  VstPluginType type;
  std::string name;
  std::string absolutePath;
  HMODULE handle;
  AEffect *plugin;

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
      std::cout << "Unable to load specified VSTi: " << GetLastErrorString().c_str() << std::endl;
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
      std::cout << "Unable to find entry func in specified VSTi" << std::endl;
      return false;
    }

    this->plugin = entryFunc(pluginVst2xHostCallback);

    if (this->plugin == nullptr) {
      std::cout << "Specified VSTi returned null plugin instance" << std::endl;
      return false;
    }

    return true;  
  };
};

DEFINE_string(vsti, "", "Full path to VST instrument plugin");

VstPlugin *instrumentPlugin = nullptr;

int main(int argc, char *argv[])
{
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  if (FLAGS_vsti.length() != 0) {
    instrumentPlugin = new VstPlugin(FLAGS_vsti);
    instrumentPlugin->open();
  }
}

