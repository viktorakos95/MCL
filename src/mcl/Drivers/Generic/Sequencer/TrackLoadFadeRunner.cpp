/* Copyright 2026, Justin Mammarella jmamma@gmail.com */

#include "TrackLoadFadeRunner.h"
#include "MidiClock.h"
#include "Grid/TrackLoadFade.h"
#include <string.h>

using LoadFadeMask = uint32_t;

static_assert(NUM_SLOTS <= sizeof(LoadFadeMask) * 8,
              "TrackLoadFadeRunner active_mask is too narrow for NUM_SLOTS");

namespace {

constexpr uint8_t LOAD_FADE_RUNTIME_STARTED = 0x80;
// Loaded while stopped: the fade waits for PLAY.
constexpr uint8_t LOAD_FADE_RUNTIME_WAIT_START = 0x40;
#if !MCL_FEATURE_HOST_LOAD_FADE_SEEK
// Loaded while stopped: its starting level goes out once the kit has (prime).
constexpr uint8_t LOAD_FADE_RUNTIME_PRIME = 0x20;
#endif

class ATTR_PACKED() LoadFadeState {
public:
  TrackLoadFadeTarget target;
  // A fade left this slot's track away from its level: 1 part-way (cut
  // short by a new load or STOP, last_value is where), 2 faded out. The
  // target, this and last_value stay when the slot is cleared; the slot's
  // next load puts the level back, and a fade it starts goes on from a
  // part-way level.
  uint8_t touched;
  uint8_t last_value;
#if MCL_FEATURE_HOST_LOAD_FADE_SEEK
  uint16_t count_down;
  uint16_t elapsed_ticks;
  uint16_t duration_ticks;
#else
  uint16_t elapsed_q12;
  uint16_t duration_q12;
  // The level's stepper: the range is added once a tick, and the level moves
  // one unit each time this passes the duration.
  uint16_t remainder;
#endif
  uint8_t flags;
  uint8_t amount;
  uint8_t start_value;
  uint8_t end_value;
#if MCL_FEATURE_HOST_LOAD_FADE_SEEK
  int8_t curve;
#endif

  void clear() {
    target.track_type = 0;
    target.device_idx = 0;
    target.track_number = 0;
    target.param = 0;
#if MCL_FEATURE_HOST_LOAD_FADE_SEEK
    count_down = 0;
    elapsed_ticks = 0;
    duration_ticks = 0;
#else
    elapsed_q12 = 0;
    duration_q12 = 0;
    remainder = 0;
#endif
    flags = 0;
    amount = 0;
    start_value = 0;
    end_value = 0;
    last_value = 255;
#if MCL_FEATURE_HOST_LOAD_FADE_SEEK
    curve = 0;
#endif
  }
};

LoadFadeState fade_states[NUM_SLOTS];
LoadFadeMask active_mask = 0;

#if MCL_FEATURE_HOST_LOAD_FADE_SEEK
uint8_t clamp_7bit(int16_t value) {
  if (value < 0) {
    return 0;
  }
  if (value > 127) {
    return 127;
  }
  return (uint8_t)value;
}

uint8_t fade_curve_phase(uint8_t phase, int8_t curve) {
  uint16_t lin = phase > 127 ? 127 : phase;
  uint16_t expo = (uint16_t)((lin * lin + 63u) / 127u);
  uint16_t inv_base = (uint16_t)(127u - lin);
  uint16_t inv =
      (uint16_t)(127u - (inv_base * inv_base + 63u) / 127u);
  uint16_t mix = curve < 0 ? (uint16_t)(-curve) : (uint16_t)curve;
  if (mix > 127) {
    mix = 127;
  }
  uint16_t shaped = curve > 0 ? expo : (curve < 0 ? inv : lin);
  uint16_t out =
      (uint16_t)((lin * (127u - mix) + shaped * mix + 63u) / 127u);
  return out > 127 ? 127 : (uint8_t)out;
}

int8_t fade_effective_curve(int8_t curve, uint8_t start_value,
                            uint8_t end_value) {
  int16_t out = curve;
  if (end_value < start_value) {
    out = -out;
  }
  if (out < -127) {
    out = -127;
  }
  if (out > 127) {
    out = 127;
  }
  return (int8_t)out;
}

uint16_t clamp_u16(uint32_t value) {
  return value > 0xFFFFu ? 0xFFFFu : (uint16_t)value;
}

uint16_t load_fade_count_down(uint32_t start_clock) {
  uint32_t now = MidiClock.div192th_counter;
  uint32_t to_start = MidiClock.clock_diff_div192(now, start_clock);
  if (to_start == 0) {
    return 0;
  }

  uint32_t ticks_per_16th = MidiClock.div192th_ticks_per_16th();
  uint32_t cycle = 0x10000UL * ticks_per_16th;
  if (to_start > (cycle / 2u)) {
    return 0;
  }

  return clamp_u16(to_start);
}

uint16_t load_fade_elapsed_since_start(uint32_t start_clock) {
  uint32_t now = MidiClock.div192th_counter;
  uint32_t to_start = MidiClock.clock_diff_div192(now, start_clock);
  uint32_t ticks_per_16th = MidiClock.div192th_ticks_per_16th();
  uint32_t cycle = 0x10000UL * ticks_per_16th;
  if (to_start != 0 && to_start <= (cycle / 2u)) {
    return 0;
  }
  return clamp_u16(MidiClock.clock_diff_div192(start_clock, now));
}

uint16_t load_fade_duration_ticks(uint16_t duration_q12) {
  uint32_t ticks_per_16th = MidiClock.div192th_ticks_per_16th();
  uint32_t ticks =
      ((uint32_t)duration_q12 * ticks_per_16th + 11u) / 12u;
  return clamp_u16(ticks == 0 ? 1u : ticks);
}

uint16_t load_fade_elapsed_ticks(uint16_t elapsed_q12) {
  if (elapsed_q12 == 0) {
    return 0;
  }
  uint32_t ticks_per_16th = MidiClock.div192th_ticks_per_16th();
  uint32_t ticks =
      ((uint32_t)elapsed_q12 * ticks_per_16th + 11u) / 12u;
  return clamp_u16(ticks);
}
#else
uint16_t load_fade_delay_q12(uint32_t start_clock) {
  uint32_t now = MidiClock.div192th_counter;
  uint32_t to_start = MidiClock.clock_diff_div192(now, start_clock);
  if (to_start == 0) {
    return 0;
  }

#if defined(__AVR__)
  if (to_start > (0x10000UL * 12u / 2u)) {
    return 0;
  }
  // The slot's sequence switches, and sends its first trig, in the pass that
  // brings the counter to start_clock - 1 (GridTrack::transition_load), and
  // a fade's first level goes out in the pass after its wait ends. The wait
  // is three ticks short, so that level is out one pass before the switch:
  // a fade-in is at its floor before the new slot's first trig.
  const uint16_t wait = to_start > 0xFFFFu ? 0xFFFFu : (uint16_t)to_start;
  return wait > 3 ? wait - 3 : 0;
#else
  uint32_t ticks_per_16th = MidiClock.div192th_ticks_per_16th();
  uint32_t cycle = 0x10000UL * ticks_per_16th;
  if (to_start > (cycle / 2u)) {
    return 0;
  }

  uint32_t delay_q12 =
      (to_start * 12u + ticks_per_16th - 1u) / ticks_per_16th;
  return delay_q12 > 0xFFFFu ? 0xFFFFu : (uint16_t)delay_q12;
#endif
}
#endif

LoadFadeMask clear_slot(GridSlot slot) {
  if (slot >= NUM_SLOTS) {
    return 0;
  }
  LoadFadeMask bit = (LoadFadeMask)1 << slot;
  active_mask &= ~bit;
  // Everything but the target, the touched mark and the last level, which
  // the slot's next load still needs.
  constexpr size_t kept = sizeof(TrackLoadFadeTarget) + 2;
  memset((uint8_t *)&fade_states[slot] + kept, 0,
         sizeof(LoadFadeState) - kept);
  return bit;
}

bool begin_slot(GridSlot slot, LoadFadeState &state) {
  uint8_t current = 0;
  if (!read_track_load_fade_value(state.target, &current)) {
    clear_slot(slot);
    return false;
  }

  uint8_t floor = current > state.amount ? current - state.amount : 0;
  if (state.flags & TRACK_LOAD_FADE_FLAG_OUT) {
    state.start_value = current;
    state.end_value = floor;
  } else {
    state.start_value = floor;
    state.end_value = current;
  }
  if (state.touched == 1 && state.last_value <= 127) {
    // An earlier fade left the track part-way: go on from there.
    state.start_value = state.last_value;
  } else {
    state.last_value = 255;
  }
  state.flags |= LOAD_FADE_RUNTIME_STARTED;
  state.touched = 1;
  return true;
}

bool write_slot_value(GridSlot slot,
                      LoadFadeState &state,
                      MidiUartClass *uart,
                      MidiUartClass *uart2,
                      bool advance) {
#if MCL_FEATURE_HOST_LOAD_FADE_SEEK
  uint16_t elapsed = state.elapsed_ticks;
  uint16_t duration = state.duration_ticks;
  bool complete = elapsed >= duration;
  uint8_t phase = complete
                      ? 127
                      : (uint8_t)(((uint32_t)elapsed * 127u) / duration);
  phase = fade_curve_phase(
      phase, fade_effective_curve(state.curve, state.start_value,
                                  state.end_value));
  int16_t value =
      (int16_t)state.start_value +
      ((int16_t)state.end_value - (int16_t)state.start_value) * phase / 127;
  uint8_t value7 = complete ? state.end_value : clamp_7bit(value);
#else
  // A straight line from start_value to end_value over duration_q12 ticks,
  // stepped as the LFO steps its phase: no multiply or divide in the
  // sequencer interrupt. Each tick adds the range to a remainder, and the
  // level moves one unit towards its end each time the remainder passes the
  // duration, so it arrives exactly on the last tick. last_value is the
  // level of the tick before (every change is written).
  const uint8_t end = state.end_value;
  const bool complete = state.elapsed_q12 >= state.duration_q12;
  uint8_t value7 = end;
  if (!complete) {
    const uint8_t start = state.start_value;
    value7 = start;
    if (state.elapsed_q12 != 0) {
      value7 = state.last_value;
      uint16_t remainder =
          state.remainder + (end > start ? end - start : start - end);
      while (remainder >= state.duration_q12) {
        remainder -= state.duration_q12;
        if (value7 < end) {
          value7++;
        } else if (value7 > end) {
          value7--;
        }
      }
      state.remainder = remainder;
    }
  }
#endif

  if (value7 != state.last_value) {
    write_track_load_fade_value(state.target, value7, uart, uart2);
    state.last_value = value7;
  }

  if (complete) {
    if ((state.flags & TRACK_LOAD_FADE_FLAG_THEN_IN) != 0) {
      // OUTIN: the held slot switches now; it fades in over the same length
      // from where the fade-out ended.
      state.flags = TRACK_LOAD_FADE_FLAG_ENABLED;
#if MCL_FEATURE_HOST_LOAD_FADE_SEEK
      state.count_down = 0;
      state.elapsed_ticks = 0;
#else
      state.elapsed_q12 = 0;
      state.remainder = 0;
#endif
      return true;
    }
    // Back at its level after an IN; faded out after an OUT.
    state.touched = (state.flags & TRACK_LOAD_FADE_FLAG_OUT) ? 2 : 0;
    clear_slot(slot);
    return false;
  }

  if (advance
#if MCL_FEATURE_HOST_LOAD_FADE_SEEK
      && state.elapsed_ticks < 0xFFFFu
#else
      && state.elapsed_q12 < 0xFFFFu
#endif
      ) {
#if MCL_FEATURE_HOST_LOAD_FADE_SEEK
    state.elapsed_ticks++;
#else
    state.elapsed_q12++;
#endif
  }
  return true;
}

void tick_slot(GridSlot slot, MidiUartClass *uart, MidiUartClass *uart2) {
  LoadFadeState &state = fade_states[slot];
  // A level restore runs as a fade with no amount: one write of the level.
  if ((state.flags & TRACK_LOAD_FADE_FLAG_ENABLED) == 0 ||
#if MCL_FEATURE_HOST_LOAD_FADE_SEEK
      state.duration_ticks == 0) {
#else
      state.duration_q12 == 0) {
#endif
    clear_slot(slot);
    return;
  }

  // Loaded while stopped: the fade starts with the first tick after PLAY.
  if ((state.flags & LOAD_FADE_RUNTIME_WAIT_START) != 0) {
    state.flags &= (uint8_t)~LOAD_FADE_RUNTIME_WAIT_START;
#if MCL_FEATURE_HOST_LOAD_FADE_SEEK
    state.count_down = 0;
#else
    state.elapsed_q12 = 0;
#endif
  }

  if ((state.flags & LOAD_FADE_RUNTIME_STARTED) == 0) {
#if MCL_FEATURE_HOST_LOAD_FADE_SEEK
    if (state.count_down > 0) {
      state.count_down--;
      return;
    }
#else
    if (state.elapsed_q12 > 0) {
      state.elapsed_q12--;
      return;
    }
#endif

    if (!begin_slot(slot, state)) {
      return;
    }
  }

  write_slot_value(slot, state, uart, uart2, true);
}

} // namespace

void TrackLoadFadeRunner::clear(bool preserve_armed_prestart) {
  // A slot keeps its target, so a track a fade left away from its level
  // gets it back on its next load.
#if !MCL_FEATURE_HOST_LOAD_FADE_SEEK
  USE_LOCK();
  SET_LOCK();
#endif
  for (uint8_t n = 0; n < NUM_SLOTS; n++) {
    if (preserve_armed_prestart &&
        (fade_states[n].flags & LOAD_FADE_RUNTIME_WAIT_START) != 0) {
      continue;
    }
    clear_slot(n);
  }
#if !MCL_FEATURE_HOST_LOAD_FADE_SEEK
  CLEAR_LOCK();
#endif
}

#if !MCL_FEATURE_HOST_LOAD_FADE_SEEK
// The sequencer interrupt ticks these slots while the main loop starts a
// fade; it must not see one half set up.
void TrackLoadFadeRunner::start(GridSlot slot,
                                const TrackLoadFadeTarget &target,
                                const TrackLoadFadeData *fade,
                                uint32_t start_clock, MidiUartClass *uart,
                                MidiUartClass *uart2) {
  USE_LOCK();
  SET_LOCK();
  start_unlocked(slot, target, fade, start_clock, uart, uart2);
  CLEAR_LOCK();
}
#endif

void TrackLoadFadeRunner::start_unlocked(GridSlot slot,
                                const TrackLoadFadeTarget &target,
                                const TrackLoadFadeData *fade,
                                uint32_t start_clock
#if MCL_FEATURE_HOST_LOAD_FADE_SEEK
                                ,
                                bool allow_prestart
#endif
                                ,
                                MidiUartClass *uart,
                                MidiUartClass *uart2) {
  if (slot >= NUM_SLOTS) {
    return;
  }

  LoadFadeState &state = fade_states[slot];
  if ((state.flags & TRACK_LOAD_FADE_FLAG_THEN_IN) != 0 &&
      (fade == nullptr || (fade->flags & TRACK_LOAD_FADE_FLAG_THEN_IN) == 0)) {
    // OUTIN holds this slot: its switch arrives a little early, and the
    // fade-out goes on and becomes the fade-in. Only a new OUTIN replaces it.
    return;
  }
  LoadFadeMask bit = clear_slot(slot);
  const bool transport_started = MidiClock.state == MidiClockClass::STARTED;
#if !MCL_FEATURE_HOST_LOAD_FADE_SEEK
  // A load while stopped arms its fade; it starts on PLAY.
  const bool allow_prestart = true;
#endif
  const bool fading = fade != nullptr && fade->enabled() &&
                      (transport_started || allow_prestart);

  if (fading) {
    state.target = target;
    state.flags = fade->flags & (TRACK_LOAD_FADE_FLAG_ENABLED |
                                 TRACK_LOAD_FADE_FLAG_OUT |
                                 TRACK_LOAD_FADE_FLAG_THEN_IN);
    state.amount = fade->amount > 127 ? 127 : fade->amount;
#if MCL_FEATURE_HOST_LOAD_FADE_SEEK
    state.curve = fade->curve;
    state.duration_ticks = load_fade_duration_ticks(fade->duration_q12);
    uint32_t elapsed_ticks = load_fade_elapsed_ticks(fade->elapsed_q12());
    if (transport_started) {
      elapsed_ticks += load_fade_elapsed_since_start(start_clock);
    }
    state.elapsed_ticks = clamp_u16(elapsed_ticks);
    if (state.elapsed_ticks > state.duration_ticks) {
      state.elapsed_ticks = state.duration_ticks;
    }
#else
    state.duration_q12 = fade->duration_q12;
#endif
  } else if (state.touched) {
    // An earlier fade left this track away from its level (an OUT, or a fade
    // cut short by STOP), and this load gives it back: a fade with no amount,
    // one write of the level at the load point.
    state.flags = TRACK_LOAD_FADE_FLAG_ENABLED;
#if MCL_FEATURE_HOST_LOAD_FADE_SEEK
    state.duration_ticks = 1;
#else
    state.duration_q12 = 1;
#endif
  } else {
    return;
  }
#if MCL_FEATURE_HOST_LOAD_FADE_SEEK
  state.count_down = transport_started ? load_fade_count_down(start_clock) : 0;
#else
  if (transport_started) {
    state.elapsed_q12 = load_fade_delay_q12(start_clock);
  }
#endif
  if (!transport_started) {
    // Stopped: the fade waits for PLAY.
    state.flags |= LOAD_FADE_RUNTIME_WAIT_START;
#if MCL_FEATURE_HOST_LOAD_FADE_SEEK
    if (state.count_down == 0 && begin_slot(slot, state)) {
      write_slot_value(slot, state, uart, uart2, false);
    }
#else
    // The load sends the kit after this; prime() then sets the level.
    (void)uart;
    (void)uart2;
    state.flags |= LOAD_FADE_RUNTIME_PRIME;
#endif
  }
  active_mask |= bit;
}

bool TrackLoadFadeRunner::silenced(GridSlot slot) {
  if (slot >= NUM_SLOTS) {
    return false;
  }
  // Faded out, or on its way there (OUTIN's fade-out comes back in).
  const uint8_t flags = fade_states[slot].flags &
                        (TRACK_LOAD_FADE_FLAG_ENABLED |
                         TRACK_LOAD_FADE_FLAG_OUT | TRACK_LOAD_FADE_FLAG_THEN_IN);
  return fade_states[slot].touched == 2 ||
         flags == (TRACK_LOAD_FADE_FLAG_ENABLED | TRACK_LOAD_FADE_FLAG_OUT);
}

void TrackLoadFadeRunner::release(GridSlot slot) {
  if (slot >= NUM_SLOTS) {
    return;
  }
  USE_LOCK();
  SET_LOCK();
  fade_states[slot].flags &= (uint8_t)~TRACK_LOAD_FADE_FLAG_THEN_IN;
  CLEAR_LOCK();
}

#if !MCL_FEATURE_HOST_LOAD_FADE_SEEK
void TrackLoadFadeRunner::prime(MidiUartClass *uart, MidiUartClass *uart2) {
  for (uint8_t slot = 0; slot < NUM_SLOTS; slot++) {
    LoadFadeState &state = fade_states[slot];
    if ((state.flags & LOAD_FADE_RUNTIME_PRIME) == 0) {
      continue;
    }
    state.flags &= (uint8_t)~LOAD_FADE_RUNTIME_PRIME;
    uint8_t level;
    if (!read_track_load_fade_value(state.target, &level)) {
      continue;
    }
    // Silent for an IN; its own level for an OUT, which falls from it on
    // PLAY, and for a level restore (no amount), which is then done.
    if (state.amount == 0) {
      state.touched = 0;
      clear_slot(slot);
    } else if ((state.flags & TRACK_LOAD_FADE_FLAG_OUT) == 0) {
      level = level > state.amount ? level - state.amount : 0;
      state.touched = 1;
    }
    state.last_value = level;
    write_track_load_fade_value(state.target, level, uart, uart2);
  }
}
#endif

void TrackLoadFadeRunner::tick(MidiUartClass *uart, MidiUartClass *uart2) {
  if (active_mask == 0) {
    return;
  }
  if (MidiClock.state != MidiClockClass::STARTED) {
    // Stopped: running fades end; those loaded while stopped wait for PLAY.
    clear(true);
    return;
  }
  LoadFadeMask bit = 1;
  for (uint8_t slot = 0; slot < NUM_SLOTS; slot++, bit <<= 1) {
    if ((active_mask & bit) != 0) {
      tick_slot(slot, uart, uart2);
    }
  }
}
