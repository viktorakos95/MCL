/* Copyright Justin Mammarella jmamma@gmail.com 2018 */

#ifndef EUCPAGE_H__
#define EUCPAGE_H__

#include "GUI.h"

// Elektron-style Euclidean sequencer mode editor -- reached only from the
// SEQ menu's "EUCLIDEAN" entry, no dedicated hardware key.
//
// No MCLEncoder involved at all -- UP/DOWN adjusts whichever of the 7
// params is currently selected (LEFT/RIGHT cycles which one), directly on
// the slot. Tried sharing SeqStepPage's own encoder objects first, but
// even that still has a cost; simplest and cheapest is to not need an
// Encoder object in the first place. LightPage is still the base class
// (not the lighter PageParent) only because MCL::pages_table (MCL.h) is
// typed to LightPage* -- the encoders[] array it carries just goes
// unused here.
//
// Params, cycled via LEFT/RIGHT: PL1, PL2, EUC (on/off), RO1, RO2, TRO, OP
// -- same order they're laid out in the 4x2 on-screen grid (matching the
// Digitone/Analog layout: PL1 PL2 LEN EUC / RO1 RO2 TRO OP), skipping over
// the LEN cell since it's a read-only readout, not a selectable param --
// track length is edited from the SEQ menu's own LENGTH entry, not
// duplicated here.
class EucPage : public LightPage {
public:
  static constexpr uint8_t NUM_PARAMS = 7;

  uint8_t current_track;
  uint8_t param_index;

  EucPage() : LightPage() {
    current_track = 0;
    param_index = 0;
  }

  bool handleEvent(gui_event_t *event) override;
  void track_update();
  void update_leds();
  void loop() override;
  void display() override;
  void init() override;
  void cleanup() override;
};

#endif /* EUCPAGE_H__ */
