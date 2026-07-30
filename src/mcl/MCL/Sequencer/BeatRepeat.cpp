/* Copyright Justin Mammarella jmamma@gmail.com 2018 */

#include "Sequencer/BeatRepeat.h"

#include "GUI/PageIndex.h"
#include "GUI/Pages/CommonPages.h"
#include "KeyInterface.h"
#include "MCL.h"
#include "MCLSysConfig.h"
#include "../../Drivers/DeviceContext.h"
#include "../../Drivers/MD/MD.h"

#include <avr/pgmspace.h>

namespace {

// Values are in half-ticks (24ppqn MIDI clock == 48 half-ticks per quarter
// note) rather than whole ticks, purely so 1/64 (1.5 ticks) has an exact,
// whole-number period like every other rate here. See beat_repeat_tick().
const uint8_t BEAT_REPEAT_HALF_TICKS[BEAT_REPEAT_RATE_COUNT] PROGMEM = {
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
  static uint8_t half_tick_counter = 0;
  static uint16_t prev_pad_mask = 0;

  // beat_repeat_armed already reflects the debounced LEFT+RIGHT chord (see
  // MixerPage::handleEvent()/loop()) — reusing it here (rather than
  // re-checking raw key state) keeps "shown as armed" and "actually
  // triggering" from ever disagreeing. Gated to the Primary device slot
  // since the roll is MD-specific (MD.triggerTrack() below); the Mixer
  // page's Secondary slot may be bound to a different device entirely.
  bool armed = mcl.current_page == MIXER_PAGE &&
               mixer_page.beat_repeat_armed &&
               mixer_page.mixer_device_idx == DeviceIdx::Primary;

  if (!armed) {
    half_tick_counter = 0;
    prev_pad_mask = 0;
    return;
  }

  uint16_t pad_mask = 0;
  for (uint8_t i = 0; i < 16; i++) {
    if (key_interface.is_key_down(MDX_KEY_TRIG1 + i)) {
      pad_mask |= (uint16_t)1 << i;
    }
  }

  uint16_t newly_pressed = pad_mask & ~prev_pad_mask;
  prev_pad_mask = pad_mask;

  uint8_t rate = beat_repeat_normalized_rate(mcl_cfg.beat_repeat_rate);
  uint8_t period = pgm_read_byte(&BEAT_REPEAT_HALF_TICKS[rate]);

  bool fire_all = false;
  half_tick_counter += 2;
  if (half_tick_counter >= period) {
    half_tick_counter = 0;
    fire_all = true;
  }

  uint16_t to_fire = newly_pressed;
  if (fire_all) {
    to_fire |= pad_mask;
  }
  if (to_fire == 0) {
    return;
  }

  for (uint8_t i = 0; i < 16; i++) {
    if (to_fire & ((uint16_t)1 << i)) {
      MD.triggerTrack(i, BEAT_REPEAT_VELOCITY, uart);
    }
  }
}
