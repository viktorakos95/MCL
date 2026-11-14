/* Justin Mammarella jmamma@gmail.com 2018 */

#pragma once

#include "Grid/GridRowHeader.h"
#include "Grid/GridLink.h"
#include "GUI.h"
#include "MCL.h"
#include "MCLStrings.h"

#define MAX_VISIBLE_ROWS 4
#define MAX_VISIBLE_COLS 8

// AVR and TBD: Load and Save arm on the Grid page itself. The left info
// column becomes the Load/Save panel and the grid stays visible. The hosted
// builds keep the GridLoadPage / GridSavePage pages.
#if defined(__AVR__) || defined(PLATFORM_TBD)
#define MCL_GRID_IO_PANEL 1
#else
#define MCL_GRID_IO_PANEL 0
#endif

#define GRID_IO_OFF 0
#define GRID_IO_LOAD 1
#define GRID_IO_SAVE 2

#define SLOT_DISABLED 255
#define SLOT_PENDING 254
#define SLOT_OFFSET_LOAD 253
#define SLOT_RAM_RECORD 252
#define SLOT_RAM_PLAY 251

class GridTrack;

class GridPage : public LightPage {
public:
  GridRowHeader row_headers[MAX_VISIBLE_ROWS];
  char slot_labels[MAX_VISIBLE_ROWS][GRID_WIDTH][3];

  GridColumn col;
  GridRow row;
  GridColumn cur_col;
  GridRow cur_row;
  GridColumn old_col = 255;
  uint8_t bank;

  bool reload_slot_models;
  bool show_slot_menu;
  bool write_cfg;

  GridRow active_slots[NUM_SLOTS];

  GridIndex cur_grid;
  uint8_t slot_clear;

  uint8_t slot_apply;
  uint8_t slot_copy;
  uint8_t slot_paste;
  uint8_t slot_undo;
  GridColumn slot_undo_x;
  GridRow slot_undo_y;
  uint8_t slot_load;
  uint8_t slot_menu_grid;
  GridLink slot_original_link;
  uint8_t slot_load_sound = 1;

  uint16_t grid_lastclock;

  GridRow row_scan;
  uint64_t row_states[2];

  PageIndex last_page = NULL_PAGE;

  // bank_popup states:
  //   0 = closed
  //   1 = bank held (preview, MD-bank-key entry)
  //   2 = pattern stage (trig press loads/chains in current bank)
  uint8_t bank_popup;
  uint16_t bank_popup_lastclock;
  uint16_t bank_popup_loadmask;
#ifdef PLATFORM_TBD
  // First trig pressed inside the popup, recorded so the LED repaint can
  // distinguish "head" (red) from "chained" (yellow). 255 = none yet.
  uint8_t bank_popup_first_trig = 255;
  // OLED bank-grid is hidden once trig presses start so the user can see
  // the underlying page; re-shown on the next arrow press.
  bool bank_popup_oled_visible = true;
#endif

  bool draw_encoders;
  uint16_t draw_encoders_lastclock;

#if MCL_GRID_IO_PANEL
  uint8_t io_mode;          // GRID_IO_OFF / GRID_IO_LOAD / GRID_IO_SAVE
  GridIndex io_saved_grid;  // grid shown when Load/Save was armed
  // Pending-transition animation: slots whose pending load fired within the
  // last 420 ms (set by GridTask when the load happens), one bit per slot.
  union {
    uint32_t settle_mask;
    uint8_t settle_bits[4];
  };
  uint16_t settle_clock;

  bool io_armed() const { return io_mode != GRID_IO_OFF; }
  void io_begin(uint8_t mode);
  void io_end(bool reinit = true);
  bool io_handleEvent(gui_event_t *event);
  void io_loop();
  void io_action(bool groups);
  void io_group_select();
  void io_toggle_grid();
  void io_set_view(GridColumn base);
  uint16_t io_visible_mask();
  void io_display_panel();
#else
  bool io_armed() const { return false; }
#endif
  GridPage(Encoder *e1 = NULL, Encoder *e2 = NULL, Encoder *e3 = NULL,
           Encoder *e4 = NULL)
      : LightPage(e1, e2, e3, e4) {}
  bool handleEvent(gui_event_t *event) override;
#if defined(MCL_HAS_DESKTOP_MOUSE)
  virtual bool handleMouseEvent(mcl_mouse_event_t *event) override;
#endif
  GridSpan getWidth();
  GridColumn getCol();
  GridRow getRow();

  void jump_to_row(GridRow row);
  void load_row(uint8_t n, GridRow row);

  void row_state_scan();
  void update_row_state(GridRow row, bool state);
  void set_active_row(GridRow row);
  bool is_slot_queue(uint8_t x, uint8_t y);
  void load_slot_models();
  void display_slot_menu();
  void display_counters();
  void display_grid_info();
  void display_position();
  void display_grid();
  void display_row_info();
  void display() override;
  void display_oled();
  void setup() override;
  void cleanup() override;
  void init() override;
  void apply_slot_changes(bool ignore_undo = false, bool ignore_func = false);

  void load_old_col();
  void close_bank_popup();

  void loop() override;
  void send_row_led();
};

static inline uint16_t grid_row_bank_mask(const uint64_t *row_states,
                                          uint8_t bank) {
  const uint8_t *mask = (const uint8_t *)row_states + ((uint8_t)(bank << 1));
  return (uint16_t)mask[0] | ((uint16_t)mask[1] << 8);
}

extern void apply_slot_changes_cb();
extern void rename_row();
