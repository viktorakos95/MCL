/* Copyright Justin Mammarella jmamma@gmail.com 2018 */

#include "Sequencer/BeatRepeat.h"

#include "GUI/PageIndex.h"
#include "GUI/Pages/CommonPages.h"
#include "GUI/Pages/Sequencer/SeqPage.h"
#include "KeyInterface.h"
#include "MCL.h"
#include "MCLSysConfig.h"
#include "MidiClock.h"
#include "Sequencer/SeqPtcTrackRef.h"
#include "Sequencer/SeqTrack.h"
#include "../../Drivers/DeviceContext.h"
#include "../../Drivers/MD/MD.h"

#include <avr/pgmspace.h>

namespace {

// Values are in MidiClock.div192th_counter's own unit. Despite the name,
// MidiClockClass::div192th_ticks_per_16th() returns 12 on AVR (not 48), so
// a quarter note is 12*4 = 48 units here, not 192 — using the wrong scale
// here previously made every rate fire 4x slower than intended. 48 units/
// quarter is still fine-grained enough for every rate below (including
// 1/64 and the triplets) to have an exact whole-number period, unlike the
// raw 24ppqn tick (1/64 would be 1.5 ticks).
//
// Firing off MidiClock's own running position (rather than a counter this
// feature starts from 0 whenever the gesture begins) means repeats always
// land exactly on the same absolute grid the rest of the sequencer already
// uses, instead of potentially landing a few milliseconds off a note
// that's already scheduled on that same step — audible as two
// near-simultaneous hits ("flamming") rather than one.
const uint8_t BEAT_REPEAT_DIV192_PERIOD[BEAT_REPEAT_RATE_COUNT] PROGMEM = {
    48, // 1/4
    32, // 1/4T
    24, // 1/8
    16, // 1/8T
    12, // 1/16
    8,  // 1/16T
    6,  // 1/32
    4,  // 1/32T
    3,  // 1/64
    2,  // 1/64T
};

const char BEAT_REPEAT_NAME_0[] PROGMEM = "1/4";
const char BEAT_REPEAT_NAME_1[] PROGMEM = "1/4T";
const char BEAT_REPEAT_NAME_2[] PROGMEM = "1/8";
const char BEAT_REPEAT_NAME_3[] PROGMEM = "1/8T";
const char BEAT_REPEAT_NAME_4[] PROGMEM = "1/16";
const char BEAT_REPEAT_NAME_5[] PROGMEM = "1/16T";
const char BEAT_REPEAT_NAME_6[] PROGMEM = "1/32";
const char BEAT_REPEAT_NAME_7[] PROGMEM = "1/32T";
const char BEAT_REPEAT_NAME_8[] PROGMEM = "1/64";
const char BEAT_REPEAT_NAME_9[] PROGMEM = "1/64T";

const char *const BEAT_REPEAT_NAMES[BEAT_REPEAT_RATE_COUNT] PROGMEM = {
    BEAT_REPEAT_NAME_0, BEAT_REPEAT_NAME_1, BEAT_REPEAT_NAME_2,
    BEAT_REPEAT_NAME_3, BEAT_REPEAT_NAME_4, BEAT_REPEAT_NAME_5,
    BEAT_REPEAT_NAME_6, BEAT_REPEAT_NAME_7, BEAT_REPEAT_NAME_8,
    BEAT_REPEAT_NAME_9,
};

constexpr uint8_t BEAT_REPEAT_VELOCITY = 100;

} // namespace

uint8_t beat_repeat_normalized_rate(uint8_t rate) {
  return rate < BEAT_REPEAT_RATE_COUNT ? rate : 0;
}

uint8_t beat_repeat_cycle_rate(uint8_t rate, bool increase) {
  rate = beat_repeat_normalized_rate(rate);
  // SYSTEM menu "ROLL TRIPLETS": off means every triplet rate (an odd index
  // — see the BEAT_REPEAT_RATE_* enum in BeatRepeat.h) is skipped, stepping
  // by 2 instead of 1 to visit only 1/4, 1/8, 1/16, 1/32, 1/64. Snap to the
  // even neighbor first in case the stored rate is a leftover triplet from
  // before the toggle was turned off — otherwise stepping by 2 from an odd
  // index would only ever land on other odd (triplet) indices.
  if (!mcl_cfg.roll_triplets) {
    rate &= ~1;
    if (increase) {
      rate = (rate + 2 >= BEAT_REPEAT_RATE_COUNT) ? 0 : rate + 2;
    } else {
      rate = (rate == 0) ? (BEAT_REPEAT_RATE_COUNT - 2) : rate - 2;
    }
    return rate;
  }
  if (increase) {
    rate++;
    if (rate >= BEAT_REPEAT_RATE_COUNT) {
      rate = 0;
    }
  } else {
    if (rate == 0) {
      rate = BEAT_REPEAT_RATE_COUNT - 1;
    } else {
      rate--;
    }
  }
  return rate;
}

void beat_repeat_rate_name(uint8_t rate, char *dst, uint8_t dst_size) {
  rate = beat_repeat_normalized_rate(rate);
  const char *name = (const char *)pgm_read_ptr(&BEAT_REPEAT_NAMES[rate]);
  strncpy_P(dst, name, dst_size - 1);
  dst[dst_size - 1] = '\0';
}

void beat_repeat_tick(MidiUartClass *uart) {
  // Tracks currently held open on the MD hardware by the roll itself (its
  // mute_state was true when we started rolling it). Resynced — not
  // "restored" — to whatever mute_state currently says once released, so
  // a mute-scene load (YES + arrow) that changes this track's real mute
  // state mid-roll is respected rather than clobbered back to a stale
  // pre-roll snapshot. See the release-resync loop below.
  static uint16_t forced_unmuted_mask = 0;

  // beat_repeat_armed already reflects the debounced LEFT+RIGHT chord (see
  // MixerPage::handleEvent()/loop()) — reusing it here (rather than
  // re-checking raw key state) keeps "shown as armed" and "actually
  // triggering" from ever disagreeing. Gated to the Primary device slot
  // since the roll is MD-specific (MD.triggerTrack() below); the Mixer
  // page's Secondary slot may be bound to a different device entirely.
  //
  // Deliberately NOT gated on mcl.current_page == MIXER_PAGE: arming still
  // only happens from MixerPage::handleEvent() (so the gesture can only
  // start there), but once armed the roll keeps running off raw key state
  // read directly below, regardless of which page is now active. This
  // matters because pressing REC forces a page switch away from Mixer to
  // the step page (stock behavior, see MCL.cpp) — without this, holding
  // the roll through a REC press would silently kill it, unlike the
  // arpeggiator, which isn't tied to any one page either.
  bool armed = mixer_page.beat_repeat_armed &&
               mixer_page.mixer_device_idx == DeviceIdx::Primary;

  if (armed && !(key_interface.is_key_down(MDX_KEY_LEFT) &&
                key_interface.is_key_down(MDX_KEY_RIGHT))) {
    // The chord was released. MixerPage's own key-release handler already
    // disarms instantly while Mixer page is still active (for immediate UI
    // feedback); this is the fallback for when it isn't — Mixer page never
    // sees the release event once REC has switched the active page away.
    mixer_page.beat_repeat_armed = false;
    armed = false;
  }

  uint16_t pad_mask = 0;
  if (armed) {
    for (uint8_t i = 0; i < 16; i++) {
      if (key_interface.is_key_down(MDX_KEY_TRIG1 + i)) {
        pad_mask |= (uint16_t)1 << i;
      }
    }
  }

  // Runs every real tick regardless of grid phase, so a released pad (or
  // the whole roll disarming) gets its mute state resynced promptly rather
  // than waiting for the next subdivision boundary.
  uint16_t released = forced_unmuted_mask & (uint16_t)~pad_mask;
  if (released != 0) {
    for (uint8_t i = 0; i < 16; i++) {
      if (released & ((uint16_t)1 << i)) {
        SeqTrack *seq_track = mixer_page.mixer_seq_track(i);
        if (seq_track != nullptr) {
          mixer_page.mixer_target.mute_track(i, seq_track->mute_state);
        }
      }
    }
    forced_unmuted_mask &= ~released;
  }

  if (!armed || pad_mask == 0) {
    return;
  }

  uint8_t rate = beat_repeat_normalized_rate(mcl_cfg.beat_repeat_rate);
  uint8_t period = pgm_read_byte(&BEAT_REPEAT_DIV192_PERIOD[rate]);

  // Deliberately no "fire instantly on newly-pressed pad" case: every hit,
  // including the very first one after pressing, waits for this grid point
  // so it's always exactly on time rather than at whatever arbitrary
  // moment the pad happened to be pressed.
  if (MidiClock.div192th_counter % period != 0) {
    return;
  }

  for (uint8_t i = 0; i < 16; i++) {
    if (!(pad_mask & ((uint16_t)1 << i))) {
      continue;
    }

    // Bypass mute for the roll (SYSTEM menu "ROLL MUTE" toggle): mute is a
    // real MD hardware CC, not just an MCL bookkeeping flag, so a
    // manually-triggered note wouldn't sound on a muted track otherwise.
    // Only send the unmute CC once per hold (not every hit) — mute_state
    // itself is left untouched, so MCL's own mute bookkeeping/LEDs/mixer
    // display stay accurate throughout.
    if (mcl_cfg.roll_ignores_mute &&
        !(forced_unmuted_mask & ((uint16_t)1 << i))) {
      SeqTrack *seq_track = mixer_page.mixer_seq_track(i);
      if (seq_track != nullptr && seq_track->mute_state) {
        mixer_page.mixer_target.mute_track(i, false);
        forced_unmuted_mask |= (uint16_t)1 << i;
      }
    }

    MD.triggerTrack(i, BEAT_REPEAT_VELOCITY, uart);

    // Same explicit trigger+record pairing the arpeggiator uses for MD
    // tracks (MDArpSeqTrack::dispatch_note() -> SeqPtcTrackRef::trigger()/
    // record_track(), which on AVR is the exact same MD.triggerTrack()
    // call above) — recording isn't a passive listener on any note that
    // goes out, each dispatch site has to opt in like this or it's
    // invisible to live-record.
    if (SeqPage::recording && MidiClock.state == 2) {
      reset_undo();
      SeqPtcTrackRef::record_track(i, BEAT_REPEAT_VELOCITY);
    }
  }
}
