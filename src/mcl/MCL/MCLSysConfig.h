/* Copyright 2018, Justin Mammarella jmamma@gmail.com */

#ifndef MCLSYSCONFIG_H__
#define MCLSYSCONFIG_H__

#include "MCLSd.h"
#include "MCLMemory.h"
#include "Sequencer/PtcGroups.h"

#define CONFIG_VERSION_SAMPLE_BANK_LINK 4021
#define CONFIG_VERSION CONFIG_VERSION_SAMPLE_BANK_LINK

#define MD_SAMPLE_BANK_OFF 0
#define MD_SAMPLE_BANK_FIXED_FIRST 1
#define MD_SAMPLE_BANK_FIXED_LAST 128

#define MIDI_OMNI_MODE 17
#define MIDI_LOCAL_MODE 0
#define MD_POLY_MODE_INT 0
#define MD_POLY_MODE_INT_EXT 1

extern void mclsys_apply_config();
extern void mclsys_apply_config_midi();
extern void mclsys_normalize_midi_config();

extern void usb_os_update();
extern void usb_dfu_mode();
extern void usb_disk_mode();
extern void mcl_setup();

// Persisted verbatim in config.mcls and ProjectHeader.
class ATTR_PACKED() MCLSysConfigData {
public:
  uint32_t version;
  char project[PRJ_PATH_LEN];
  uint8_t number_projects;
  uint8_t uart1_turbo_speed;
  uint8_t uart2_turbo_speed;
  uint8_t usb_turbo_speed;
  uint8_t clock_send;
  uint8_t clock_rec;
  uint8_t drumRouting[16];
  uint8_t routing[24];
  GridRow row;
  GridColumn col;
  GridRow cur_row;
  GridColumn cur_col;
  uint16_t poly_mask;
  uint8_t uart2_ctrl_chan;
  // Legacy poly-channel byte: 0 = INT; nonzero legacy channel/1 = INT+EXT.
  uint8_t uart2_poly_chan;
  uint8_t uart2_prg_in;
  uint8_t uart2_prg_out;
  uint8_t uart2_prg_mode;
  uint32_t mutes;
  uint8_t display_mirror;
  uint8_t rec_quant;
  float tempo;

  uint8_t midi_forward_1;
  uint8_t midi_forward_2;
  uint8_t midi_forward_usb;

  uint8_t rec_automation;
  uint8_t load_mode;
  uint8_t chain_queue_length;
  uint8_t chain_load_quant;

  uint8_t auto_normalize;
  uint8_t ram_page_mode;
  uint8_t track_select;
  uint16_t track_type_select;
  uint8_t uart2_device;
  uint8_t uart_cc_fwd;
  uint8_t usb_mode;
  uint8_t midi_transport_rec;
  uint8_t midi_transport_send;
  uint8_t midi_ctrl_port;
  uint8_t md_trig_channel;
  uint8_t seq_dev;
  uint8_t uart2_cc_mute;
  uint8_t uart2_cc_level;
  uint8_t uart1_device;
  uint8_t grid_page_mode;
  uint8_t uart_note_fwd;
  uint8_t usb_device;
  uint8_t grid_x_device;
  uint8_t grid_x_port;
  uint8_t grid_y_device;
  uint8_t grid_y_port;
  uint8_t ptc_group[PTC_GROUP_TRACKS];
  uint8_t project_config;
  uint8_t md_sample_bank;
  uint8_t md_sample_bank_capture;
  uint8_t active_arrangement_idx;
};

static_assert(sizeof(MCLSysConfigData) == 185,
              "persisted system config layout changed");

// The Grid page LOAD panel's FADE length: 0 follows QUANT, n is 2^(n-1)
// steps (n = 1 .. LOAD_FADE_MAX).
#define LOAD_FADE_MAX 11
// A trig in the Load edit sets a length of 16 x n steps (trig n, a bar a
// trig, up to 256), stored as LOAD_FADE_TRIG_FIRST + n - 1, after the knob's
// lengths.
#define LOAD_FADE_TRIG_FIRST 12
#define LOAD_FADE_CODES_MAX (LOAD_FADE_TRIG_FIRST + 15)

// The steps of a set fade length (load_fade_len 1 .. LOAD_FADE_CODES_MAX).
static inline uint16_t load_fade_steps(uint8_t len) {
  return len >= LOAD_FADE_TRIG_FIRST ? 16 * (len - LOAD_FADE_TRIG_FIRST + 1)
                                     : (uint16_t)1 << (len - 1);
}
#define LOAD_FADE_FORCE_IN 1
#define LOAD_FADE_FORCE_OUT 2

class MCLSysConfig : public MCLSysConfigData {
public:
  // The LOAD panel's FADE: bit 0 on. Bits 1-2: 0 picks each column's
  // direction by what it was playing (MCLActions::load_fade_dir),
  // LOAD_FADE_FORCE_IN / _OUT force it (SCALE+UP / SCALE+DOWN).
  int8_t load_fade;
  uint8_t load_fade_len;
  // Both follow the persisted block in config.mcls, then their complements,
  // so a file written before FADE existed reads as off. Older MCL reads and
  // writes only the persisted block and leaves these bytes alone.
  uint8_t load_fade_check[2];
  // SYSTEM > LOAD/SAVE: 1 (STAY) keeps the Grid page's Load and Save armed
  // after a load or save, 0 (CLOSE) closes them. It follows the fade bytes,
  // then its complement, so a file written before it existed reads CLOSE.
  uint8_t io_stay;
  uint8_t io_stay_check;
  uint16_t cfg_save_lastclock;
  File cfgfile;
  bool write_cfg();
  bool cfg_init();
};

extern MCLSysConfig mcl_cfg;

#endif /* MCLSYSCONFIG_H__ */
