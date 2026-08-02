/* Copyright Justin Mammarella jmamma@gmail.com 2018 */

#pragma once

#include <stdint.h>

#include "MCLMemory.h" // NUM_MD_TRACKS

// Elektron-style Euclidean sequencer mode. Two pulse generators (PL1/PL2)
// each spread a chosen number of pulses as evenly as possible across a
// track's steps, each with its own rotation (RO1/RO2), then get merged via
// a boolean operator (OP) and rotated together (TRO) into one trig mask.
//
// Deliberately point-query, not mask-building: euclidean_pattern_has_pulse()
// below answers "does step N have a pulse" in O(1) (a handful of integer
// multiply/divides), without ever materializing or caching a full pattern.
// Playback only ever needs one step's answer per track per tick, and
// display only needs the ~16 visible steps per redraw, so there's no hot
// path that benefits from a cached mask -- and on this RAM-constrained AVR
// target, a per-track cache (which was tried first) is exactly the kind of
// extra state that tips the .bss region over its budget. See MDSeqTrack.h
// for the small per-track knob values this reads.

enum : uint8_t {
  EUC_OP_OR = 0,
  EUC_OP_XOR,
  EUC_OP_AND,
  EUC_OP_SUB,
  EUC_OP_COUNT
};

// One slot per MD track, always resident -- so scrolling tracks (EucPage,
// like ArpPage already does for its own params) recalls each track's own
// remembered PL1/PL2/etc, not a single shared value. Originally a 1-deep
// pool shared by whichever track touched it last (evicting on switch),
// because bitfield-packed x16 still overflowed this AVR build's RAM by
// 100+ bytes at the time -- but that was before this fork's other RAM/
// flash work (see MCL_AVR_RAM_BUDGET-style margin checks throughout this
// session) freed up real headroom, and per-track storage no longer needs
// a `track` field to know which track a slot belongs to (the array index
// *is* the track number) or the eviction bookkeeping that came with
// sharing one slot, so the extra 15 tracks' worth costs less than 16x the
// original 5 bytes would suggest. `enabled` gates playback/display per
// track -- see MDSeqTrack::effective_trig() (MDSeqTrack.h) -- independent
// tracks can be armed at once now (nothing forces mutual exclusion
// anymore); turning EUC off for one track never touches another's.
constexpr uint8_t EUC_NUM_SLOTS = NUM_MD_TRACKS;

// Bitfields -- every byte matters here, see the RAM discussion above.
// enabled(1)+pl1(7)+pl2(7)+ro1(6)+ro2(6)+tro(6)+op(2) = 35 bits, 5 bytes.
struct EucSlot {
  uint8_t enabled : 1;
  uint8_t pl1 : 7;
  uint8_t pl2 : 7;
  uint8_t ro1 : 6;
  uint8_t ro2 : 6;
  uint8_t tro : 6;
  uint8_t op : 2;
};

extern EucSlot euc_slots[EUC_NUM_SLOTS];

// Resets every track's slot to defaults (pl1=4/pl2=3/rest 0/enabled=
// false). Static storage already zero-inits everything except pl1/pl2, so
// this only really needs to run once at boot (see MCL::setup(), MCL.cpp)
// -- kept as an explicit step rather than relying on zero-init because
// pl1/pl2 default to nonzero (an all-zero pattern has no pulses at all,
// a confusing thing to land on the first time EUC is touched).
void euc_slots_init();

// `track`'s slot (nullptr only if `track` is out of range -- every real
// track index always has one, see EUC_NUM_SLOTS above). A slot being
// "there" does NOT imply `enabled` -- check that separately (or via
// effective_trig()).
EucSlot *euc_slot_find(uint8_t track);

// True if a single generator spreading `pulses` evenly across `length`
// steps places one at `step`. O(1): finds the one pulse index that could
// possibly land on `step` (via the same "i*length/pulses" even-distribution
// formula euclidean_pattern_has_pulse's callers rely on) and checks it,
// rather than walking every pulse.
bool euclidean_generator_has_pulse(uint8_t step, uint8_t pulses,
                                    uint8_t length);

// Full pipeline for one step: generate both pulse trains (each rotated by
// ro1/ro2 individually), combine via op, then account for the combined
// result being rotated by tro. Mirrors the exact order Elektron describes:
// R01/R02 rotate each generator's own pulses, TRO rotates the merged track
// afterward.
bool euclidean_pattern_has_pulse(uint8_t step, uint8_t pl1, uint8_t pl2,
                                  int8_t ro1, int8_t ro2, uint8_t op,
                                  int8_t tro, uint8_t length);
