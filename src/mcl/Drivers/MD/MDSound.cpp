#include "MDSound.h"
#include "MD.h"
#include "MCLSd.h"

bool MDSound::write_sound() {
  DEBUG_PRINT_FN();
  bool ret;
  version = SOUND_VERSION;
  ret = write_data(this, sizeof(MDSoundData), 0);
  return ret;
}

bool MDSound::read_sound() {
  DEBUG_PRINT_FN();
  bool ret;
  // The version 2000 layout: header, two 24-parameter records, sample names.
  static constexpr uint16_t v1_size =
      offsetof(MDSoundData, machine1) + 2 * sizeof(MDMachineV1) +
      sizeof(sample_id1) + sizeof(sample_id2);
  uint8_t *bytes = reinterpret_cast<uint8_t *>(static_cast<MDSoundData *>(this));
  ret = read_data(bytes, v1_size, 0);
  if (!ret) {
    return false;
  }
  if (version == SOUND_VERSION) {
    return read_data(bytes + v1_size, sizeof(MDSoundData) - v1_size, v1_size);
  }
  // Widen from the back so nothing is overwritten before it has moved.
  uint8_t *old = reinterpret_cast<uint8_t *>(&machine1);
  memmove(sample_id1, old + 2 * sizeof(MDMachineV1),
          sizeof(sample_id1) + sizeof(sample_id2));
  memmove(&machine2, old + sizeof(MDMachineV1), sizeof(MDMachineV1));
  machine2.upgrade_v1();
  machine1.upgrade_v1();
  return true;
}

// Copies kit track `from` into one machine of a sound, stored as the sound's
// slot `slot`. The LFO destination is kept as a slot: 0 for track `first`,
// 1 for track `second`; an LFO aimed at any other track is dropped, with its
// depth.
static void fetch_machine(MDMachine &machine, uint8_t from, uint8_t first,
                          uint8_t second, uint8_t slot) NOINLINE();
static void fetch_machine(MDMachine &machine, uint8_t from, uint8_t first,
                          uint8_t second, uint8_t slot) {
  machine.model = MD.kit.models[from]; //get raw model including tonal bit
  machine.level = MD.kit.levels[from];
  memcpy(&machine.params, &(MD.kit.params[from]), SPS_PARAMS_PER_TRACK);
  MD.kit.get_ext(from, &machine);
  MD.kit.get_lfo(from, &machine.lfo);

  if (machine.lfo.destinationTrack == first) {
    machine.lfo.destinationTrack = 0;
  } else if (machine.lfo.destinationTrack == second) {
    machine.lfo.destinationTrack = 1;
  } else {
    machine.lfo.destinationTrack = 255;
    machine.lfo.depth = machine.params[MODEL_LFOD] = 0;
  }

  machine.track = slot;
  machine.normalize_level();
}

bool MDSound::fetch_sound(uint8_t track) {
  DEBUG_PRINT_FN();
  uint8_t trigGroup = MD.kit.trigGroups[track];
  fetch_machine(machine1, track, track, trigGroup, 0);
  machine_count = 1;

  // If track uses trigGroup, assume sound is made up of two models.

  if ((trigGroup < 16) && (trigGroup != track)) {
    fetch_machine(machine2, trigGroup, track, trigGroup, 1);
    machine_count = 2;
  }
  return true;
}

bool MDSound::load_sound(uint8_t track) {
  DEBUG_PRINT_FN();

  DEBUG_PRINTLN(machine1.model);
#ifdef DEBUG_MODE
  char str[3] = "  ";
  const char* tmp = getMDMachineNameShort(machine1.model, 2);
  copyMachineNameShort(tmp, str);
  DEBUG_PRINTLN(str);
#endif

  bool send_level = true, send = true;
  if ((machine_count > 1) && (track != 15)) {
#ifdef DEBUG_MODE
    DEBUG_PRINTLN(F("loading second machine"));
    tmp = getMDMachineNameShort(machine2.model, 2);
    copyMachineNameShort(tmp, str);
    DEBUG_PRINTLN(str);
#endif
    if (machine2.lfo.destinationTrack < 16) {
      machine2.lfo.destinationTrack += track;
    }
    machine1.trigGroup = track + 1;
    machine2.trigGroup = track;

    MD.sendMachine(track + 1, &machine2, send_level, send);

  } else {
    machine1.trigGroup = track;
  }

  if (machine1.lfo.destinationTrack < 16) {
    machine1.lfo.destinationTrack += track;
  }

  if (machine_count == 1) {
    machine1.trigGroup = track;
  } else {
    machine1.trigGroup = track + 1;
  }

  MD.sendMachine(track, &machine1, send_level, send);
  return true;
}

bool MDSound::read_data(void *data, uint32_t size, uint32_t position) {
  DEBUG_PRINT_FN();

  // DEBUG_PRINTLN(size);
  // DEBUG_PRINTLN(position);
  bool ret;

  ret = file.seekSet(position);
  if (!ret) {
    DEBUG_PRINTLN(F("could not seek"));
    DEBUG_PRINTLN(position);
    DEBUG_PRINTLN((uint32_t)file.fileSize());
    return false;
  }
  if (!file.isOpen()) {
    DEBUG_PRINTLN(F("file not open"));
    return false;
  }
  ret = mcl_sd.read_data(data, size, &file);
  return ret;
}

bool MDSound::write_data(void *data, uint32_t size, uint32_t position) {
  DEBUG_PRINT_FN();
  //  DEBUG_PRINTLN(size);
  //  DEBUG_PRINTLN(position);
  //  DEBUG_PRINTLN(file.fileSize());
  bool ret = false;

  ret = file.seekSet(position);

  if (!ret) {
    DEBUG_PRINTLN(F("could not seek"));
    DEBUG_PRINTLN(position);
    DEBUG_PRINTLN((uint32_t)file.fileSize());
    return false;
  }

  ret = mcl_sd.write_data(data, size, &file);
  return ret;
}
