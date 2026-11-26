#pragma once

#include <stdint.h>

struct MidiDeviceParamInfo {
  bool active = false;
  bool p4_param = false;
  bool sendable = false;
  bool nrpn = false;
  bool macro = false;
  uint8_t param_id = 0;
  uint8_t ctrl = 0;
  uint8_t ctrl_type = 0;
  uint16_t resolution = 128;
  int16_t min_value = 0;
  int16_t max_value = 127;
  int16_t default_value = 0;
  int16_t current_value = 0;
};

// The values an edit of a 7-bit parameter may set. A stored value outside
// the range (an older Kit) is kept until it is edited.
struct MidiDeviceParamRange {
  uint8_t min;
  uint8_t max;

  uint8_t clamp(uint8_t value) const {
    return value < min ? min : (value > max ? max : value);
  }
};
