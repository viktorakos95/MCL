/* Copyright 2026, Justin Mammarella jmamma@gmail.com */

#pragma once

#include "../../../MCL/MCLMemory.h"
#include "../../../MCL/MCLPlatformFeatures.h"
#include "Grid/TrackLoadFadeTarget.h"
#include "platform.h"
#include <inttypes.h>

class MidiUartClass;
class TrackLoadFadeData;

// Generic per-load fade engine. Owns timing/curve/runtime only — destination
// dispatch (read of the current parameter value, write of new values) is
// delegated to the TrackLoadFadeTarget bridge so the runner stays free of
// device coupling.
class TrackLoadFadeRunner {
public:
  // Clears every active fade, or every one but those waiting for PLAY. A
  // track a fade left away from its level gets it back on its next load.
  static void clear(bool preserve_armed_prestart = false);

  // Always clears any state previously held for `slot`. If `fade` is null,
  // transport isn't STARTED, or the fade is disabled, returns after clearing
  // so no new fade is scheduled. Otherwise schedules a new fade using
  // `target` as the destination.
  static void start(GridSlot slot,
                    const TrackLoadFadeTarget &target,
                    const TrackLoadFadeData *fade,
                    uint32_t start_clock
#if MCL_FEATURE_HOST_LOAD_FADE_SEEK
                    ,
                    bool allow_prestart = false
#endif
                    ,
                    MidiUartClass *uart = nullptr,
                    MidiUartClass *uart2 = nullptr)
#if MCL_FEATURE_HOST_LOAD_FADE_SEEK
  {
    start_unlocked(slot, target, fade, start_clock, allow_prestart, uart,
                   uart2);
  }
#else
      ;
#endif

#if !MCL_FEATURE_HOST_LOAD_FADE_SEEK
  // After a load made while stopped has sent its kit: each fade it armed
  // sets its track's starting level (silent for an IN), and a track an
  // earlier fade left away from its level gets it back.
  static void prime(MidiUartClass *uart, MidiUartClass *uart2);
#endif

  // start() without the interrupt lock the AVR build takes around it.
  static void start_unlocked(GridSlot slot, const TrackLoadFadeTarget &target,
                             const TrackLoadFadeData *fade,
                             uint32_t start_clock
#if MCL_FEATURE_HOST_LOAD_FADE_SEEK
                             ,
                             bool allow_prestart
#endif
                             ,
                             MidiUartClass *uart, MidiUartClass *uart2);

  // Advances every active fade by one tick. Output is written through the
  // TrackLoadFadeTarget bridge.
  static void tick(MidiUartClass *uart, MidiUartClass *uart2);
};
