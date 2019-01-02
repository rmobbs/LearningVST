#pragma once

#include "Types.h"

// Singleton. Use GlobalSettings::get() to get the instance
class GlobalSettings {
  static constexpr ulong kDefaultBlockSize = 5121;

protected:
  ulong blockSize = kDefaultBlockSize;

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
};