#include "GUI/Pages/Grid/GridPage.h"
#include "GUI/Pages/CommonPages.h"
#ifdef PLATFORM_TBD
#include "Ui.h"
#endif
#include "GUI/Pages/Grid/GridPages.h"
#include "ResourceManager.h"
#include "MCLGUI.h"
#include "MCLSysConfig.h"
#include "Grid/GridTask.h"
#include "EmptyTrack.h"
#include "Project.h"
#include "Devices/DeviceManager.h"
#include "../../../../Drivers/MidiDevice.h"
#include "../../../../Drivers/Generic/GenericMidiDevice.h"
#include "../../../../Drivers/MD/MD.h"
#include "../../../../Drivers/MNM/MNMParams.h"
#include "Grid/MCLActions.h"
#include "MCLClipBoard.h"
#include "MCLStrings.h"
#if MCL_GRID_IO_PANEL
#include "MDTrack.h"
#endif

#define PERF_ENC 1

namespace {

void set_slot_label(char label[3], char a, char b) {
  label[0] = a;
  label[1] = b;
  label[2] = '\0';
}

#if defined(__AVR__)
uint16_t grid_slot_label_for_type(uint8_t track_type,
                                  GridSlotLabelContext ctx) {
  const char *machine_name = nullptr;
  switch (track_type) {
  case MD_TRACK_TYPE:
    machine_name = getMDMachineNameShort(ctx.model, 2);
    return machine_name ? make_grid_slot_label(machine_name[0], machine_name[1])
                        : 0;
  case MNM_TRACK_TYPE:
    machine_name = getMNMMachineNameShort(ctx.model, 2);
    return machine_name ? make_grid_slot_label(machine_name[0], machine_name[1])
                        : 0;
  case A4_TRACK_TYPE:
    return make_grid_slot_label('A', ctx.column + '1');
  case EXT_TRACK_TYPE:
    return make_grid_slot_label('M', ctx.column + '1');
  case MDFX_TRACK_TYPE:
    return make_grid_slot_label('F', 'X');
  case MDTEMPO_TRACK_TYPE:
    return make_grid_slot_label('T', 'P');
  case MD_ROUTE_TRACK_TYPE:
    return make_grid_slot_label('R', 'T');
  case GRIDCHAIN_TRACK_TYPE:
    return make_grid_slot_label('C', 'N');
  case PERF_TRACK_TYPE:
    return make_grid_slot_label('P', 'F');
  default:
    return 0;
  }
}
#endif

void copy_grid_slot_label(uint8_t track_type,
                          GridSlotLabelContext ctx, char label[3]) {
#if defined(__AVR__)
  uint16_t packed = grid_slot_label_for_type(track_type, ctx);
#else
  EmptyTrack scratch;
  auto *track = ((DeviceTrack *)&scratch)->init_track_type(track_type);
  uint16_t packed = track->grid_slot_label(ctx);
#endif
  if (packed == 0) {
    return;
  }
  label[0] = packed >> 8;
  label[1] = packed;
  label[2] = '\0';
}

void inherit_grid_x_row_name(GridRowHeader &row_header, GridRow row) {
  GridRowHeader grid_x_header;
  if (!proj.read_grid_row_header(&grid_x_header, row, 0) ||
      !grid_x_header.active || grid_x_header.name[0] == '\0') {
    row_header.name[0] = '\0';
    return;
  }

  strncpy(row_header.name, grid_x_header.name, sizeof(row_header.name));
  row_header.name[sizeof(row_header.name) - 1] = '\0';
}

enum SlotDirtyMask : uint8_t {
  SLOT_DIRTY_LENGTH = 1 << 0,
  SLOT_DIRTY_LOOPS = 1 << 1,
  SLOT_DIRTY_ROW = 1 << 2,
  SLOT_DIRTY_LOAD_SOUND = 1 << 3,
};

bool apply_slot_dirty_fields(GridTrack &target_slot, const GridTrack &edited_slot,
                             uint8_t dirty_mask) {
  bool store_slot = false;
  bool changed_length = dirty_mask & SLOT_DIRTY_LENGTH;
  bool changed_loops = dirty_mask & SLOT_DIRTY_LOOPS;

  if (dirty_mask & SLOT_DIRTY_LOAD_SOUND) {
    target_slot.set_load_sound(edited_slot.load_sound());
    store_slot = true;
  }
  if (changed_loops && edited_slot.link.loops == 0) {
    target_slot.link.loops = 0;
    store_slot = true;
  } else if (changed_loops || changed_length) {
    if (changed_loops && changed_length) {
      target_slot.link.loops = edited_slot.link.loops;
      target_slot.link.length = edited_slot.link.length;
      store_slot = true;
    } else if (changed_loops) {
      uint16_t slot_length =
          (uint16_t)target_slot.link.length *
          SeqTrack::get_speed_multiplier_int(target_slot.link.speed_value()) /
          12;
      if (slot_length) {
        uint16_t target_length =
            (uint32_t)edited_slot.link.length *
            SeqTrack::get_speed_multiplier_int(edited_slot.link.speed_value()) *
            edited_slot.link.loops / 12;
        uint16_t matched_loops = target_length / slot_length;
        if (!(target_length % slot_length) && matched_loops > 0 &&
            matched_loops <= UINT8_MAX) {
          target_slot.link.loops = matched_loops;
        } else {
          // An unrepresentable match uses the same fallback as a fractional one.
          // Narrowing 256/512/... to zero would silently disable chaining.
          target_slot.link.loops = edited_slot.link.loops;
        }
        store_slot = true;
      }
    } else if (target_slot.link.speed_value() == edited_slot.link.speed_value()) {
      target_slot.link.length = edited_slot.link.length;
      store_slot = true;
    }
  }
  if (dirty_mask & SLOT_DIRTY_ROW) {
    target_slot.link.row = edited_slot.link.row;
    store_slot = true;
  }
  return store_slot;
}

#if defined(MCL_HAS_DESKTOP_MOUSE)
bool grid_mouse_hit_visible_cell(GridPage &page, const mcl_mouse_event_t *event,
                                 uint8_t *visible_col, uint8_t *visible_row,
                                 GridColumn *track_idx, GridRow *row_idx) {
  constexpr int16_t x_offset = 43;
  constexpr int16_t y_offset = 8;
  constexpr int16_t row_top = y_offset - 6;

  int row = (event->y - row_top) / 8;
  if (event->y < row_top || row < 0 || row >= MAX_VISIBLE_ROWS) {
    return false;
  }

  int base_col = (int)page.getCol() - page.cur_col;
  int base_row = (int)page.getRow() - page.cur_row;
  GridSpan grid_width = page.getWidth();
  GridSpan col_shift = 0;
  if (page.show_slot_menu) {
    if (page.cur_col + param3.cur > MAX_VISIBLE_COLS - 1) {
      col_shift = page.cur_col + param3.cur - MAX_VISIBLE_COLS;
    }
  }

  int cur_posx = x_offset;
  int hit_x = -1;
  for (uint8_t x = col_shift; x < MAX_VISIBLE_COLS + col_shift && x < grid_width;
       x++) {
    int track = x + base_col;
    if (event->x >= cur_posx - 1 && event->x < cur_posx + 9) {
      hit_x = x;
      break;
    }
    if (track >= 0 && track % 4 == 3 && grid_width >= 8) {
      cur_posx += 12;
    } else {
      cur_posx += 10;
    }
  }

  if (hit_x < 0) {
    return false;
  }

  int track = hit_x + base_col;
  int grid_row = row + base_row;
  if (track < 0 || track >= GRID_WIDTH || grid_row < 0 ||
      grid_row >= GRID_LENGTH) {
    return false;
  }

  *visible_col = (uint8_t)hit_x;
  *visible_row = (uint8_t)row;
  *track_idx = (GridColumn)track;
  *row_idx = (GridRow)grid_row;
  return true;
}

void set_slot_menu_range_to_cell(GridPage &page, uint8_t visible_col,
                                 uint8_t visible_row) {
  GridSpan row_shift = 0;
  if (page.cur_row + param4.cur > MAX_VISIBLE_ROWS - 1) {
    row_shift = page.cur_row + param4.cur - MAX_VISIBLE_ROWS;
  }

  int width = (int)visible_col - page.cur_col + 1;
  int height = (int)visible_row + row_shift - page.cur_row + 1;
  if (width < 1) {
    width = 1;
  }
  if (height < 1) {
    height = 1;
  }
  if (width > page.getWidth()) {
    width = page.getWidth();
  }
  int max_height = GRID_LENGTH - page.getRow();
  if (height > max_height) {
    height = max_height;
  }

  param3.cur = width;
  param4.cur = height;
}

void close_slot_menu_from_mouse(GridPage &page) {
  if (!page.show_slot_menu) {
    return;
  }

  uint8_t old_undo = page.slot_undo;
  bool restore_undo = false;
  if (!(page.slot_copy || page.slot_paste || page.slot_clear ||
        page.slot_load)) {
    page.slot_undo = 0;
    restore_undo = true;
  }

  page.apply_slot_changes(false, true);

  if (restore_undo) {
    page.slot_undo = old_undo;
  }

  page.init();
}
#endif

} // namespace

#if MCL_GRID_IO_PANEL
// Load is armed and its rows are being edited (Load|Yes or the
// Machinedrum's [Global] is held): the knobs edit them (io_handleEvent,
// io_loop). Otherwise they are the Grid page's own.
static bool io_editing;
// FADE (Slide) is held past a tap, or used: the FADE page shows.
static bool io_fade_page();
// Load or Save was armed when Classic/Extended opened the mixer or REC
// opened step edit: init() arms it again on the return from there.
static uint8_t io_resume;
#ifndef PLATFORM_TBD
// The group popup's icons are loaded after the page's own resources (init()
// loads those and clears this).
static bool io_icons_loaded;
#endif
#endif

// The Grid page's own knobs: the performance encoders when they are enabled,
// else the cursor (knob 1 the column, knob 2 the row).
static void grid_knobs(Encoder **encoders) NOINLINE();
static void grid_knobs(Encoder **encoders) {
  if (mcl_cfg.grid_page_mode == PERF_ENC) {
    encoders[0] = &perf_param1;
    encoders[1] = &perf_param2;
    encoders[2] = &perf_param3;
    encoders[3] = &perf_param4;
  }
  else {
    encoders[0] = &param1;
    encoders[1] = &param2;
    encoders[2] = &param3;
    encoders[3] = &param4;
  }
}

void GridPage::init() {
  DEBUG_PRINTLN(F("Grid page init"));
#if MCL_GRID_IO_PANEL
  if (io_mode) {
    io_end(false);
  }
#endif
  grid_knobs(encoders);
  param1.max = getWidth() - 1;
  show_slot_menu = false;
  old_col = 255;
  reload_slot_models = false;
  draw_encoders = false;
  // Edge case, prevent R.Clear being called if we're outside of GridPage
  mcl_gui.reset_trigleds();
  if (mcl.currentPage() != GRID_PAGE) {
    return;
  }
  key_interface.off();
  //load_slot_models();
  R.Clear();
  R.use_machine_names_short();
  R.use_icons_knob();
#ifdef PLATFORM_TBD
  R.use_icons_logo();
  RM_REQUIRE(0, __T_machine_names_short, __T_icons_knob, __T_icons_logo);
#else
  RM_REQUIRE(0, __T_machine_names_short, __T_icons_knob);
#if MCL_GRID_IO_PANEL
  io_icons_loaded = false;
#endif
#endif
#if MCL_GRID_IO_PANEL
  if (io_resume) {
    const uint8_t mode = io_resume;
    io_resume = GRID_IO_OFF;
    if (mcl.previous_page == MIXER_PAGE ||
        mcl.previous_page == SEQ_STEP_PAGE) {
      io_begin(mode);
    }
  }
#endif
}

void GridPage::setup() {
  param1.cur = param1.old = mcl_cfg.col;
  param2.cur = param2.old = mcl_cfg.row;
  param3.cur = 1;
  cur_col = mcl_cfg.cur_col;
  cur_row = mcl_cfg.cur_row;
  memset(active_slots, SLOT_DISABLED, sizeof(active_slots));
  /*
  cur_col = 0;
  if (mcl_cfg.row < MAX_VISIBLE_ROWS) { cur_row = mcl_cfg.row; }
  else { cur_row = MAX_VISIBLE_ROWS - 1; }
  */
}

void GridPage::cleanup() {
#if MCL_GRID_IO_PANEL
  if (io_mode) {
    io_end(false);
  }
#endif
  oled_font_default();
  oled_text_color(WHITE, BLACK);
  bank_popup = 0;
}

void GridPage::load_row(uint8_t n, GridRow row) {
  if (row >= GRID_LENGTH) { return; }
  if (IS_BIT_CLEAR16(grid_page.bank_popup_loadmask, n)) {
    GridLoadPage::group_load(row);
    SET_BIT16(grid_page.bank_popup_loadmask, n);
  }
}

void GridPage::jump_to_row(GridRow row) {
  //  uint8_t y = (row / MAX_VISIBLE_ROWS) * MAX_VISIBLE_ROWS;
  //  uint8_t r = row - y;
  GridRow y = row;
  GridRow r = row - (row / MAX_VISIBLE_ROWS) * MAX_VISIBLE_ROWS;
  param2.cur = y;
  param2.old = y;
  cur_row = r;
  reload_slot_models = false;
  grid_lastclock = read_clock_ms();
  write_cfg = true;
}

void GridPage::set_active_row(GridRow row) {
  grid_task.last_active_row = row;
  if (bank_popup) {
    send_row_led();
  }
}

void GridPage::send_row_led() {
  GridRow active_row = grid_task.last_active_row;
  if (active_row >= GRID_LENGTH) { return; }
  uint16_t blink_mask = 0;
  if ((active_row >> 4) == bank) {
    blink_mask = (uint16_t)1 << (active_row & 0x0F);
  }
  MD.set_trigleds(blink_mask, TRIGLED_EXCLUSIVENDYNAMIC, 1);
}
void GridPage::close_bank_popup() {
  if (bank_popup == 2) {
    MD.draw_close_bank();
  }
  key_interface.off();
  if (last_page != 255) {
    DEBUG_PRINTLN(F("setting page"));
    mcl.setPage(last_page);
  }
  last_page = NULL_PAGE;
  bank_popup = 0;
  bank_popup_loadmask = 0;
#ifdef PLATFORM_TBD
  bank_popup_first_trig = 255;
  bank_popup_oled_visible = true;
#endif
  note_interface.init_notes();
  // Clear blink leds. On TBD this also drops the trig colour override
  // (set_trigleds_local resets it) so the strip returns to monochrome.
  MD.set_trigleds(0, TRIGLED_EXCLUSIVENDYNAMIC, 1);
#ifdef PLATFORM_TBD
  mcl_gui.set_trigleds_local(0, TRIGLED_EXCLUSIVE);
#endif
}

void GridPage::load_old_col() {
  cur_col = old_col;
  param1.cur = cur_col;
  param1.old = cur_col;
  param3.cur = GRID_WIDTH - cur_col;
  param3.old = param3.cur;
  cur_grid = 0;
  param3.max = getWidth() + 1;
}

void GridPage::loop() {
#if MCL_GRID_IO_PANEL
  if (io_mode == GRID_IO_LOAD) {
    io_loop();
  }
#endif
  if (mcl_cfg.grid_page_mode == PERF_ENC) {
     //need to limit range of the alternate encoders
     encoder_t *enc = nullptr;
     perf_page.func_enc_check();
     param1.update(enc);
     param2.update(enc);
     param3.update(enc);
     param4.update(enc);
  }
  int8_t diff, new_val;
  if (show_slot_menu) {
    if (param3.hasChanged()) {
        if ((cur_grid == 0) && (getCol() + param3.cur > (int)GRID_WIDTH)) {
        old_col = getCol();
        cur_col = 0;
        param1.cur = 0;
        param1.old = 0;
        param3.cur = 1;
        param3.old = 1;
        cur_grid = 1;
        param3.max = getWidth();
        load_slot_models();
        reload_slot_models = true;
      }
      else if ((cur_grid == 1) && (param3.cur == 0) && (old_col != 255)) {
        load_old_col();
        load_slot_models();
        reload_slot_models = true;
      }
      if (param3.cur == 0) { param3.cur = 1; param3.old = 1; }
    }
    if (param4.hasChanged()) {
      if (cur_row + param4.cur > MAX_VISIBLE_ROWS - 1) {
        load_slot_models();
        reload_slot_models = true;
      }
    }
    grid_slot_page.loop();
    return;
  } else {
    if (mcl_cfg.grid_page_mode == PERF_ENC
#if MCL_GRID_IO_PANEL
        && !io_editing &&
        !(io_mode == GRID_IO_LOAD && mcl_cfg.io_stay == LOAD_SAVE_LEGACY)
#endif
    ) {
      if (encoders[0]->hasChanged() || encoders[1]->hasChanged() || encoders[2]->hasChanged() || encoders[3]->hasChanged()) {
         //mcl.setPage(MIXER_PAGE);
         draw_encoders_lastclock = read_clock_ms();
         draw_encoders = true;
         //return;
         perf_page.encoder_send();
      }
    }
  }

  if (param3.hasChanged()) {
    if (param3.cur == 0) { param3.cur = 1; param3.old = 1; }
  }
  if (param1.hasChanged()) {
    diff = param1.cur - param1.old;
    new_val = cur_col + diff;
    if (new_val > MAX_VISIBLE_COLS - 1) {
      new_val = MAX_VISIBLE_COLS - 1;
    }
    if (new_val < 0) {
      new_val = 0;
    }
    cur_col = new_val;
  }

  if (param2.hasChanged()) {
    diff = param2.cur - param2.old;
    new_val = cur_row + diff;

    if (new_val > MAX_VISIBLE_ROWS - 1) {
      new_val = MAX_VISIBLE_ROWS - 1;
    }
    if (new_val < 0) {
      new_val = 0;
    }
    cur_row = new_val;
    if ((cur_row == MAX_VISIBLE_ROWS - 1) || (cur_row == 0)) {
      load_slot_models();
    }
    reload_slot_models = true;
    grid_lastclock = read_clock_ms();

    write_cfg = true;
  }
#if MCL_GRID_IO_PANEL
  if (io_mode) {
    // Load and Save hold the knobs, so the GUI does not mark the cursor's
    // moves (the arrow keys) as seen; do it here, or each frame would move
    // it again.
    param1.old = param1.cur;
    param2.old = param2.cur;
  }
#endif
  param3.cur = 1;
  param4.cur = 1;
  param4.max = GRID_LENGTH - getRow();

  if (!reload_slot_models) {
    load_slot_models();
    reload_slot_models = true;
  }

  uint16_t now = read_clock_ms();
  if (now < grid_lastclock) {
    grid_lastclock = now + GUI_NAME_TIMEOUT;
  }

  if (clock_diff(grid_lastclock, now) > GUI_NAME_TIMEOUT) {
    ///   DEBUG_DUMP(grid_lastclock);
    //   DEBUG_DUMP(read_clock_ms());
    if ((write_cfg) && (MidiClock.state != 2)) {
      mcl_cfg.cur_col = cur_col;
      mcl_cfg.cur_row = cur_row;

      mcl_cfg.col = param1.cur;
      mcl_cfg.row = param2.cur;

      mcl_cfg.tempo = MidiClock.get_tempo();
      DEBUG_PRINTLN(F("write cfg"));
      if (mcl_cfg.write_cfg()) {
        proj.store_config_from_system();
      }
      grid_lastclock = now;
      write_cfg = false;
      // }
    }
    row_state_scan();
  }

  if (mcl_cfg.grid_page_mode == PERF_ENC) {
     param1.checkHandle();
     param2.checkHandle();
     param3.checkHandle();
     param4.checkHandle();
  }

#ifndef PLATFORM_TBD
  // AVR: state 2 is the post-release countdown — popup auto-closes after
  // 800 ms. On TBD the popup is opened by a NO-button gesture instead of
  // a held BANK key, so there's no natural release to time off; the popup
  // stays up until trig→pattern or a re-press of NO closes it.
  if (bank_popup == 2 &&
      clock_diff(bank_popup_lastclock, now) > 800) {
    close_bank_popup();
    return;
  }
#endif

}

void GridPage::row_state_scan() {
  if (row_scan) {
    GridRowHeader header_tmp;
    row_scan--;

    GridRow row = GRID_LENGTH - row_scan - 1;
    proj.read_grid_row_header(&header_tmp, row, 0);
    bool state = header_tmp.is_empty();

    proj.read_grid_row_header(&header_tmp, row, 1);
    state |= header_tmp.is_empty();

    update_row_state(row, !state);
  }
}

void GridPage::update_row_state(GridRow row, bool state) {
  // DEBUG_PRINTLN(F("updating row state"));
  // DEBUG_PRINTLN(row);
  if (state) {
    SET_BIT128_P(row_states, row);
  } else {
    CLEAR_BIT128_P(row_states, row);
  }
}

GridRow GridPage::getRow() { return param2.cur; }

GridColumn GridPage::getCol() { return param1.cur; }

GridSpan GridPage::getWidth() { return GRID_WIDTH; }

void GridPage::load_slot_models() {
  DEBUG_PRINTLN(F("load slot models"));
  GridSpan row_shift = 0;
  if ((cur_row + param4.cur > MAX_VISIBLE_ROWS - 1)) {
    row_shift = cur_row + param4.cur - MAX_VISIBLE_ROWS;
  }
  GridRow base_row = getRow() - cur_row + row_shift;

  for (uint8_t n = 0; n < MAX_VISIBLE_ROWS; n++) {
    for (uint8_t x = 0; x < GRID_WIDTH; x++) {
      set_slot_label(slot_labels[n][x], '-', '-');
    }
    GridRow row = base_row + n;
    if (row >= GRID_LENGTH) { continue; }
    proj.read_grid_row_header(&row_headers[n], row, cur_grid);
    if (cur_grid != 0) {
      inherit_grid_x_row_name(row_headers[n], row);
    }
    update_row_state(row, row_headers[n].active);
    for (uint8_t x = 0; x < GRID_WIDTH; x++) {
      uint8_t track_type = row_headers[n].track_type[x];
      GridSlotLabelContext ctx = {row_headers[n].model[x], x};
#if defined(PLATFORM_TBD)
      ctx.slot = (GridSlot)(x + cur_grid * GRID_WIDTH);
      ctx.row = row;
#endif
      copy_grid_slot_label(track_type, ctx, slot_labels[n][x]);
    }
  }
}

// Cursor + print in one out-of-line call: each inline pair costs ~30 bytes.
static void text_at(uint8_t x, uint8_t y, const char *str) NOINLINE();
static void text_at(uint8_t x, uint8_t y, const char *str) {
  oled_cursor(x, y);
  oled_display.print(str);
}
[[maybe_unused]] static void text_at_P(uint8_t x, uint8_t y, const char *str_P) NOINLINE();
[[maybe_unused]] static void text_at_P(uint8_t x, uint8_t y, const char *str_P) {
  oled_cursor(x, y);
  mcl_print_P(str_P);
}

void GridPage::display_counters() {
  uint8_t y_offset = 8;

  char val[3];

  mcl_gui.put_value_at2(MidiClock.bar_counter, val);

  if (val[0] == '0') {
    val[0] = (char)0x60;
  }

  oled_font_small();
  text_at(24, y_offset, val);

  mcl_print_P(mclstr_colon);
  oled_display.print(MidiClock.beat_counter);

  if ((mcl_actions.next_transition != (uint16_t)-1) &&
      (MidiClock.bar_counter <= mcl_actions.nearest_bar) &&
      (mcl_actions.nearest_beat > MidiClock.beat_counter ||
       mcl_actions.nearest_bar != MidiClock.bar_counter)) {
    mcl_gui.put_value_at2(mcl_actions.nearest_bar, val);

    if (val[0] == '0') {
      val[0] = (char)0x60;
      if (val[1] == '0') {
        val[1] = (char)0x60;
      }
    }
    text_at(24, y_offset + 8, val);
    mcl_print_P(mclstr_colon);
    oled_display.print(mcl_actions.nearest_beat);
  }
}

static void draw_grid_device_label(uint8_t x, uint8_t y,
                                   MidiDevice *device) {
  const char *name = device->name;
  char label[3] = {name[0] ? name[0] : ' ', name[1] ? name[1] : ' ', '\0'};
  text_at(x, y, label);
}

static void draw_bank_row_label(uint8_t row, char *val) NOINLINE();
static void draw_bank_row_label(uint8_t row, char *val) {
  oled_display.print((char)('A' + row / 16));
  mcl_gui.put_value_at2((row & 0x0F) + 1, val);
  oled_display.print(val);
}

void GridPage::display_grid_info() {
  uint8_t y_offset = 8;

  oled_display.setFont(&Elektrothic);
  oled_cursor(0, 10);
  oled_display.print((uint16_t)(MidiClock.get_tempo() + 0.5f));

  display_counters();
  oled_font_small();
  //  mcl_print_P(mclstr_colon);
  // oled_display.print(MidiClock.step_counter);


  uint8_t tri_x = 10, tri_y = 12;
  if (MidiClock.state == 2) {
    oled_display.drawFastVLine(tri_x, tri_y, 5, WHITE);
    oled_display.fillTriangle_3px(tri_x + 1, tri_y, WHITE);
  }
  if (MidiClock.state == 0) {
    oled_display.fillRect(tri_x - 1, tri_y, 2, 5, WHITE);
    oled_display.fillRect(tri_x + 2, tri_y, 2, 5, WHITE);
  }

  draw_grid_device_label(0, y_offset + 1 + 1 * 8,
                         device_manager.primary_device());
  draw_grid_device_label(0, y_offset + 3 * 8,
                         device_manager.secondary_device());

  display_position();

  oled_display.fillRect(0, y_offset + 2 * 8 - 6, 37, 7, WHITE);

  oled_text_color(BLACK, WHITE);
  if (row_headers[cur_row].name[0] != '\0') {
    char rowname[10];
    strncpy(rowname, row_headers[cur_row].name, 9);
    rowname[9] = '\0';

    text_at(1, y_offset + 2 * 8, rowname);
  }

  oled_text_color(WHITE, BLACK);
}
// Bottom line of the info column: grid, column and row, e.g. "X:01 A01".
// "X:05" in the bottom line's grid:column place; column 0 prints "X:--".
static void print_grid_column(uint8_t grid, uint8_t column, char *val)
    NOINLINE();
static void print_grid_column(uint8_t grid, uint8_t column, char *val) {
  oled_cursor(10, 32);
  oled_display.print((char)('X' + grid));
  oled_display.print(':');
  if (column) {
    mcl_gui.put_value_at2(column, val);
  } else {
    val[0] = val[1] = '-';
  }
  val[2] = '\0';
  oled_display.print(val);
}

void GridPage::display_position() {
  char val[4];
  print_grid_column(cur_grid, param1.cur + 1, val);
  mcl_print_P(mclstr_space);
  draw_bank_row_label(param2.cur, val);
}

bool GridPage::is_slot_queue(uint8_t x, uint8_t y) {
  if (show_slot_menu) {
    return false;
  }
  GridSlot slot = cur_grid * GRID_WIDTH + x;
  if (mcl_actions.chains[slot].is_mode_queue()) {
    for (uint8_t n = 0; n < mcl_actions.chains[slot].num_of_links; n++) {
      if (mcl_actions.chains[slot].rows[n] == y) {
        return true;
      }
    }
  }
  return false;
}

#if MCL_GRID_IO_PANEL
// Dotted ring in the 1-pixel gap around a grid cell, marching every 140 ms:
// the same walk and 2-on/2-off pattern as the SPS-X grid's dotted orbit.
// 11x9 ring: top left to right, down, back along the bottom, up. steps 29
// leaves out the left side (the last 7 steps), for a ring whose left
// neighbour's right side already stands on that column.
static void draw_pending_orbit(uint8_t x, uint8_t y, uint8_t idx,
                               uint8_t steps) {
  int8_t dx = 1, dy = 0;
  for (uint8_t i = 0; i < steps; i++, idx++) {
    if ((idx & 3) < 2) {
      oled_display.drawPixel(x, y, WHITE);
    }
    if (i == 10 || i == 18 || i == 28) {
      // Corner: turn clockwise.
      int8_t t = dx;
      dx = -dy;
      dy = t;
    }
    x += dx;
    y += dy;
  }
}
#endif

void GridPage::display_grid() {
  uint8_t x_offset = 43;
  uint8_t y_offset = 8;

  oled_font_small();

  GridColumn base_col = getCol() - cur_col;
  GridRow base_row = getRow() - cur_row;
  GridSpan grid_width = getWidth();
  bool blink_hint = MidiClock.getBlinkHint(false);

//  encoders[1]->handler = NULL;

#if MCL_GRID_IO_PANEL
  const uint16_t io_mask = io_mode ? io_visible_mask() : 0;
  const GridRow io_row = getRow();
  const uint16_t now = read_clock_ms();
  const uint8_t orbit_phase = (uint8_t)(now / 140) & 3;
  if (settle_mask && clock_diff(settle_clock, now) >= 420) {
    settle_mask = 0;
  }
  const uint16_t settle16 = settle_bits[cur_grid * 2] |
                            (settle_bits[cur_grid * 2 + 1] << 8);
  const uint16_t first_bit = 1u << base_col;
#endif

  GridSpan row_shift = 0;
  GridSpan col_shift = 0;
  if (show_slot_menu) {
    if (cur_col + param3.cur > MAX_VISIBLE_COLS - 1) {

      col_shift = cur_col + param3.cur - MAX_VISIBLE_COLS;
    }

    if (cur_row + param4.cur > MAX_VISIBLE_ROWS - 1) {
      row_shift = cur_row + param4.cur - MAX_VISIBLE_ROWS;
    }
  }
  for (uint8_t y = 0; y < MAX_VISIBLE_ROWS; y++) {

    uint8_t cur_posx = x_offset;
    uint8_t cur_posy = y_offset + y * 8;
    GridSpan w = grid_width;
#if MCL_GRID_IO_PANEL
    uint16_t col_bit = first_bit << col_shift;
    uint8_t orbit_run = 0; // pending slots just left of this one, same group
#endif
    for (uint8_t x = col_shift; x < MAX_VISIBLE_COLS + col_shift && x < w;
         x++) {
      oled_cursor(cur_posx, cur_posy);

      GridColumn track_idx = x + base_col;
      GridRow row_idx = y + base_row;

      uint8_t active_cue_color = WHITE;

      char *label = slot_labels[y][track_idx];
      //  Highlight the current cursor position + slot menu apply range
      bool a = in_area(x, y + row_shift, cur_col, cur_row, param3.cur - 1,
                       param4.cur - 1);
#if MCL_GRID_IO_PANEL
      if (io_mode) {
        // Armed: the selected slots on the armed row invert (in group select,
        // the slots of the selected groups); the arrow drawn after the grid
        // marks the row in place of the cursor.
        a = row_idx == io_row &&
            (GridIOPage::show_track_type
                 ? GridIOPage::slot_matches_track_type_select(
                       track_idx + GRID_WIDTH * cur_grid)
                 : (io_mask & col_bit) != 0);
      }
#endif
      bool b = is_slot_queue(track_idx, row_idx);
      if (a ^ b) {
        oled_display.fillRect(cur_posx - 1, cur_posy - 6, 9, 7, WHITE);
        oled_text_color(BLACK, WHITE);
        active_cue_color = BLACK;
      } else {
        oled_text_color(WHITE, BLACK);
      }

      GridSlot track_grid_idx = track_idx + GRID_WIDTH * cur_grid;
      GridRow active_slot = active_slots[track_grid_idx];
      bool active = row_idx == active_slot;
      if (!active || !blink_hint) {
        oled_display.print(label);
        if (active) {
          // a gentle visual cue for active slots
          oled_display.drawPixel(cur_posx - 1, cur_posy - 6, active_cue_color);
        }
      }
#if MCL_GRID_IO_PANEL
      if ((active_slot == SLOT_PENDING &&
           mcl_actions.links[track_grid_idx].row == row_idx) ||
          mcl_actions.deferred_rows[track_grid_idx] == row_idx) {
        // Neighbouring pending rings share a column (11 px rings, 10 px
        // pitch): only the left ring's side is drawn there. Each ring starts
        // its dots where the one to its left reached (10 px on, 2 mod 4), so
        // the top and bottom edges march on evenly and the shared corners
        // match.
        draw_pending_orbit(cur_posx - 2, cur_posy - 7,
                           orbit_phase + 2 * orbit_run, orbit_run ? 29 : 36);
        orbit_run++;
      } else {
        orbit_run = 0;
        if (active && (settle16 & col_bit)) {
          oled_display.fillRect(cur_posx - 1, cur_posy - 6, 9, 7, INVERT);
        }
      }
      col_bit <<= 1;
#endif

      // tomThumb is 4x6
      if (track_idx % 4 == 3 && w >= 8) {
        if (y == 0) {
          // draw vertical separator
          mcl_gui.draw_vertical_dashline(cur_posx + 9, 3);
        }
        cur_posx += 12;
#if MCL_GRID_IO_PANEL
        orbit_run = 0; // rings across the group gap don't touch
#endif
      } else {
        cur_posx += 10;
      }
    }
  }

#if MCL_GRID_IO_PANEL
  if (io_mode) {
    // Load/Save armed: an arrow left of the armed row in place of the
    // separator (not while Load's rows are edited), and < / > at the grid's
    // edges for selected slots in the hidden columns.
    uint8_t ty = y_offset + cur_row * 8;
    if (!io_editing && !io_fade_page()) {
      oled_display.fillTriangle_3px(x_offset - 5, ty - 5, WHITE);
    }
    if (io_mask & (first_bit - 1)) {
      text_at_P(41, ty, PSTR("<"));
    }
    if (io_mask & ~((uint16_t)(first_bit << MAX_VISIBLE_COLS) - 1)) {
      text_at_P(125, ty, PSTR(">"));
    }
  } else
#endif
  // optionally, draw the first separator
  if ((base_col + col_shift) % 4 == 0) {
    mcl_gui.draw_vertical_dashline(x_offset - 3, 3);
  }
  oled_text_color(WHITE, BLACK);
}
void GridPage::display_slot_menu() {
  uint8_t y_offset = 8;
  grid_slot_page.draw_menu(1, y_offset, 39);
  // grid_slot_page.draw_scrollbar(36);
}

void GridPage::display_row_info() {
  GridSpan row_shift = 0;
  if ((cur_row + param4.cur > MAX_VISIBLE_ROWS - 1)) {
    row_shift = cur_row + param4.cur - MAX_VISIBLE_ROWS;
  }
  GridRow base_row = getRow() - cur_row + row_shift;
  char val[4];
  val[2] = '\0';

  for (uint8_t n = 0; n < MAX_VISIBLE_ROWS; n++) {
    GridRow row = base_row + n;
    if (row >= GRID_LENGTH) { return; }
    oled_cursor(27, (n + 1) * 8);
    draw_bank_row_label(row, val);
  }
}
#ifdef FPS
int frames;
int frameclock;
#endif
void GridPage::display() {

  #ifdef FPS
  if (clock_diff(frameclock, read_clock_ms()) >= 1000) {
  DEBUG_PRINT(F("FPS: ")); DEBUG_PRINTLN(frames);
  frames = 0;
  frameclock = read_clock_ms();
  }
  frames++;
  #endif
  oled_display.clearDisplay();
  oled_font_small();
  oled_text_color(WHITE, BLACK);
  if (!show_slot_menu) {
#if MCL_GRID_IO_PANEL
    if (io_mode) {
      io_display_panel();
    } else
#endif
    display_grid_info();
  } else {
    if (param4.cur > 4) {
     display_row_info();
    }
    else{
     display_slot_menu();
    }
  }
  display_grid();
#if MCL_GRID_IO_PANEL
  if (io_mode && GridIOPage::show_track_type) {
    // Hold YES: the group boxes from the old Load/Save pages, without their
    // title bar, 2 px lower so they reach the bottom edge. The LOAD/SAVE
    // chip and the top grid row stay visible above them; when that row is
    // the armed one, its slots in the selected groups invert there. TBD
    // draws them in the bottom half of its 64-row screen, under the grid.
#ifdef PLATFORM_TBD
    mcl_gui.draw_track_type_select(mcl_cfg.track_type_select, 34);
#else
    mcl_gui.draw_track_type_select(mcl_cfg.track_type_select, 2);
#endif
  }
#endif
  if (draw_encoders && clock_diff(draw_encoders_lastclock, read_clock_ms()) < 750) {
    oled_font_default();
    mixer_page.draw_encs();
  }
  else {
    draw_encoders = false;
  }
#ifdef DEBUGMODE
  if (row_scan) {
    mcl_gui.draw_progress_bar(8, 8, false, 18, 2, 18, 7, false);
  }
#endif

#if defined(PLATFORM_TBD) && defined(DEBUG_TBD_BUTTONS)
  {
    extern Ui tbd_ui;
    ui_data_t dbg = tbd_ui.CopyUiData();
    char buf[32];
    oled_font_default();
    oled_display.setTextSize(1);
    oled_text_color(WHITE, BLACK);
    oled_cursor(0, 32);
    snprintf(buf, sizeof(buf), "f:%02X d:%04X", dbg.f_btns, dbg.d_btns);
    oled_display.print(buf);
    oled_cursor(0, 40);
    snprintf(buf, sizeof(buf), "m:%04X p:%02X%02X%02X%02X",
      dbg.mcl_btns,
      dbg.pot_states[0], dbg.pot_states[1],
      dbg.pot_states[2], dbg.pot_states[3]);
    oled_display.print(buf);
    oled_cursor(0, 48);
    snprintf(buf, sizeof(buf), "fl:%02X ml:%04X", dbg.f_btns_long_press, dbg.mcl_btns_long_press);
    oled_display.print(buf);
  }
#endif

  // The SPS bottom-32 strip is now an overlay (SpsStripPage) installed
  // by SpsMode when the latch turns on; rendering happens in
  // GuiClass::display() via the GUI overlay slot, not here.
}

void rename_row() {
  const char *my_title = "ROW NAME:";
  GridRowHeader row_headers[NUM_GRIDS];

  for (uint8_t n = 0; n < NUM_GRIDS; n++) {
    proj.read_grid_row_header(&row_headers[n], grid_page.getRow(), n);
  }

  if (row_headers[0].active) {
    if (mcl_gui.wait_for_input(row_headers[0].name, my_title, 8)) {
      strcpy(row_headers[1].name, row_headers[0].name);
      for (uint8_t n = 0; n < NUM_GRIDS; n++) {
        proj.write_grid_row_header(&(row_headers[n]), grid_page.getRow(), n);
        proj.sync_grid(n);
      }
    }
  } else {
    gfx.alert("Error", "Row not active");
  }
  grid_page.load_slot_models();
  grid_slot_page.select_item(0);
}

void apply_slot_changes_cb() { grid_page.apply_slot_changes(); }

COLD_SMALL() void GridPage::apply_slot_changes(bool ignore_undo, bool ignore_func) {
  GridSpan width;
  GridSpan height;

  GridColumn _col = getCol();
  GridIndex target_grid = slot_menu_grid;

  bool activate_header = false;
  //old_col != 255 indicates that the grid selection spans grids x and y.
  if (old_col != 255) {
    _col = old_col;
    target_grid = 0;
  }

  uint8_t track_select_array[NUM_SLOTS];
  uint8_t load_mode_old = mcl_cfg.load_mode;
  uint8_t undo = (slot_clear || slot_paste) && slot_undo && !ignore_undo &&
                 slot_undo_x == _col && slot_undo_y == getRow();
  GridTrack temp_slot;
  DEBUG_PRINTLN(F("apply slot"));


  if (!ignore_func) {
    void (*row_func)() =
        grid_slot_page.menu.get_row_function(grid_slot_page.encoders[1]->cur);
    if (row_func != NULL) {
      DEBUG_PRINTLN(F("calling menu func"));
      (*row_func)();
      return;
    }
  }

  slot.set_load_sound(slot_load_sound != 0);

  width = old_col != 255 ? GRID_WIDTH - _col : param3.cur;
  height = param4.cur;

  uint8_t slot_dirty_mask = 0;
  if (slot.link.length != slot_original_link.length) {
    slot_dirty_mask |= SLOT_DIRTY_LENGTH;
  }
  if (slot.link.loops != slot_original_link.loops) {
    slot_dirty_mask |= SLOT_DIRTY_LOOPS;
  }
  if (slot.link.row != slot_original_link.row) {
    slot_dirty_mask |= SLOT_DIRTY_ROW;
  }
  if ((slot.link.speed ^ slot_original_link.speed) & 0x80) {
    slot_dirty_mask |= SLOT_DIRTY_LOAD_SOUND;
  }

  uint8_t slot_update = 0;

  if (!(slot_copy || slot_paste || slot_clear || slot_load || undo)) {
    if (slot_dirty_mask) {
      slot_update = 1;
      DEBUG_PRINTLN(F("Slot update"));
    }
  }
  if (undo == 1) {
    slot_clear = 0;
    slot_paste = 1;
  }

  cur_grid = target_grid;

  if (slot_copy == 1 || (slot_clear == 1 && !undo)) {
    const char *verb;
    if (slot_clear == 1) {
      slot_undo_x = _col;
      slot_undo_y = getRow();
      verb = mclstr_clear;
      slot_undo = 1;
    } else {
      slot_undo = 0;
      verb = mclstr_copy;
    }
    oled_display.textbox_P(verb, width > 0 ? mclstr_slots : mclstr_slot);
    uint8_t copy_w = (old_col != 255) ? width + param3.cur : width;
    mcl_clipboard.copy(_col + GRID_WIDTH * cur_grid, getRow(), copy_w, height);
  }
  if (slot_clear) {
    goto run;
  }

  else if (slot_paste == 1) {
    if (undo) {
      oled_display.textbox_P(mclstr_undo);
    } else {
      oled_display.textbox_P(mclstr_paste);
    }
    slot_undo = 0;
    mcl_clipboard.paste(_col + GRID_WIDTH * cur_grid, getRow());
  } else {
    if (slot_update == 1) {
      oled_display.textbox_P(mclstr_slot, mclstr_update);
    }

    if (slot_load) {
      if (height > 1) {
        mcl_cfg.load_mode = LOAD_QUEUE;
      }
      GridLoadPage::display_load();
    }
  run:

    oled_display.display();

    GridRowHeader header;

  again:

    if (slot_load == 1) {
      memset(track_select_array, 0, sizeof(track_select_array));
    }
    for (uint8_t y = 0; y < height && y + getRow() < GRID_LENGTH; y++) {
      GridRow ypos = y + getRow();
      proj.read_grid_row_header(&header, ypos, cur_grid);

      if (slot_clear && height > 8) {
        mcl_gui.draw_progress("", y, height);
      }
      for (uint8_t x = 0; x < width && x + _col < getWidth(); x++) {
        GridColumn xpos = x + _col;
        if (slot_clear == 1) {
          // Delete slot(s)
          proj.clear_slot_grid(xpos + cur_grid * GRID_WIDTH, ypos);
          header.update_model(xpos, 0, EMPTY_TRACK_TYPE);
        } else if (slot_update == 1) {
          // Save slot link data
          activate_header = true;
          if (!temp_slot.load_from_grid(xpos + cur_grid * GRID_WIDTH, ypos)) {
            continue;
          }
          if (apply_slot_dirty_fields(temp_slot, slot, slot_dirty_mask)) {
            temp_slot.store_in_grid(xpos + cur_grid * GRID_WIDTH, ypos);
          }
        } else if (slot_load == 1) {
          // if (height > 1 && y == 0) {
          //   mcl_actions.chains[xpos].init();
          // }
          track_select_array[xpos + cur_grid * GRID_WIDTH] = 1;
        }
      }
      if (slot_load == 1) {
        DEBUG_PRINTLN(F("slot load put"));
        grid_task.load_queue.put(mcl_cfg.load_mode,ypos, track_select_array);
      }
      // If all slots are deleted then clear the row name
      else if ((header.is_empty() && (slot_clear == 1)) || (activate_header)) {
        header.active = activate_header;
        header.name[0] = '\0';
        proj.write_grid_row_header(&header, ypos, cur_grid);
      }
    }
  }

  if ((slot_clear == 1) || (slot_paste == 1) || (slot_update == 1)) {
    proj.sync_grid(cur_grid);
  }
  if (old_col != 255 && slot_paste) {
    proj.sync_grid(1);
  }

  if (old_col != 255) {
    if (cur_grid == 0) {
      cur_grid = 1;
      width = param3.cur;
      _col = 0;
      goto again;
    }
    load_old_col();
    _col = getCol();
  }
  mcl_cfg.load_mode = load_mode_old;
  slot_apply = 0;
  slot_load = 0;
  slot_clear = 0;
  slot_copy = 0;
  slot_paste = 0;
  slot.load_from_grid(_col + cur_grid * GRID_WIDTH, getRow());
  slot_load_sound = slot.load_sound() ? 1 : 0;
  slot_menu_grid = cur_grid;
  slot_original_link = slot.link;
  old_col = 255;
}

bool GridPage::handleEvent(gui_event_t *event) {
#if MCL_GRID_IO_PANEL
  if (io_mode && io_handleEvent(event)) {
    return true;
  }
#endif
  if (EVENT_NOTE(event)) {
    uint8_t port = event->port;

    uint8_t track = event->source;
    if (!device_manager.port_supports(
            port, MidiDeviceCapability::MdTrigInterface)) {
      return true;
    }

    GridRow row = grid_page.bank * 16 + track;
    if (event->mask == EVENT_BUTTON_RELEASED) {
      if (grid_page.bank_popup > 0) {
        if (note_interface.notes_all_off()) {
          grid_page.bank_popup_loadmask = 0;
          grid_page.close_bank_popup();
        }
        return true;
      }
    }

    else if (event->mask == EVENT_BUTTON_PRESSED) {
      if (grid_page.bank_popup > 0) {

        uint8_t load_mode_old = mcl_cfg.load_mode;
        uint16_t loadmask = grid_page.bank_popup_loadmask;
        bool has_load = loadmask != 0;
        bool single_load = has_load && ((loadmask & (loadmask - 1)) == 0);

        // The first trig loads its row; a column that is chaining keeps
        // chaining up to the load's point (MCLActions::load_tracks).
        if (!has_load) {
          grid_page.jump_to_row(row);
          if (load_mode_old != LOAD_AUTO) {
            mcl_cfg.load_mode = LOAD_MANUAL;
          }
        }
        else {
          mcl_cfg.load_mode = LOAD_QUEUE;
        }

        if (single_load) {
          uint8_t n = 0;
          while ((loadmask & 1) == 0) {
            loadmask >>= 1;
            n++;
          }
          uint8_t r = grid_page.bank * 16 + n;
          grid_page.bank_popup_loadmask = 0;
          // A second trig builds a queue: the chains start afresh, and the
          // first trig's row goes in again as the queue's first entry.
          mcl_actions.init_chains();
          grid_page.load_row(n, r);
        }

        grid_page.load_row(track, row);

#ifdef PLATFORM_TBD
        // Track first trig and repaint the strip: head=red, chained=yellow.
        // Keep the OLED bank grid visible while selecting patterns.
        if (grid_page.bank_popup_first_trig == 255) {
          grid_page.bank_popup_first_trig = track;
        }
        {
          uint16_t loadmask = grid_page.bank_popup_loadmask;
          uint16_t head_mask = (uint16_t)1 << grid_page.bank_popup_first_trig;
          uint16_t chained_mask = loadmask & ~head_mask;
          // Layer order matters — later calls overwrite earlier per-trig
          // colours. Populated rows first as a dim red baseline, then the
          // chained set in yellow, then the head in bright red.
          constexpr uint32_t kRedDim    = ((uint32_t)48  << 16);
          constexpr uint32_t kYellow    = ((uint32_t)255 << 16) | ((uint32_t)200 << 8);
          constexpr uint32_t kRedBright = ((uint32_t)255 << 16);
          mcl_gui.set_trigleds_color(grid_row_bank_mask(grid_page.row_states, grid_page.bank),
                                     kRedDim);
          mcl_gui.set_trigleds_color(chained_mask, kYellow);
          mcl_gui.set_trigleds_color(head_mask, kRedBright);
        }
#else
        // AVR: a held BANK key is the gesture modifier — pressing a trig
        // while no BANK key is held collapses straight to "load and close".
        // TBD keeps the popup up across multiple trig presses; the
        // EVENT_BUTTON_RELEASED branch closes once all trigs are released.
        if (!key_interface.is_key_down(MDX_KEY_BANKA) &&
            !key_interface.is_key_down(MDX_KEY_BANKB) &&
            !key_interface.is_key_down(MDX_KEY_BANKC) &&
            !key_interface.is_key_down(MDX_KEY_BANKD)) {
          grid_page.close_bank_popup();
        }
#endif
        mcl_cfg.load_mode = load_mode_old;
        return true;
      }
    }
  }
  if (EVENT_CMD(event)) {

    uint8_t key = event->source;
    if (key_interface.is_key_down(MDX_KEY_PATSONG)) {
      if (show_slot_menu) {
        if (event->mask == EVENT_BUTTON_PRESSED) {
          switch (key) {
          case MDX_KEY_BANKA:
          case MDX_KEY_BANKB:
          case MDX_KEY_BANKC: {
          loadmode:
            mcl_cfg.load_mode = key - MDX_KEY_BANKA + 1;
            bool persistent = false;
            GridLoadPage::md_popup_title(mcl_cfg.load_mode, persistent);
            return true;
          }
          case MDX_KEY_NO: {
            goto next;
          }
          case MDX_KEY_PATSONGKIT: {
            set_active_row(grid_task.last_active_row);
            return true;
          }
          }
        }
        return grid_slot_page.handleEvent(event);
      }
    }
  next:
    if (event->mask == EVENT_BUTTON_PRESSED) {
      if (key != MDX_KEY_FUNC) {
        draw_encoders = false;
      }
      uint8_t inc = 1;
      bool func_down = key_interface.is_key_down(MDX_KEY_FUNC);
      if (func_down) {
        inc = 4;
      }
      if (show_slot_menu) {
#ifdef PLATFORM_TBD
        const bool is_arrow =
            key == MDX_KEY_UP || key == MDX_KEY_DOWN ||
            key == MDX_KEY_LEFT || key == MDX_KEY_RIGHT;
        if (is_arrow) {
          // TBD default: when the slot menu is open, arrows navigate the
          // menu first. Hold the normal FUNC modifier to adjust the slot
          // geometry instead.
          if (!func_down) {
            switch (key) {
            case MDX_KEY_UP:
              grid_slot_page.encoders[1]->cur -= 1;
              return true;
            case MDX_KEY_DOWN:
              grid_slot_page.encoders[1]->cur += 1;
              return true;
            case MDX_KEY_LEFT:
              grid_slot_page.encoders[0]->cur -= 1;
              return true;
            case MDX_KEY_RIGHT:
              grid_slot_page.encoders[0]->cur += 1;
              return true;
            default:
              break;
            }
          }
          switch (key) {
          case MDX_KEY_UP:
            param4.cur -= inc;
            return true;
          case MDX_KEY_DOWN:
            param4.cur += inc;
            return true;
          case MDX_KEY_LEFT:
            if (inc > 1) {
              inc = 4;
            }
            param3.cur = max(0, param3.cur - inc);
            return true;
          case MDX_KEY_RIGHT:
            if (inc > 1) {
              inc = 4;
            }
            param3.cur += inc;
            return true;
          default:
            break;
          }
        }
#endif
        switch (key) {
        // case MDX_KEY_NO: {
        //  goto slot_menu_off;
        //}
        case MDX_KEY_YES: {
          slot_load = 1;
          goto slot_menu_off;
        }
        case MDX_KEY_COPY: {
          slot_copy = 1;
          goto apply_slot_edit;
        }
        case MDX_KEY_CLEAR: {
          slot_clear = 1;
          goto apply_slot_edit;
        }
        case MDX_KEY_PASTE: {
          slot_paste = 1;
        apply_slot_edit:
          apply_slot_changes(false, true);
          init();
          // if (key_interface.is_key_down(MDX_KEY_NO)) {
          //  goto slot_menu_on;
          //}
          return true;
        }
        case MDX_KEY_UP: {
          param4.cur -= inc;
          return true;
        }
        case MDX_KEY_DOWN: {
          param4.cur += inc;
          return true;
        }
        case MDX_KEY_LEFT: {
          param3.cur = max(0, param3.cur - inc);
          return true;
        }
        case MDX_KEY_RIGHT: {
          param3.cur += inc;
          return true;
        }
        case MDX_KEY_BANKA:
        case MDX_KEY_BANKB:
        case MDX_KEY_BANKC: {
          if (!func_down) {
            goto loadmode;
          }
        }
        }
      }
      switch (key) {
      case MDX_KEY_SCALE: {
        cur_grid = !cur_grid;
        param1.max = getWidth() - 1;
        init();
        return true;
      }
      case MDX_KEY_UP: {
        param2.cur -= inc;
        reset_undo();
        return true;
      }
      case MDX_KEY_DOWN: {
        param2.cur += inc;
        reset_undo();
        return true;
      }
      case MDX_KEY_LEFT: {
        param1.cur = max(0, param1.cur - inc);
        reset_undo();
        return true;
      }
      case MDX_KEY_RIGHT: {
        param1.cur += inc;
        reset_undo();
        return true;
      }
      case MDX_KEY_YES: {
        key_interface.ignoreNextEvent(MDX_KEY_YES);
        if (!func_down) {
          goto load;
        }
        goto save;
      }
      case MDX_KEY_NO: {
        if (!show_slot_menu) {
          goto slot_menu_on;
        }
        break;
      }
      }
    }
    if (event->mask == EVENT_BUTTON_RELEASED) {
      if (bank_popup) {
        switch (key) {
        case MDX_KEY_BANKA:
        case MDX_KEY_BANKB:
        case MDX_KEY_BANKC:
        case MDX_KEY_BANKD: {
          uint8_t bank = key - MDX_KEY_BANKA;
          if (bank_popup == 1) {
            bank_popup = 2;
            bank_popup_lastclock = read_clock_ms();
            MD.draw_bank(bank);
          }
          return true;
        }
        }
      }
      switch (key) {
      case MDX_KEY_NO: {
        if (!key_interface.is_key_down(MDX_KEY_PATSONG)) {
          goto slot_menu_off;
        }
        return true;
      }
      }
    }
  }

  if (EVENT_BUTTON(event)) {

    if (!show_slot_menu) {
      if (EVENT_RELEASED(event, Buttons.BUTTON1)) {
      save:
#if MCL_GRID_IO_PANEL
        io_begin(GRID_IO_SAVE);
#else
        mcl.setPage(GRID_SAVE_PAGE);
#endif

        return true;
      }

#if MCL_GRID_IO_PANEL && !defined(PLATFORM_TBD)
      // Load|Yes opens Load on the press. With Save|No held the press is the
      // config chord below; with Load or Save armed the panel has the button.
      if (EVENT_PRESSED(event, Buttons.BUTTON4) && !io_mode &&
          !BUTTON_DOWN(Buttons.BUTTON1)) {
#else
      if (EVENT_RELEASED(event, Buttons.BUTTON4)) {
#endif
      load:
#if MCL_GRID_IO_PANEL
        io_begin(GRID_IO_LOAD);
#else
        mcl.setPage(GRID_LOAD_PAGE);
#endif

        return true;
      }
    }

    if (EVENT_PRESSED(event, Buttons.BUTTON3)) {
      if (key_interface.is_key_down(MDX_KEY_NO)) { return true; }
    slot_menu_on:
      DEBUG_DUMP(getCol());
      DEBUG_DUMP(getRow());
      if (!slot.load_from_grid(getCol() + cur_grid * GRID_WIDTH, getRow())) { return true; }
      DEBUG_PRINTLN(F("what's in the slot"));
      DEBUG_DUMP(slot.link.loops);
      DEBUG_DUMP(slot.link.row);
      slot_load_sound = slot.load_sound() ? 1 : 0;
      slot_menu_grid = cur_grid;
      slot_original_link = slot.link;
      encoders[0] = &grid_slot_param1;
      encoders[1] = &grid_slot_param2;
      encoders[2] = &param3;
      encoders[3] = &param4;
      param3.cur = 1;
      param4.cur = 1;
      slot_apply = 0;
      old_col = 255;
      if (!slot.is_ext_track()) {
        grid_slot_page.menu.enable_entry(1, true);
        grid_slot_page.menu.enable_entry(2, false);
      } else {
        grid_slot_page.menu.enable_entry(1, false);
        grid_slot_page.menu.enable_entry(2, true);
      }
      show_slot_menu = true;
      grid_slot_page.init();
      return true;
    }

    if (EVENT_RELEASED(event, Buttons.BUTTON3)) {
    slot_menu_off:
      if (show_slot_menu && !key_interface.is_key_down(MDX_KEY_NO)) {
        uint8_t old_undo = slot_undo;
        bool restore_undo = false;
        // Prevent undo from occuring when re-entering shift menu. Want to keep
        // undo flag in case user decides to undo with MD GUI.
        if (!(slot_copy || slot_paste || slot_clear || slot_load)) {
          slot_undo = 0;
          restore_undo = true;
        }

        apply_slot_changes();

        if (restore_undo) {
          slot_undo = old_undo;
        }

        DEBUG_PRINTLN(F("here"));
        init();
      }
      return true;
    }
#ifndef PLATFORM_TBD
    // A + X → SYSTEM_PAGE on AVR. TBD reaches the system page via the
    // TL → TR chord in tbd_handleEvent so this slot is free for Y/X
    // chord assignments.
    if ((EVENT_PRESSED(event, Buttons.BUTTON1) &&
         BUTTON_DOWN(Buttons.BUTTON4)) ||
        (EVENT_PRESSED(event, Buttons.BUTTON4) &&
         BUTTON_DOWN(Buttons.BUTTON1))) {
      system_page.isSetup = false;
      mcl.pushPage(SYSTEM_PAGE);
      return true;
    }
#endif
  }
  return false;
}


#if MCL_GRID_IO_PANEL
// ---------------------------------------------------------------------------
// Load / Save armed on the Grid page (AVR and TBD). YES or BUTTON4 arms Load,
// FUNC+YES or BUTTON1 arms Save.
// The left info column shows the settings, the grid shows the selection, and
// releasing every trig loads or saves the selected slots, as the old Load and
// Save pages did.
// ---------------------------------------------------------------------------

// Print a number right-aligned to x = right (TomThumb digits are 4 px);
// 0 prints " -": TomThumb's space is 2 px, so the dash sits centred
// between the two digit columns.
static void print_right(uint8_t value, uint8_t right, uint8_t y) {
  char str[4] = {' ', '-', '\0'};
  if (value) {
    mcl_gui.put_value_at(value, str);
  }
  text_at(right - 4 * strlen(str), y, str);
}

// "LEN 8", "QUANT 16": label on the left, value at the right edge. The label
// starts at x 1 so the edit box (x 0 to 36) shows 1 px each side, as in menus.
static void panel_row(uint8_t y, const char *label_P, uint8_t value) {
  text_at_P(1, y, label_P);
  print_right(value, 37, y);
}

// A fade length of 2^n steps, right-aligned like the panel's other values:
// one digit up to 8, two to 64, three to 512, then 1024.
static void print_fade_steps(uint8_t n, uint8_t y) NOINLINE();
static void print_fade_steps(uint8_t n, uint8_t y) {
  uint8_t digits = n < 4 ? 1 : n < 7 ? 2 : n < 10 ? 3 : 4;
  oled_cursor(37 - 4 * digits, y);
  oled_display.print(1u << n);
}

// While Load|Yes is held, or [Global] on the Machinedrum, the Load panel's
// settings are edited with one cursor walking down its stops: the mode (the
// chip), QUANT, Queue's LEN (in Queue), then FADE's MODE and LEN, which show
// on the FADE screen (the one holding FADE opens). Knob 2 or UP/DOWN move
// the cursor, the picked row is inverted; knob 1 or LEFT/RIGHT change it.
// Otherwise the knobs stay the Grid page's own (grid_knobs).
enum : uint8_t {
  IO_ITEM_MODE,
  IO_ITEM_QUANT,
  IO_ITEM_QLEN,
  IO_ITEM_FMODE,
  IO_ITEM_FLEN
};
// Knob 2 during the edit; its value is the cursor's stop.
static MCLEncoder io_menu_line(IO_ITEM_FLEN, 0, ENCODER_RES_PAT);
// The first FADE stop: in Queue its LEN comes first.
static uint8_t io_fade_stop() {
  return mcl_cfg.load_mode == LOAD_QUEUE ? IO_ITEM_FMODE : IO_ITEM_QLEN;
}
// What the cursor's stop edits (outside Queue there is no QLEN stop).
static uint8_t io_item() {
  const uint8_t stop = io_menu_line.cur;
  return stop < IO_ITEM_QLEN || mcl_cfg.load_mode == LOAD_QUEUE ? stop
                                                                : stop + 1;
}
// Knob 1 during the edit: rests at 1, and io_loop takes each click off it.
static MCLEncoder io_menu_value(2, 0, ENCODER_RES_PAT);

// FADE's mode as a filled 9 x 5 px graphic, the small font's height, at the
// row's right (x 27 .. 35, baseline y), 1 px inside the edit box's edge: AUT
// a dip, IN a rising ramp, OUT a falling one.
static void draw_fade_mode(uint8_t y) {
  const uint8_t mode = mcl_cfg.load_fade >> 1;
  for (uint8_t i = 0; i < 9; i++) {
    const uint8_t h = mode == LOAD_FADE_FORCE_IN    ? (i + 3) / 2
                      : mode == LOAD_FADE_FORCE_OUT ? (11 - i) / 2
                                                    : (i > 4 ? i - 4 : 4 - i) + 1;
    oled_display.drawFastVLine(27 + i, y - h, h, WHITE);
  }
}


// The rows are edited while the Machinedrum's [Global] is held, or once
// Load|Yes has been held this long (io_loop). Load|Yes released sooner was a
// tap, which closes Load.
constexpr uint16_t IO_EDIT_HOLD_MS = 200;
// When Load|Yes went down, and whether it has been down long enough since.
static uint16_t io_tap_clock;
static bool io_held;
// The press of Load|Yes that opened Load is still down: let go early, it
// leaves Load open.
static bool io_opening;

// The trig LEDs last sent while a value is edited: one bit for its trig, 0
// for none. IO_LEDS_SELECTION: the selection's LEDs are showing;
// IO_LEDS_UNKNOWN: the group popup had them.
constexpr uint16_t IO_LEDS_SELECTION = 0xFFFF;
constexpr uint16_t IO_LEDS_UNKNOWN = 0xFFFE;
static uint16_t io_edit_leds = IO_LEDS_SELECTION;
// The Slide key (FADE) held on Load: once held past a tap, or used, the edit
// runs on FADE's two stops (the FADE screen). Released with nothing used
// before the hold time, it was a tap, which turns FADE on or off.
static bool io_slide;
static bool io_slide_used;
// When Slide went down: FADE's off row shows once it is held IO_EDIT_HOLD_MS,
// so a tap goes from blank straight to the length it turns on.
static uint16_t io_slide_clock;

static bool io_fade_page() {
  return io_slide &&
         (io_slide_used ||
          clock_diff(io_slide_clock, read_clock_ms()) >= IO_EDIT_HOLD_MS);
}

// One step of FADE's mode, in one order everywhere: - (off), AUT, IN, OUT.
// Off keeps the direction, so a tap turns FADE back on in its last mode.
static void io_step_fade_mode(int8_t dir) {
  const uint8_t fade = mcl_cfg.load_fade;
  uint8_t state = (fade & 1) ? 1 + (fade >> 1) : 0;
  state = limit_value(state, dir, 0, 1 + LOAD_FADE_FORCE_OUT);
  mcl_cfg.load_fade = state ? ((state - 1) << 1) | 1 : fade & ~1;
}

// SYSTEM > LOAD/SAVE LEGACY: while Load is open its knobs are its settings,
// in the order of the released Load page (5.02): knob 1 the mode, knob 2
// Queue's LEN, knobs 3 and 4 QUANT.
static void io_load_knobs(Encoder **encoders) {
  encoders[0] = &gridload_param1;
  encoders[1] = &gridio_param2;
  encoders[2] = &gridio_param4;
  encoders[3] = &gridio_param4;
}

// Whether the arrows move the grid with the panel left armed: Load's in
// CLOSE and STAY, Save's in STAY. In LEGACY they disarm and move the
// cursor, as MCL always did.
static bool io_arrows_scroll(bool load) {
  return mcl_cfg.io_stay == LOAD_SAVE_STAY ||
         (load && mcl_cfg.io_stay == LOAD_SAVE_CLOSE);
}

// Start or end editing the Load panel's rows: knob 1 changes the chosen
// row and knob 2 picks it, or the knobs go back to the Grid page's (Load's
// own in LEGACY).
static void io_rows_edit(Encoder **encoders, bool on) {
  io_editing = on;
  if (on) {
    io_menu_value.cur = 1;
    // Knobs 3 and 4 get the Grid page's third and fourth encoders, which do
    // nothing outside the slot menu.
    encoders[0] = &io_menu_value;
    encoders[1] = &io_menu_line;
    encoders[2] = &param3;
    encoders[3] = &param4;
  } else if (mcl_cfg.io_stay == LOAD_SAVE_LEGACY) {
    io_load_knobs(encoders);
  } else {
    grid_knobs(encoders);
  }
}

// One step of the cursor's stop. pressed: knob 1 is pressed while turned.
static void io_setting_step(int8_t dir, bool pressed) {
  const uint8_t item = io_item();
  if (item == IO_ITEM_FMODE) {
    io_step_fade_mode(dir);
    return;
  }
  if (item == IO_ITEM_FLEN) {
    // QUANT's length (-), then 1 .. 1024 steps.
    mcl_cfg.load_fade_len =
        limit_value(mcl_cfg.load_fade_len, dir, 0, LOAD_FADE_MAX);
    return;
  }
  // The mode, QUANT or Queue's LEN: one click of the knob object that holds
  // it (io_loop stores the result). Pressed, QUANT and LEN move by one and
  // not to the next power of two.
  Encoder *knob = item == IO_ITEM_QUANT  ? &gridio_param4
                  : item == IO_ITEM_MODE ? &gridload_param1
                                         : &gridio_param2;
  encoder_t click = {};
  click.normal = dir * (knob->rot_res * ENCODER_RES_MULTIPLIER + 1);
  click.button = pressed && item != IO_ITEM_MODE;
  knob->update(&click);
}

// Nothing selected: no slots, DEST or group popup, and the trig LEDs clear.
static void io_clear_selection() {
  GridIOPage::show_track_type = false;
  GridIOPage::show_offset = false;
  GridIOPage::track_select = 0;
  GridIOPage::offset = 255;
  note_interface.init_notes();
  GridIOPage::paint_track_select_leds();
}

// Save reads the Machinedrum's current pattern before saving.
static void io_read_md_pattern() {
#ifdef PLATFORM_TBD
  // A TBD may run without an MD: ask only one that is connected and
  // assigned to the grid, as TBD's Save page did.
  if (MD.connected && (device_manager.primary_device() == &MD ||
                       device_manager.secondary_device() == &MD))
#endif
    MD.getCurrentPattern(CALLBACK_TIMEOUT);
}

// Save has saved since it was armed (SYSTEM > LOAD/SAVE: STAY): read the
// pattern again before the next save, as the MD may have moved on.
static bool io_saved;

void GridPage::io_begin(uint8_t mode) {
  io_mode = mode;
  io_editing = false;
  io_edit_leds = IO_LEDS_SELECTION;
  io_slide = false;
  io_slide_used = false;
  GridIOPage::old_grid = cur_grid;
  io_saved_grid = cur_grid;
  draw_encoders = false;

#ifdef PLATFORM_TBD
  // A parameter strip in the bottom half would take the knobs; Load/Save
  // replaces it, as TBD's Load/Save overlay did.
  GUI.clearOverlay();
#endif
  io_clear_selection();
  key_interface.on();

  if (mode == GRID_IO_LOAD) {
    // The knobs stay the Grid page's own until the rows are edited. The
    // settings live in these knob objects, which io_loop compares with
    // mcl_cfg, not with their old values.
    gridload_param1.cur = mcl_cfg.load_mode;
    gridio_param2.cur = mcl_cfg.chain_queue_length;
    gridio_param4.cur = mcl_cfg.chain_load_quant;
    if (mcl_cfg.io_stay == LOAD_SAVE_LEGACY) {
      io_load_knobs(encoders);
    }
    GridLoadPage::md_popup_title(mcl_cfg.load_mode);
    // Opened by a Load|Yes press that is still down (Load opens on the press
    // on AVR): it times the hold like a press inside Load.
    io_opening = BUTTON_DOWN(Buttons.BUTTON4);
    io_tap_clock = read_clock_ms();
  } else {
    io_saved = false;
    io_read_md_pattern();
    reload_slot_models = false;
    MD.popup_text_P(mclstr_save_slots, true);
  }
}

void GridPage::io_end(bool reinit) {
  io_mode = GRID_IO_OFF;
  io_editing = false;
  GridIOPage::show_track_type = false;
  GridIOPage::show_offset = false;
  GridIOPage::offset = 255;
  key_interface.send_md_leds();
  MD.popup_text(127, 2);
  note_interface.init_notes();
  // Back to the grid shown when Load/Save was armed (SCALE may have changed
  // it); init() reloads the slot labels, now or when the Grid page opens.
  cur_grid = io_saved_grid;
  if (reinit) {
    init();
  }
}

void GridPage::io_loop() {
  // The rows are edited while Load|Yes has been held long enough or [Global]
  // is held, and not while DEST is open.
  io_held = BUTTON_DOWN(Buttons.BUTTON4) &&
            clock_diff(io_tap_clock, read_clock_ms()) >= IO_EDIT_HOLD_MS;
  const bool edit = !GridIOPage::show_offset &&
                    (io_held || key_interface.is_key_down(MDX_KEY_PATSONG) ||
                     io_fade_page());
  if (edit != io_editing) {
    io_rows_edit(encoders, edit);
  }
  if (io_editing) {
    // The stops follow the mode (BANK A-C may change it mid-edit); with FADE
    // held only FADE's two.
    io_menu_line.min = io_slide ? io_fade_stop() : 0;
    io_menu_line.max = io_fade_stop() + 1;
    io_menu_line.cur =
        constrain(io_menu_line.cur, io_menu_line.min, io_menu_line.max);
    const int8_t dir = io_menu_value.cur - 1;
    io_menu_value.cur = 1;
    if (dir != 0 && !GridIOPage::show_offset) {
      io_setting_step(dir, BUTTON_DOWN(Buttons.ENCODER1));
    }
  }
  // While QUANT or FADE is edited the trig matching its value is lit: trig
  // n is 2^n steps, so QUANT's LED bit is its step count halved (none for
  // a count that is not a power of two) and a fade length code of n + 1
  // lights trig n. Otherwise the selection's LEDs show.
  uint16_t leds = IO_LEDS_SELECTION;
  if ((io_editing || io_slide) && !GridIOPage::show_offset) {
    const uint8_t item = io_item();
    if (item == IO_ITEM_QUANT) {
      const uint8_t quant = mcl_cfg.chain_load_quant;
      leds = (quant & (quant - 1)) ? 0 : quant >> 1;
    } else if (item >= IO_ITEM_FMODE) {
      const uint8_t len = mcl_cfg.load_fade_len;
      leds = (mcl_cfg.load_fade & 1) && len >= 2 ? (uint16_t)1 << (len - 2) : 0;
    }
  }
  if (GridIOPage::show_track_type) {
    // The group popup has the LEDs; they are set again once it closes.
    io_edit_leds = IO_LEDS_UNKNOWN;
  } else if (leds != io_edit_leds) {
    io_edit_leds = leds;
    if (leds == IO_LEDS_SELECTION) {
      GridIOPage::paint_track_select_leds();
    } else {
      mcl_gui.set_trigleds(leds, TRIGLED_EXCLUSIVE);
    }
  }
  if (GridIOPage::show_offset) {
    // Picking a destination: the settings stay as they are.
    gridload_param1.cur = mcl_cfg.load_mode;
    gridio_param2.cur = mcl_cfg.chain_queue_length;
    gridio_param4.cur = mcl_cfg.chain_load_quant;
    return;
  }
  // The mode, LEN and QUANT follow the knob objects that hold them (io_begin
  // set them equal): the mode with its MD popup (the bank keys change it
  // too), LEN in Queue, QUANT. FADE is set where it is changed.
  if ((uint8_t)gridload_param1.cur != mcl_cfg.load_mode) {
    mcl_cfg.load_mode = gridload_param1.cur;
    GridLoadPage::md_popup_title(mcl_cfg.load_mode);
  }
  if (mcl_cfg.load_mode == LOAD_QUEUE) {
    mcl_cfg.chain_queue_length = gridio_param2.cur;
  } else {
    // Knob 2 is LEN only in Queue (LEGACY); elsewhere it stays put.
    gridio_param2.cur = mcl_cfg.chain_queue_length;
  }
  mcl_cfg.chain_load_quant = gridio_param4.cur;
}

void GridPage::io_action(bool groups) {
  uint8_t track_select_array[NUM_SLOTS];
  const bool load = io_mode == GRID_IO_LOAD;
  // SYSTEM > LOAD/SAVE: STAY keeps Load and Save armed for the next one;
  // CLOSE closes them, as MCL always did.
  const bool stay = mcl_cfg.io_stay == LOAD_SAVE_STAY;
  if (groups) {
    if (!stay) {
      key_interface.off();
    }
    GridIOPage::track_select_array_from_type_select(track_select_array);
  } else {
    GridIOPage::populate_track_select_from_notes(track_select_array);
  }
  if (load) {
    if (groups) {
      mcl_actions.write_original = 1;
    }
    grid_task.load_queue.put(mcl_cfg.load_mode, getRow(), track_select_array,
                             GridIOPage::offset);
  } else {
    if (io_saved) {
      io_read_md_pattern();
    }
    oled_display.textbox_P(mclstr_save, groups ? mclstr_groups : mclstr_tracks);
    oled_display.display();
    mcl_actions.save_tracks(getRow(), track_select_array, SAVE_SEQ);
    io_saved = true;
  }
  if (!stay) {
    io_end();
    return;
  }
  // Armed for the next one (the arrows move the grid): the selection, DEST
  // and the group popup clear, and the Machinedrum's popup returns. After a
  // save the grid reloads the slot labels.
  io_clear_selection();
  if (load) {
    GridLoadPage::md_popup_title(mcl_cfg.load_mode);
  } else {
    reload_slot_models = false;
    MD.popup_text_P(mclstr_save_slots, true);
  }
}

void GridPage::io_group_select() {
#ifndef PLATFORM_TBD
  // The popup draws the device icons; they fit beside the grid's resources
  // (TBD's Grid page loads them already). Load stays armed across loads, so
  // load them once after each time init() loads the page's own: each copy
  // took another 1,631 bytes, and the third ran past the end of the
  // resource buffer.
  if (!io_icons_loaded) {
    RM_REQUIRE(0, __T_machine_names_short, __T_icons_knob, __T_icons_logo);
    R.use_icons_logo();
    io_icons_loaded = true;
  }
#endif
  GridIOPage::show_track_type = true;
  MD.popup_text_P(io_mode == GRID_IO_LOAD ? mclstr_load_groups
                                          : mclstr_save_groups,
                  true);
  mcl_gui.set_trigleds(mcl_cfg.track_type_select, TRIGLED_EXCLUSIVE);
}

void GridPage::io_toggle_grid() {
  GridIOPage::toggle_grid();
  cur_grid = GridIOPage::old_grid;
  reload_slot_models = false;
  GridIOPage::paint_track_select_leds();
}

// Show columns base .. base + 7, keeping the cursor's place in the view.
void GridPage::io_set_view(GridColumn base) {
  param1.cur = param1.old = base + cur_col;
}

// Selected slots of the shown grid: latched by SCALE, or held on the trigs.
uint16_t GridPage::io_visible_mask() {
  static_assert(GRID_WIDTH == 16 && NUM_SLOTS == 32,
                "track_select halves are one grid each");
  if (GridIOPage::show_offset) {
    // Choosing a destination: only the chosen slot inverts.
    GridSlot offset = GridIOPage::offset;
    return offset / GRID_WIDTH == cur_grid ? 1u << (offset % GRID_WIDTH) : 0;
  }
  uint16_t mask = cur_grid ? (uint16_t)(GridIOPage::track_select >> 16)
                           : (uint16_t)GridIOPage::track_select;
  // While the rows are edited the trigs set values, not slots: the note
  // interface records a press before the panel drops it, so a frame drawn
  // between would show it as selected.
  if (io_editing || io_slide) {
    return mask;
  }
  uint16_t bit = 1;
  for (uint8_t n = 0; n < GRID_WIDTH; n++, bit <<= 1) {
    // Trigs map to the shown grid; notes 16-31 address grid Y directly.
    if (note_interface.is_note(n) ||
        (cur_grid && note_interface.is_note(n + GRID_WIDTH))) {
      mask |= bit;
    }
  }
  return mask;
}

void GridPage::io_display_panel() {
  const bool load = io_mode == GRID_IO_LOAD;

  // Line 1: the action in the 5x7 font, in an inverted chip. On Load it is
  // the mode: LOAD (Manual), AUTO or QUE. While the rows are edited it is the
  // mode row, boxed only while that row is picked, so the picked row is the
  // only box.
  // With the cursor on FADE's stops (or FADE held) the panel is the FADE
  // screen: the chip reads FADE and the rows are FADE's MODE and LEN.
  const uint8_t item = io_item();
  const bool fade_page = load && io_editing && item >= IO_ITEM_FMODE;
  const bool boxed =
      fade_page || !(io_editing && item != IO_ITEM_MODE);
  // While the mode row is picked, the chip stands right of a filled arrow:
  // >[LOAD]. Otherwise it is in its place.
  const bool arrow = !fade_page && io_editing && item == IO_ITEM_MODE;
  const uint8_t cx = arrow ? 4 : 0;
  if (arrow) {
    oled_display.fillTriangle_3px(0, 2, WHITE);
  }
  if (boxed) {
    oled_display.fillRect(cx, 0, 25, 9, WHITE);
  }
  oled_font_default();
  oled_text_color(boxed ? BLACK : WHITE);
  const char *chip = mclstr_save;
  if (load) {
    chip = mcl_cfg.load_mode == LOAD_AUTO    ? mclstr_auto
           : mcl_cfg.load_mode == LOAD_QUEUE ? mclstr_queue_short
                                             : mclstr_load;
  }
  text_at_P(cx + 1, 1, fade_page ? mclstr_fade : chip);
  oled_font_small();
  oled_text_color(WHITE, BLACK);

  if (load) {
    // Line 1, beside the chip: the 16th-step count, for timing the trig
    // release against the quantise boundary (not while the rows are edited
    // or the FADE page shows).
    if (!io_editing && !fade_page) {
      char str[7];
      uint8_t step_count =
          (MidiClock.div16th_counter - mcl_actions.start_clock32th / 2) % 64;
      mcl_gui.put_value_at(step_count, str);
      text_at(37 - 4 * strlen(str), 8, str);
    }
    // Line 2: QUANT. Line 3: LEN, in Queue. A QUANT or LEN of 1 means "the
    // slot's own length" and shows "-". While a destination is picked, DEST
    // alone replaces them; the
    // destination itself goes on line 4.
    if (GridIOPage::show_offset) {
      text_at_P(0, 16, mclstr_dest);
    } else if (fade_page) {
      // MODE: - (off), or AUT, IN or OUT as on the FADE row. LEN: - (QUANT's
      // length) or steps.
      if (mcl_cfg.load_fade & 1) {
        text_at_P(1, 16, mclstr_mode);
        draw_fade_mode(16);
      } else {
        panel_row(16, mclstr_mode, 0);
      }
      const uint8_t len = mcl_cfg.load_fade_len;
      if (len) {
        text_at_P(1, 24, mclstr_len);
        print_fade_steps(len - 1, 24);
      } else {
        panel_row(24, mclstr_len, 0);
      }
      // The picked row is inverted.
      oled_display.fillRect(0, 10 + 8 * (item - IO_ITEM_FMODE), 37, 7,
                            INVERT);
    } else {
      uint8_t quant = mcl_cfg.chain_load_quant;
      panel_row(16, mclstr_quant, quant == 1 ? 0 : quant);
      if (mcl_cfg.load_mode == LOAD_QUEUE) {
        uint8_t len = mcl_cfg.chain_queue_length;
        panel_row(24, mclstr_len, len == 1 ? 0 : len);
      } else {
        // FADE in the other modes: "FADE -" when off, else FADE and its
        // mode's graphic: AUT (direction by column), IN or OUT (forced).
        // Off, the row shows only in the edit (FADE's stops follow QUANT).
        if (mcl_cfg.load_fade & 1) {
          text_at_P(1, 24, mclstr_fade);
          draw_fade_mode(24);
        } else if (io_editing) {
          panel_row(24, mclstr_fade, 0);
        }
      }
      if (io_editing && item != IO_ITEM_MODE) {
        // Editing: the row the knobs edit is inverted, with 1 px of the box
        // each side of the text, as in menus (the mode row is the chip).
        oled_display.fillRect(0, item == IO_ITEM_QUANT ? 10 : 18, 37, 7,
                              INVERT);
      }
    }
  } else {
    // SND + SEQ -> GRID, as on the old Save page.
    text_at_P(0, 17, mclstr_name_snd);
    text_at_P(0, 25, mclstr_seq);
    oled_hline(12, 14, 2, WHITE);
    oled_hline(12, 22, 2, WHITE);
    oled_display.drawFastVLine(14, 14, 9, WHITE);
    mcl_gui.draw_horizontal_arrow(15, 18, 5);
    text_at_P(21, 21, mclstr_grid);
  }
  // Line 4, Load and Save alike: the armed row, where the Grid page prints
  // it (after "X:01 "). While DEST is
  // open, the destination slot as grid:column ("Y:05") stands there instead,
  // where the Grid page prints the cursor's; "--" until a trig picks one.
  // It follows the DEST offset (GridIOPage::offset): once a trig has picked
  // the slot it stays there, after DEST closes, until the load that uses it
  // clears the offset.
  char val[4];
  const GridSlot offset = GridIOPage::offset;
  const bool picked = offset < NUM_SLOTS;
  if (load && (GridIOPage::show_offset || picked)) {
    print_grid_column(picked ? offset / GRID_WIDTH : cur_grid,
                      picked ? offset % GRID_WIDTH + 1 : 0, val);
  }
  // Not while Load's rows are edited or the FADE page shows.
  if (!io_editing && !fade_page) {
    oled_cursor(26, 32);
    draw_bank_row_label(param2.cur, val);
  }
}

bool GridPage::io_handleEvent(gui_event_t *event) {
  const bool load = io_mode == GRID_IO_LOAD;
  if (EVENT_NOTE(event)) {
    uint8_t track = event->source;
    if (track >= NUM_SLOTS) {
      return true;
    }
    // Editing the rows, or Slide held: the trigs set values, not slots. Trig
    // n sets QUANT, or the FADE length (turning FADE on), to 2^n steps, the
    // knob's powers of two: QUANT to 64 on trigs 1-6 (trigs 7-16 set 64
    // too), the fade to 1024 on trigs 1-10 (either FADE stop). On the mode
    // and Queue's LEN they do nothing.
    if ((io_editing || io_slide) && !GridIOPage::show_track_type &&
        track < 16) {
      if (event->mask == EVENT_BUTTON_PRESSED) {
        // With FADE held the cursor is on FADE's stops (set on its press).
        const uint8_t item = io_item();
        io_slide_used |= io_slide;
        if (GridIOPage::show_offset) {
          // Picking a destination: the settings stay as they are.
        } else if (item == IO_ITEM_QUANT) {
          // Trigs past 6 set QUANT's maximum, 64.
          gridio_param4.cur = track < 6 ? 2 << track : 64;
        } else if (item >= IO_ITEM_FMODE && track < LOAD_FADE_MAX - 1) {
          // Length code n is 2^(n-1) steps; the fade turns on. The lit
          // trig again: back to QUANT's length.
          const uint8_t len = track + 2;
          mcl_cfg.load_fade_len =
              (mcl_cfg.load_fade & 1) && mcl_cfg.load_fade_len == len ? 0
                                                                       : len;
          mcl_cfg.load_fade |= 1;
        }
        // Its release may come after the edit: it is ignored.
        note_interface.ignoreNextEvent(track);
      }
      // Not a slot pick: the note interface recorded this press, or the
      // release of a trig held from before the edit, and the grid and the
      // LEDs show its notes as the selection, so drop the note again.
      CLEAR_BIT32(note_interface.notes_on, track);
      CLEAR_BIT32(note_interface.notes_off, track);
      return true;
    }
    if (event->mask == EVENT_BUTTON_PRESSED) {
      if (GridIOPage::show_track_type) {
        if (track < 5) {
          TOGGLE_BIT16(mcl_cfg.track_type_select, track);
          mcl_gui.set_trigleds(mcl_cfg.track_type_select, TRIGLED_EXCLUSIVE);
        }
        return true;
      }
      if (track < GRID_WIDTH) {
        // A trig outside the view pages the grid to it.
        GridColumn base = param1.cur - cur_col;
        if (track < base || track >= base + MAX_VISIBLE_COLS) {
          io_set_view(track < MAX_VISIBLE_COLS ? 0
                                               : GRID_WIDTH - MAX_VISIBLE_COLS);
        }
      }
      if (GridIOPage::show_offset) {
        GridIOPage::offset = GridIOPage::slot_for_note(track);
      }
      GridIOPage::paint_track_select_leds();
    } else if (!GridIOPage::show_track_type) {
      GridIOPage::paint_track_select_leds();
      if (note_interface.notes_all_off()) {
        if (GridIOPage::show_offset) {
          GridIOPage::show_offset = false;
          note_interface.init_notes();
          GridIOPage::paint_track_select_leds();
        } else if (!BUTTON_DOWN(Buttons.BUTTON2)) {
          io_action(false);
        }
      }
    }
    return true;
  }

  if (EVENT_CMD(event)) {
    uint8_t key = event->source;
    if (event->mask == EVENT_BUTTON_PRESSED) {
      switch (key) {
      case MDX_KEY_YES:
        io_group_select();
        return true;
      case MDX_KEY_NO:
        io_end();
        return true;
      case MDX_KEY_SCALE:
        io_toggle_grid();
        return true;
      case MDX_KEY_STOP:
      case MDX_KEY_PLAY:
        // The transport keys do their job and Load stays armed: a fade
        // loaded while stopped starts on PLAY. Save disarms, as below.
        if (load) {
          return false;
        }
        io_end();
        return false;
      case MDX_KEY_BANKD:
        // The Machinedrum's SLIDE key arrives as bank D: FADE. Held, the
        // edit opens on FADE's stops (the FADE screen): the arrows and knobs
        // pick and change MODE and LEN, the trigs set the length; a tap (on
        // the release) turns FADE off or on again in its last mode. In Queue
        // too, where FADE is set for the Manual and Auto loads (queue loads
        // do not fade).
        if (load && !GridIOPage::show_offset) {
          io_slide = true;
          io_slide_used = false;
          io_slide_clock = read_clock_ms();
          if (io_menu_line.cur < io_fade_stop()) {
            io_menu_line.cur = io_fade_stop();
          }
        }
        return true;
      case MDX_KEY_FUNC:
        if (load && mcl_cfg.load_mode == LOAD_MANUAL) {
          GridIOPage::show_offset = !GridIOPage::show_offset;
          note_interface.init_notes();
          if (GridIOPage::show_offset) {
            GridIOPage::offset = 255;
          }
        }
        return true;
      case MDX_KEY_BANKA:
      case MDX_KEY_BANKB:
      case MDX_KEY_BANKC:
        // The mode keys, like the knobs, do nothing while DEST is open.
        if (load && !GridIOPage::show_offset &&
            !key_interface.is_key_down(MDX_KEY_FUNC)) {
          gridload_param1.cur = key - MDX_KEY_BANKA + 1;
        }
        return true;
      case MDX_KEY_LEFT:
      case MDX_KEY_RIGHT:
        // Editing, or FADE held: LEFT/RIGHT change the cursor's stop.
        if (load && (io_editing || io_slide)) {
          io_setting_step(key == MDX_KEY_RIGHT ? 1 : -1, false);
          io_slide_used |= io_slide;
          return true;
        }
        // The trigs pick the columns, so where the arrows move the grid
        // (io_arrows_scroll) LEFT/RIGHT scroll the columns shown by one
        // straight away and the panel stays armed.
        if (io_arrows_scroll(load)) {
          int8_t base = param1.cur - cur_col + (key == MDX_KEY_RIGHT ? 1 : -1);
          if (base >= 0 && base <= (int8_t)getWidth() - MAX_VISIBLE_COLS) {
            io_set_view(base);
          }
          return true;
        }
        io_end();
        return false;
      case MDX_KEY_UP:
      case MDX_KEY_DOWN:
        // Editing, or FADE held: UP/DOWN move the cursor (with FADE held,
        // between FADE's two stops).
        if (load && (io_editing || io_slide)) {
          io_menu_line.cur = limit_value(
              io_menu_line.cur, key == MDX_KEY_DOWN ? 1 : -1,
              io_slide ? io_fade_stop() : 0, io_fade_stop() + 1);
          io_slide_used |= io_slide;
          return true;
        }
        // Where the arrows move the grid, UP/DOWN move the armed row and the
        // panel stays armed; otherwise they disarm, as below.
        if (io_arrows_scroll(load)) {
          return false;
        }
        io_end();
        return false;
      default: {
        // Any other key disarms and does its usual job. The mixer
        // (Classic/Extended while playing) and step edit (REC) come back to
        // the panel: init() arms it again on the return.
        const uint8_t mode = io_mode;
        io_end();
        if (key == MDX_KEY_REC ||
            (key == MDX_KEY_EXTENDED && MidiClock.state == 2)) {
          io_resume = mode;
        }
        return false;
      }
      }
    }
    if (event->mask == EVENT_BUTTON_RELEASED && key == MDX_KEY_BANKD) {
      // A tap: let go before the hold time with nothing used. A longer
      // hold only looked at the FADE page.
      if (io_slide && !io_fade_page()) {
        mcl_cfg.load_fade ^= 1;
      }
      io_slide = false;
      return true;
    }
    if (event->mask == EVENT_BUTTON_RELEASED && key == MDX_KEY_YES) {
      if (GridIOPage::show_track_type) {
        io_action(true);
      }
      return true;
    }
    return false;
  }

  if (EVENT_BUTTON(event)) {
    if (EVENT_PRESSED(event, Buttons.BUTTON2)) {
      io_toggle_grid();
      return true;
    }
    // Load|Yes on Load: a tap closes it, as it always did; held, the rows
    // can be edited (io_loop). With Save|No held it is the config chord,
    // which the Grid page takes once Load is disarmed.
    if (load && event->source == Buttons.BUTTON4) {
      if (EVENT_PRESSED(event, Buttons.BUTTON4)) {
        if (BUTTON_DOWN(Buttons.BUTTON1)) {
          io_end();
          return false;
        }
        io_tap_clock = read_clock_ms();
      } else {
        // Let go before the hold time: a tap, unless it is the press that
        // opened Load.
        if (!io_held && !io_opening) {
          io_end();
        }
        io_opening = false;
      }
      return true;
    }
#ifndef PLATFORM_TBD
    // Save|No pressed with Load|Yes held (Load opens on that press) is the
    // config chord, which the Grid page takes once the panel is disarmed.
    if (EVENT_PRESSED(event, Buttons.BUTTON1) &&
        BUTTON_DOWN(Buttons.BUTTON4)) {
      io_end();
      return false;
    }
#endif
    // Shift|Menu: hold for the groups, release to load or save the row. The
    // Machinedrum's [Global] arrives as Shift|Menu with its own key down: on
    // Load it is the edit key (io_loop), not the row load.
    if (EVENT_PRESSED(event, Buttons.BUTTON3)) {
      if (!(load && key_interface.is_key_down(MDX_KEY_PATSONG))) {
        io_group_select();
      }
      return true;
    }
    if (EVENT_RELEASED(event, Buttons.BUTTON3)) {
      if (GridIOPage::show_track_type) {
        io_action(true);
      }
      return true;
    }
    // Save|No closes; on Save, Load|Yes does too.
    if (EVENT_RELEASED(event, Buttons.BUTTON1) ||
        EVENT_RELEASED(event, Buttons.BUTTON4)) {
      io_end();
      return true;
    }
  }
  return false;
}
#endif // MCL_GRID_IO_PANEL

#if defined(MCL_HAS_DESKTOP_MOUSE)
bool GridPage::handleMouseEvent(mcl_mouse_event_t *event) {
  if (event == NULL) {
    return false;
  }

  const bool left_button = (event->buttons & MCL_MOUSE_BUTTON_LEFT) != 0;
  const bool right_button = (event->buttons & MCL_MOUSE_BUTTON_RIGHT) != 0;

  if (show_slot_menu) {
    if (right_button && (event->type == MCL_MOUSE_DOWN ||
                         event->type == MCL_MOUSE_DOUBLE_CLICK)) {
      close_slot_menu_from_mouse(*this);
      return true;
    }

    if (event->x < 40 &&
        grid_slot_page.handleMouseEventAt(event, 1, 8, 39)) {
      return true;
    }

    if (event->type == MCL_MOUSE_WHEEL && event->deltaY != 0) {
      int step = event->deltaY > 0 ? -1 : 1;
      if ((event->modifiers & MCL_MOUSE_MODIFIER_SHIFT) != 0) {
        param3.cur += step;
      } else {
        param4.cur += step;
      }
      return true;
    }

    if (event->type != MCL_MOUSE_DOWN &&
        event->type != MCL_MOUSE_DOUBLE_CLICK &&
        event->type != MCL_MOUSE_DRAG) {
      return false;
    }
    if (!left_button) {
      return false;
    }

    uint8_t visible_col = 0;
    uint8_t visible_row = 0;
    GridColumn track_idx = 0;
    GridRow row_idx = 0;
    if (!grid_mouse_hit_visible_cell(*this, event, &visible_col, &visible_row,
                                     &track_idx, &row_idx)) {
      return false;
    }

    set_slot_menu_range_to_cell(*this, visible_col, visible_row);
    return true;
  }

  if (event->type == MCL_MOUSE_WHEEL && event->deltaY != 0) {
    param2.cur += event->deltaY > 0 ? -1 : 1;
    reset_undo();
    return true;
  }

  if (event->type != MCL_MOUSE_DOWN &&
      event->type != MCL_MOUSE_DOUBLE_CLICK) {
    return false;
  }
  if (!left_button && !right_button) {
    return false;
  }

  uint8_t visible_col = 0;
  uint8_t visible_row = 0;
  GridColumn track_idx = 0;
  GridRow row_idx = 0;
  if (!grid_mouse_hit_visible_cell(*this, event, &visible_col, &visible_row,
                                   &track_idx, &row_idx)) {
    return false;
  }

  param1.cur = track_idx;
  param2.cur = row_idx;
  reset_undo();

  if (right_button) {
    GUI.queueVirtualButton(Buttons.BUTTON3, true);
    return true;
  }

  if (event->type == MCL_MOUSE_DOUBLE_CLICK && !show_slot_menu) {
#if MCL_GRID_IO_PANEL
    io_begin(GRID_IO_LOAD);
#else
    mcl.setPage(GRID_LOAD_PAGE);
#endif
  }
  return true;
}
#endif
