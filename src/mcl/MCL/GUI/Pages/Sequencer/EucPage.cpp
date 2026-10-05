/* Copyright Justin Mammarella jmamma@gmail.com 2018 */

#include "GUI/Pages/Sequencer/EucPage.h"
#include "GUI/Pages/Sequencer/SeqPages.h"
#include "KeyInterface.h"
#include "MCLGUI.h"
#include "MCLStrings.h"
#include "MD.h"
#include "Sequencer/Euclidean.h"
#include "Sequencer/MCLSeq.h"
#include "Sequencer/SeqDefines.h"

namespace {
// Selectable params, in the same order they cycle via LEFT/RIGHT and the
// same order they appear reading the grid left-to-right/top-to-bottom
// (with LEN, not independently selectable, inserted as the 3rd cell).
constexpr uint8_t PARAM_PL1 = 0;
constexpr uint8_t PARAM_PL2 = 1;
constexpr uint8_t PARAM_EUC = 2;
constexpr uint8_t PARAM_RO1 = 3;
constexpr uint8_t PARAM_RO2 = 4;
constexpr uint8_t PARAM_TRO = 5;
constexpr uint8_t PARAM_OP = 6;
constexpr uint8_t GRID_LEN = 255; // sentinel: the read-only LEN cell

// Indexed by PARAM_*.
const char *const euc_param_labels[] PROGMEM = {
    mclstr_pl1, mclstr_pl2, mclstr_euc, mclstr_ro1,
    mclstr_ro2, mclstr_tro, mclstr_op,
};
// 4x2 grid, matching the Digitone/Analog layout: PL1 PL2 LEN EUC / RO1 RO2
// TRO OP. Values are PARAM_* or GRID_LEN.
const uint8_t euc_grid[8] PROGMEM = {
    PARAM_PL1, PARAM_PL2, GRID_LEN, PARAM_EUC,
    PARAM_RO1, PARAM_RO2, PARAM_TRO, PARAM_OP,
};

// Indexed by op (EUC_OP_* in Euclidean.h).
const char *const euc_op_labels[] PROGMEM = {mclstr_euc_or, mclstr_euc_xor,
                                             mclstr_euc_and, mclstr_euc_sub};
// Indexed by `enabled` (0/1), same "-- "/"ON" convention ArpPage uses for
// its own enabled knob.
const char *const euc_enabled_labels[] PROGMEM = {mclstr_dash, mclstr_on};

// Formats param `p`'s current value from `slot` into `str` (>= 5 bytes).
// `slot` is never null -- every track has its own permanent slot (see
// Euclidean.h).
void format_param(uint8_t p, EucSlot *slot, char *str) {
  switch (p) {
  case PARAM_PL1:
    mcl_gui.put_value_at(slot->pl1, str);
    break;
  case PARAM_PL2:
    mcl_gui.put_value_at(slot->pl2, str);
    break;
  case PARAM_RO1:
    mcl_gui.put_value_at(slot->ro1, str);
    break;
  case PARAM_RO2:
    mcl_gui.put_value_at(slot->ro2, str);
    break;
  case PARAM_TRO:
    mcl_gui.put_value_at(slot->tro, str);
    break;
  case PARAM_OP:
    strcpy_P(str, (PGM_P)pgm_read_ptr(
                      &euc_op_labels[slot->op < EUC_OP_COUNT ? slot->op : 0]));
    break;
  case PARAM_EUC:
  default:
    strcpy_P(str, (PGM_P)pgm_read_ptr(&euc_enabled_labels[slot->enabled ? 1 : 0]));
    break;
  }
}
} // namespace

void EucPage::track_update() { current_track = seq_primary_track_index(); }

void EucPage::init() {
  DEBUG_PRINT_FN();
  track_update();
}

// Called on every way of leaving this page -- NO, YES, or just navigating
// elsewhere (GRID, another menu, track select, ...). Touching any param
// while editing sets `enabled` for live preview (see handleEvent below),
// but that preview must never survive past this page: without this,
// walking away without an explicit NO/YES left `enabled` stuck true, so
// the generated pattern kept overriding the real one on every other
// sequencer-family page (SEQ_STEP_PAGE/EXTSTEP/PTC -- see
// MDSeqTrack::effective_trig()). Sweeps every track's slot, not just
// current_track: since track-select can change which track this page is
// showing without actually leaving it (see track_update(), called from
// MDTrackSelect.cpp), a single visit here can arm more than one track's
// live preview before you finally navigate away, and none of them should
// survive uncommitted. Only clears `enabled`, same as NO -- pl1/pl2/etc
// are untouched, so reopening this page on any of those tracks picks the
// preview back up exactly as it was.
void EucPage::cleanup() {
  for (uint8_t i = 0; i < EUC_NUM_SLOTS; i++) {
    euc_slots[i].enabled = false;
  }
}

// Lights the physical trig pads to show exactly where the current
// (possibly still-unarmed-preview) pattern's pulses land, matching how
// Digitone/Rytm show the Euclidean pattern on their own key LEDs -- the
// OLED's normal step grid isn't usable here (this page is a popup card
// drawn on top of it), so the pads are the only live feedback. Respects
// SeqPage::page_select (the same "which 16-step window" SCALE pages
// through on the underlying step page) rather than always showing steps
// 0-15 -- for patterns longer than 16 steps, the pads need to track
// whichever window is actually selected, same as normal (non-Euclidean)
// step editing already does.
void EucPage::update_leds() {
  MDSeqTrack &euc_track = mcl_seq.md_tracks[current_track];
  uint8_t length = euc_track.length;
  uint8_t offset = SeqPage::page_select * 16;

  uint16_t mask = 0;
  for (uint8_t i = 0; i < 16; i++) {
    uint8_t step = offset + i;
    if (step >= length) {
      break;
    }
    // effective_trig(), not euclidean_pattern_has_pulse() directly: while
    // EUC is toggled off, this needs to fall back to the real (manual)
    // pattern -- same as leaving the page does -- not keep showing the
    // generated preview lit up as if it were still armed. EUC_PAGE is
    // itself one of effective_trig()'s "on a seq/euc page" cases, so
    // calling it from here already gets that fallback for free.
    if (euc_track.effective_trig(step)) {
      mask |= (uint16_t)1 << i;
    }
  }
  mcl_gui.set_trigleds(mask, TRIGLED_STEPEDIT);
}

void EucPage::loop() { update_leds(); }

// Compact custom layout, not MCLGUI::draw_text_encoder() -- that helper's
// box (23x16px, and it switches to the larger default font for the value)
// is sized for 4 knobs in a single row like ArpPage has. Fitting 8 cells
// on this same screen needs roughly half that: both label and value stay
// in the small TomThumb font, single line per cell ("PL1 4" etc, not a
// label line stacked over a value line -- with only 32 real rows of
// screen (this is a 128x32 SSD1305, not 128x64 -- see
// SSD1305_128_32/SSD1305_128_64 in the Adafruit_SSD1305 library; the box
// used to be sized/positioned as if it had twice the height, which is why
// the whole second row used to render almost entirely below the visible
// screen), 2 lines per cell never fit both rows at once. Box uses the
// full screen top-to-bottom (y=0-31), not ArpPage's exact box -- Arp only
// needs one row of knobs so it can afford margin above and below; with
// two rows plus a title line, EUC needs what the screen actually has.
void EucPage::display() {
  MDSeqTrack &euc_track = mcl_seq.md_tracks[current_track];
  EucSlot *slot = euc_slot_find(current_track);

  oled_display.setFont(&TomThumb);

  oled_display.fillRect(6, 0, 128 - 12, 32, BLACK);
  oled_display.drawRect(7, 1, 128 - 14, 30, WHITE);

  oled_display.setCursor(10, 8);
  oled_display.setTextColor(WHITE);
  // "EUC TRACK <n>", not a dedicated "EUCLIDEAN TRACK " string -- reuses
  // mclstr_euc (already resident, see euc_param_labels above) and
  // mclstr_track (already resident, see SeqPage.cpp's popups), so this
  // title costs nothing beyond the two already-paid-for strings.
  mcl_print_P(mclstr_euc);
  oled_display.print(' ');
  mcl_print_P(mclstr_track);
  oled_display.print(' ');
  oled_display.print(current_track + 1);

  char str[5];
  constexpr uint8_t x0 = 10;
  constexpr uint8_t col_w = 27;
  // Label cell then value, at a fixed offset from the cell's own x rather
  // than right after the label -- labels are 2-3 chars ("OP" vs "PL1"),
  // and keeping the value column dead straight across cells reads as a
  // grid; letting it float after a shorter label wouldn't.
  constexpr uint8_t value_dx = 13;
  constexpr uint8_t row0_y = 17;
  constexpr uint8_t row1_y = 28;
  for (uint8_t cell = 0; cell < 8; cell++) {
    uint8_t p = pgm_read_byte(&euc_grid[cell]);
    uint8_t col = cell % 4;
    uint8_t row = cell / 4;
    uint8_t x = x0 + col * col_w;
    uint8_t y = row == 0 ? row0_y : row1_y;

    PGM_P label;
    if (p == GRID_LEN) {
      mcl_gui.put_value_at(euc_track.length, str);
      label = mclstr_len;
    } else {
      format_param(p, slot, str);
      label = (PGM_P)pgm_read_ptr(&euc_param_labels[p]);
    }

    oled_display.setCursor(x, y);
    mcl_print_P(label);
    uint8_t value_x = x + value_dx;
    oled_display.setCursor(value_x, y);
    oled_display.print(str);
    // INVERT after the text (not before): it flips whatever pixels are
    // already there, so the just-drawn white text becomes black-on-white.
    // Doing this before the text just made the background white and then
    // drew white text on top of it -- invisible. Positioned from `y - 6`
    // (not `y`): TomThumb's cursor y is the text baseline, and its glyphs
    // sit *above* that (up to 5px), so a box starting at the baseline was
    // landing entirely below the digits instead of on them.
    if (p != GRID_LEN && p == param_index) {
      oled_display.fillRect(value_x - 1, y - 6, 13, 7, INVERT);
    }
  }
}

bool EucPage::handleEvent(gui_event_t *event) {
  if (EVENT_CMD(event)) {
    uint8_t key = event->source;
    if (event->mask == EVENT_BUTTON_PRESSED) {
      switch (key) {
      case MDX_KEY_LEFT:
        param_index = (param_index == 0) ? NUM_PARAMS - 1 : param_index - 1;
        return true;
      case MDX_KEY_RIGHT:
        param_index = (param_index + 1) % NUM_PARAMS;
        return true;
      case MDX_KEY_UP:
      case MDX_KEY_DOWN: {
        bool increase = key == MDX_KEY_UP;
        if (param_index == PARAM_EUC) {
          // Only flips `enabled` -- never touches pl1/pl2/etc, so turning
          // this off and back on always comes back to exactly what it was.
          euc_slot_find(current_track)->enabled = increase;
          return true;
        }
        EucSlot *slot = euc_slot_find(current_track);
        slot->enabled = true;
        switch (param_index) {
        case PARAM_PL1:
          if (increase && slot->pl1 < 64) slot->pl1++;
          if (!increase && slot->pl1 > 0) slot->pl1--;
          break;
        case PARAM_PL2:
          if (increase && slot->pl2 < 64) slot->pl2++;
          if (!increase && slot->pl2 > 0) slot->pl2--;
          break;
        case PARAM_RO1:
          if (increase && slot->ro1 < 63) slot->ro1++;
          if (!increase && slot->ro1 > 0) slot->ro1--;
          break;
        case PARAM_RO2:
          if (increase && slot->ro2 < 63) slot->ro2++;
          if (!increase && slot->ro2 > 0) slot->ro2--;
          break;
        case PARAM_TRO:
          if (increase && slot->tro < 63) slot->tro++;
          if (!increase && slot->tro > 0) slot->tro--;
          break;
        case PARAM_OP:
          slot->op = increase ? (slot->op + 1) % EUC_OP_COUNT
                              : (slot->op == 0 ? EUC_OP_COUNT - 1 : slot->op - 1);
          break;
        }
        return true;
      }
      case MDX_KEY_NO:
        // cleanup() (below) disables the slot on the way out -- see there
        // for why that has to be unconditional, not just a NO/YES thing.
        mcl.popPage();
        return true;
      }
    } else if (event->mask == EVENT_BUTTON_RELEASED) {
      // Triggered on release, not press: the press that got you here can
      // still be physically down when YES is first pressed, and
      // QuestionDialogPage -- which YES pushes -- only acts on a
      // press/release pair it saw in full itself. Opening it from a press
      // here left its first release looking like a stray, un-started
      // gesture, so nothing happened until YES was pressed a second time
      // -- i.e. it looked like you had to hold it.
      if (key == MDX_KEY_YES) {
        EucSlot *slot = euc_slot_find(current_track);
        if (mcl_gui.wait_for_confirm("CONFIRM", "Overwrite?")) {
          MDSeqTrack &euc_track = mcl_seq.md_tracks[current_track];
          uint8_t length = euc_track.length;
          for (uint8_t i = 0; i < length; i++) {
            euc_track.set_step(
                i, MASK_PATTERN,
                euclidean_pattern_has_pulse(i, slot->pl1, slot->pl2,
                                            slot->ro1, slot->ro2, slot->op,
                                            slot->tro, length));
          }
          mcl.popPage();
        }
        return true;
      }
      if (key == MDX_KEY_SCALE) {
        // Same "cycle through this track's 16-step windows" SCALE does on
        // the real sequencer pages (see SeqPage::check_and_set_page_select
        // and its MDX_KEY_SCALE case) -- EucPage isn't a SeqPage subclass
        // so it doesn't inherit that handling, but page_select/page_count
        // are shared static state, and update_leds() already reads
        // page_select (see there), so this just needs to drive it the
        // same way and refresh the pads to match.
        SeqPage::page_select++;
        MDSeqTrack &euc_track = mcl_seq.md_tracks[current_track];
        uint16_t offset = (uint16_t)SeqPage::page_select * 16;
        if (SeqPage::page_select >= SeqPage::page_count ||
            offset >= euc_track.length) {
          SeqPage::page_select = 0;
        }
        MD.set_seq_page(SeqPage::page_select);
        update_leds();
        return true;
      }
    }
  }
  return false;
}
