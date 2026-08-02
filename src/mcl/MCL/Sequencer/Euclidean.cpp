/* Copyright Justin Mammarella jmamma@gmail.com 2018 */

#include "Sequencer/Euclidean.h"

EucSlot euc_slots[EUC_NUM_SLOTS];

EucSlot *euc_slot_find(uint8_t track) {
  return track < EUC_NUM_SLOTS ? &euc_slots[track] : nullptr;
}

void euc_slots_init() {
  for (uint8_t i = 0; i < EUC_NUM_SLOTS; i++) {
    EucSlot &slot = euc_slots[i];
    slot.enabled = false;
    slot.pl1 = 4;
    slot.pl2 = 3;
    slot.ro1 = 0;
    slot.ro2 = 0;
    slot.tro = 0;
    slot.op = EUC_OP_OR;
  }
}

namespace {
uint8_t clamp_length(uint8_t length) { return length > 64 ? 64 : length; }

// Rotating a pattern forward by `amount` moves the pulse that was at index
// P to index (P + amount) mod length. So querying "does the rotated
// pattern have a pulse at S" is the same as asking the un-rotated pattern
// about (S - amount) mod length.
uint8_t rotate_index(uint8_t step, int8_t amount, uint8_t length) {
  if (length == 0) {
    return step;
  }
  int16_t idx = (int16_t)step - amount;
  idx %= (int16_t)length;
  if (idx < 0) {
    idx += length;
  }
  return (uint8_t)idx;
}
} // namespace

bool euclidean_generator_has_pulse(uint8_t step, uint8_t pulses,
                                    uint8_t length) {
  length = clamp_length(length);
  if (length == 0 || pulses == 0 || step >= length) {
    return false;
  }
  if (pulses > length) {
    pulses = length;
  }

  // The generator places pulse i at floor(i*length/pulses) for i in
  // [0, pulses). That mapping is non-decreasing in i, so at most one pulse
  // index can land on `step`: the smallest i with floor(i*length/pulses)
  // >= step, i.e. i0 = ceil(step*pulses/length). Check that one directly
  // instead of scanning all `pulses` candidates.
  uint16_t i0 = ((uint16_t)step * pulses + length - 1) / length;
  if (i0 >= pulses) {
    return false;
  }
  uint16_t mapped_step = (uint16_t)i0 * length / pulses;
  return mapped_step == step;
}

bool euclidean_pattern_has_pulse(uint8_t step, uint8_t pl1, uint8_t pl2,
                                  int8_t ro1, int8_t ro2, uint8_t op,
                                  int8_t tro, uint8_t length) {
  length = clamp_length(length);
  if (length == 0 || step >= length) {
    return false;
  }

  uint8_t s = rotate_index(step, tro, length);
  bool g1 = euclidean_generator_has_pulse(rotate_index(s, ro1, length), pl1,
                                          length);
  bool g2 = euclidean_generator_has_pulse(rotate_index(s, ro2, length), pl2,
                                          length);
  switch (op) {
  case EUC_OP_XOR:
    return g1 ^ g2;
  case EUC_OP_AND:
    return g1 && g2;
  case EUC_OP_SUB:
    return g1 && !g2;
  case EUC_OP_OR:
  default:
    return g1 || g2;
  }
}
