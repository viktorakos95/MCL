/* Copyright Justin Mammarella jmamma@gmail.com 2018 */

#pragma once

#include "MCLMemory.h"

// Grid page "beat repeat": hold LEFT+RIGHT, then hold a trig pad to repeat
// that MD track's sound at a chosen subdivision, independent of the MD
// sequencer's own step position. While LEFT+RIGHT are held, UP/DOWN cycles
// the subdivision. The chosen subdivision is remembered per project (see
// BEAT_REPEAT_RESERVED_RATE_IDX in Project.h / mcl_cfg.beat_repeat_rate in
// MCLSysConfig.h).

class MidiUartClass;

enum : uint8_t {
  BEAT_REPEAT_RATE_1_4 = 0,
  BEAT_REPEAT_RATE_1_4T,
  BEAT_REPEAT_RATE_1_8,
  BEAT_REPEAT_RATE_1_8T,
  BEAT_REPEAT_RATE_1_16,
  BEAT_REPEAT_RATE_1_16T,
  BEAT_REPEAT_RATE_1_32,
  BEAT_REPEAT_RATE_1_32T,
  BEAT_REPEAT_RATE_1_64,
  BEAT_REPEAT_RATE_1_64T,
  BEAT_REPEAT_RATE_COUNT
};

// Falls back to 0 for any out-of-range value (e.g. an unrecognised byte
// read back from ProjectHeader.reserved[]).
uint8_t beat_repeat_normalized_rate(uint8_t rate);

// Wraps into [0, BEAT_REPEAT_RATE_COUNT) rather than clamping, so UP/DOWN
// can cycle past either end back to the other side.
uint8_t beat_repeat_cycle_rate(uint8_t rate, bool increase);

// Copies the rate's short display name (e.g. "1/16T") into dst, which must
// be at least 6 bytes (5 chars + NUL). Name lives in PROGMEM.
void beat_repeat_rate_name(uint8_t rate, char *dst, uint8_t dst_size);

// Called once per real MIDI clock tick (24ppqn) from MCLSeq::seq(),
// unconditionally (independent of manual-step mode) so beat repeat keeps
// working regardless of whether the MD tracks are clock- or CC-advanced.
void beat_repeat_tick(MidiUartClass *uart);

// True while `track`'s pad is currently held for the roll. MDSeqTrack::seq()
// checks this to skip that track's own normal step trigger while it's being
// rolled, so holding the pad replaces the sequenced pattern with the roll
// instead of layering the two -- otherwise the track's own programmed hits
// keep landing underneath the manually-retriggered ones, audible as
// flamming/a background beat that doesn't match what's actually held.
bool beat_repeat_bypasses_track(uint8_t track);
