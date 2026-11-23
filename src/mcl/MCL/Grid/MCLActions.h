/* Copyright 2018, Justin Mammarella jmamma@gmail.com */

#pragma once

#include "Grid/GridChain.h"
#include "Grid/MCLActionsEvents.h"
#include "MCLPlatformFeatures.h"
#include "Grid/MidiDeviceGrid.h"
#include "MCLSysConfig.h"
#include "Grid/GridLink.h"
#include "TrackLoadFade.h"

class DeviceTrack;
class EmptyTrack;
class ElektronDevice;
class GridRowHeader;
class MidiUartClass;
class SeqTrack;

#define PATTERN_STORE 0
#define PATTERN_UDEF 254
#define STORE_IN_PLACE 0
#define STORE_AT_SPECIFIC 254

#define TRANSITION_NORMAL 0
#define TRANSITION_MUTE 2
#define TRANSITION_UNMUTE 3

#if MCL_FEATURE_HOST_ARRANGER
uint32_t selected_destination_mask(const uint8_t *slot_select_array,
                                   GridSlot load_offset);
#endif

//div192th_time = 1.25 / tempo;
//diff * div19th_time > 80ms equivalent to diff > (0.08/1.25) * tempo
//float ms = (0.08 * 0.80) * tempo == 0.064 * tempo; 80ms

#if defined(__AVR__)
  #define GUI_THRESHOLD_FACTOR 0.064f  //80ms
  #define TRACK_MIN_LOAD_TIME 4 //4ms
#else
  #define GUI_THRESHOLD_FACTOR 0.016f  //20ms
  #define TRACK_MIN_LOAD_TIME 1 //1ms
#endif

class DeviceLatency {
public:
  uint16_t latency_bytes;
  uint16_t div192th_latency;
};

class LinkModeData {
public:
  DeviceLatency dev_latency[NUM_DEVS];

  uint16_t div192th_total_latency;
  uint16_t div32th_total_latency;

  GridLink links[NUM_SLOTS];

  uint16_t md_latency;

  uint16_t next_transition = (uint16_t)-1;
  uint16_t nearest_bar;
  uint8_t nearest_beat;

  uint16_t next_transitions[NUM_SLOTS];
  uint8_t transition_offsets[NUM_SLOTS];
  uint8_t send_machine[NUM_SLOTS];
  // FADE's direction for each column's pending load, decided when it is
  // asked for and used once, by that load's own fade.
  uint8_t load_fade_dir[NUM_SLOTS];
  uint8_t transition_level[NUM_SLOTS];

  GridSlot dev_sync_slot[NUM_DEVS];

  GridChain chains[NUM_SLOTS];

  // A Manual or Auto load for a column whose chain hops before the load's
  // own point waits here (its row; 255 none), so the chain keeps going: at
  // the chain's last hop before that point, take_deferred_load() gives the
  // column to it, in the mode of the latest load that waited
  // (deferred_mode).
  GridRow deferred_rows[NUM_SLOTS];
  uint8_t deferred_mode;
  void clear_deferred_loads() {
    memset(deferred_rows, 255, sizeof(deferred_rows));
  }
};

#define QUANT_LEN 255

// FADE's direction for a column's load (MCLActions::load_fade_dir).
#define LOAD_FADE_DIR_NONE 0
#define LOAD_FADE_DIR_IN LOAD_FADE_FORCE_IN
#define LOAD_FADE_DIR_OUT LOAD_FADE_FORCE_OUT
#define LOAD_FADE_DIR_OUTIN 3

class MCLActions : public LinkModeData {
public:
  uint8_t do_kit_reload;
  int writepattern;
  uint8_t write_original;

  uint8_t patternswitch = PATTERN_UDEF;

  uint16_t start_clock16th;
  uint32_t start_clock32th;
  uint32_t start_clock96th;
  uint8_t store_behaviour;

  MCLActions() {}

  void setup();

  uint8_t get_quant() {
    uint8_t q;
    if (mcl_cfg.chain_load_quant == 1) {
      q = QUANT_LEN; // use slot settings
    } else {
      q = mcl_cfg.chain_load_quant; // override
    }
    return q;
  }

  // This is the track length quantisatioe
  uint8_t get_chain_length() {
    uint8_t q;
    if (mcl_cfg.chain_queue_length == 1) {
      q = QUANT_LEN; // use slot settings
    } else {
      q = mcl_cfg.chain_queue_length; // override
    }
    return q;
  }

  GridDeviceTrack *get_grid_dev_track(GridSlot slot_number);

  void init_chains();
  // At a hop of column n's chain (cache_next_tracks): once the chain's next
  // hop is not before the waiting load's point, the load takes the column,
  // its next transition and the cache this hop fills. Returns whether it did.
  bool take_deferred_load(GridSlot n);
  // Column dst loads row at step at, once (a QUANT load's pending state).
  void set_pending_load(GridSlot dst, GridRow row, uint16_t at) NOINLINE();
  // FADE's direction for a load of row into column dst (Grid page panel).
  uint8_t pick_fade_dir(GridSlot dst, GridRow row, uint8_t load_mode);
  // OUTIN for a column loading at its next transition (Grid page panel);
  // returns whether it held the switch back by the fade's length.
  bool hold_outin(GridSlot dst);
  // Whether column n's chain hops before point (a load point, in steps).
  bool chain_hops_before(GridSlot n, uint16_t point) NOINLINE();

  void send_globals();
  void switch_global(uint8_t global_page);
  void kit_reload(uint8_t pattern);
  void row_update(GridSlot last_slot);
  bool save_tracks(GridRow row, uint8_t *slot_select_array, uint8_t merge,
                   uint8_t readpattern = 255);

  void load_tracks(uint8_t *slot_select_array,
                   GridRow *_row_array = nullptr, uint8_t load_mode = 255,
                   GridSlot load_offset = 255
#if MCL_FEATURE_HOST_LOAD_FADE_SEEK
                   ,
                   bool immediate = false,
                   bool allow_prestart_fades = false
#endif
                   );
#if !defined(__AVR__)
  void clear_tracks(uint8_t *slot_select_array);
#endif
  void send_tracks_to_devices(uint8_t *slot_select_array,
                              GridRow *row_array = nullptr,
                              GridSlot load_offset = 255
#if MCL_FEATURE_HOST_LOAD_FADE_SEEK
                              ,
                              bool allow_prestart_fades = false
#endif
#if MCL_FEATURE_HOST_ARRANGER_RECORD_HOOKS
                              ,
                              bool record_arranger_clips = false
#endif
                              );
  void manual_transition(uint8_t *slot_select_array, GridRow *row_array,
                         GridSlot load_offset);
  void update_chain_links(GridSlot n, GridDeviceTrack *gdt);
  void cache_track(GridSlot n, GridDeviceTrack* gdt, GridColumn track_idx);
  void cache_next_tracks(uint8_t *slot_select_array, bool gui_update = false);
  static uint32_t ui_pass_margin();
  void calc_next_slot_transition(GridSlot n, bool ignore_chain_settings = false,
                                 bool ignore_overflow = false);
  void calc_next_transition();
  void calc_latency();
  void clear_load_fades(bool preserve_armed_prestart = false);
  // panel: the LOAD panel's FADE replaces the slot's own (see panel_fade).
  // False when the slot's track cannot fade (it is not a Machinedrum track).
  bool start_load_fade_at(GridSlot slot, const TrackLoadFadeData *fade,
                          uint32_t start_clock
#if MCL_FEATURE_HOST_LOAD_FADE_SEEK
                          ,
                          bool allow_prestart = false
#endif
                          ,
                          bool panel = true);
  // The fade FADE chose for this slot's pending load (load_fade_dir), used
  // up here: false when there is none. OUTIN gives its IN. Defined where the
  // panel exists (MCL_GRID_IO_PANEL).
  bool panel_fade(GridSlot slot, GridDeviceTrack *gdt,
                  TrackLoadFadeData *fade);

private:
  static uint16_t clear_runtime_fade_elapsed(DeviceTrack *track);
  static void restore_runtime_fade_elapsed(DeviceTrack *track,
                                           uint16_t elapsed);
  static const char *shared_row_name(ElektronDevice **devs,
                                     uint8_t save_dev_mask);
  static void copy_row_name_text(GridRowHeader &row_header,
                                 const char *name);
  static void copy_row_name(GridRowHeader &row_header, const char *name);
  static void inherit_grid_x_row_name(GridRowHeader *row_headers);
  static bool is_grid_chain_load_mode(uint8_t mode);
  static uint8_t device_mask_bit(uint8_t device_idx);
  static bool row_is_loadable(GridRow row);
  static bool row_uses_private_runtime_source(GridRow row);
  static void apply_host_runtime_clip(GridSlot dst, DeviceTrack *track);
  static void clear_host_runtime_private_source(GridSlot dst);
  static void clear_host_runtime_private_source_for_row(GridSlot dst,
                                                        GridRow row);
  static void apply_host_runtime_clip_for_row(GridSlot dst, GridRow row,
                                              DeviceTrack *track);

  DeviceTrack *load_and_prepare_track(GridSlot track_idx, GridRow row,
                                      uint8_t track_type, SeqTrack *seq_track,
                                      uint8_t seq_track_idx, bool &was_rebuilt,
                                      EmptyTrack &scratch,
                                      int8_t link_slot = -1,
                                      GridSlot runtime_dst = 255);
  void collect_tracks(uint8_t *slot_select_array, GridRow *row_array,
                      GridSlot load_offset);
  bool load_track_immediate(GridRow row, GridSlot i, GridSlot dst,
                            GridDeviceTrack *gdt_dst, uint8_t *send_masks
#if MCL_FEATURE_HOST_LOAD_FADE_SEEK
                            ,
                            TrackLoadFadeData *load_fade_out,
                            uint8_t *load_fade_valid_out,
                            bool allow_prestart_fade = false
#endif
                            );
  void swap_mute_states(uint8_t *mute_states);
};

extern void md_import();

extern MCLActionsCallbacks mcl_actions_callbacks;
extern MCLActionsMidiEvents mcl_actions_midievents;

extern MCLActions mcl_actions;
