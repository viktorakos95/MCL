/* Justin Mammarella jmamma@gmail.com 2018 */

#ifndef MDSEQTRACK_H__
#define MDSEQTRACK_H__

#include "DeviceTrack.h"
#include "MCL.h"
#include "MD.h"
#include "MDSeqTrackData.h"
#include "Sequencer/Euclidean.h"
#include "Sequencer/SeqTrack.h"
#include "MidiClock.h"

#define SEQ_NOTEBUF_SIZE 8
#define SEQ_MUTE_ON 1
#define SEQ_MUTE_OFF 0

class MDTrack;

const uint8_t number_midi_cc = 6 * 2 + 4;
const uint8_t midi_cc_array_size = 6 * 2 + 4;

class TrigNotes {
public:
  uint8_t note1;
  uint8_t note2;
  uint8_t note3;
  uint8_t len;
  uint8_t vel;
  uint8_t ccs[midi_cc_array_size];
  uint16_t count_down;
  bool first_trig = false;
};

class MDSeqTrack : public MDSeqTrackData, public SeqSlideTrack {

public:
  uint64_t oneshot_mask;

  // What get_step()/get_mask() (MASK_PATTERN) and seq() actually play/show
  // for a step: this track's own Euclidean-generated pattern (see
  // Euclidean.h -- every track has its own permanent slot) when armed
  // (`enabled`) AND a sequencer/euclidean-family page is actually showing
  // -- otherwise whatever was manually placed. Leaving to Mixer/Grid/etc
  // reverts the track to its normal pattern without discarding anything:
  // the slot (pl1/pl2/.../enabled) is untouched, this is purely a
  // read-time gate, so coming back to the step page (or reopening
  // EucPage) picks the generated pattern back up exactly as it was. The
  // manual pattern itself is never modified by any of this either, so
  // turning off a slot always shows exactly what was there before,
  // untouched.
  ALWAYS_INLINE() bool effective_trig(uint8_t step) const {
    PageIndex page = mcl.current_page;
    bool on_seq_page = page == SEQ_STEP_PAGE || page == SEQ_EXTSTEP_PAGE ||
                       page == SEQ_PTC_PAGE || page == EUC_PAGE;
    EucSlot *slot = euc_slot_find(track_number);
    if (on_seq_page && slot->enabled) {
      return euclidean_pattern_has_pulse(step, slot->pl1, slot->pl2,
                                         slot->ro1, slot->ro2, slot->op,
                                         slot->tro, length);
    }
    return steps[step].trig;
  }

  static uint16_t gui_update;
  static uint16_t md_trig_mask;
  static uint32_t load_machine_cache;
  static volatile uint16_t pending_swing_change_mask;
  static volatile uint8_t pending_swing_amount;

  TrigNotes notes;

  MDSeqTrack() : SeqSlideTrack() {
    active = MD_TRACK_TYPE;
    memset(notes.ccs, 255, sizeof(notes.ccs));
  }
  ALWAYS_INLINE() void reset() {
    SeqSlideTrack::reset();
    oneshot_mask = 0;
    record_mutes = false;
    send_notes_off();
  }

  void get_mask(uint64_t *_pmask, uint8_t mask_type) const;

  bool get_step(uint8_t step, uint8_t mask_type) const;
  void set_step(uint8_t step, uint8_t mask_type, bool val);
  void rotate_mask(uint8_t mask_type, uint8_t dir);

  void seq(MidiUartClass *uart_, MidiUartClass *uart2_);

  static void pre_seq(MidiUartClass *uart_);
  static void post_seq(MidiUartClass *uart_);

  void mute() { mute_state = SEQ_MUTE_ON; }
  void unmute() { mute_state = SEQ_MUTE_OFF; }

  void send_trig();
  void send_trig_inline();
  uint8_t trig_conditional(uint8_t condition);
  ALWAYS_INLINE() void clear_step_oneshot(uint8_t step) {
    CLEAR_BIT64(oneshot_mask, step);
  }
  void send_parameter_locks(uint8_t step, bool trig,
                            uint16_t lock_idx = 0xFFFF);
  void send_parameter_locks_inline(uint8_t step, bool trig, uint16_t lock_idx);
  void reset_params();
  void get_step_locks(uint8_t step, uint8_t *params,
                      bool ignore_locks_disabled = false);

  void recalc_slides();
  uint8_t effective_timing(uint8_t step, uint8_t ticks_per_step) const;
  void find_next_locks(uint8_t curidx, uint8_t step, uint8_t &mask);

  void set_track_pitch(uint8_t step, uint8_t pitch);
  void set_track_step(uint8_t step, uint8_t utiming,
                      uint8_t velocity = 127);
  // !! Note lockidx is lock index, not param id
  bool set_track_locks_i(uint8_t step, uint8_t lockidx, uint8_t velocity);
  // !! Note track_param is param_id, not lock index
  bool set_track_locks(uint8_t step, uint8_t track_param,
                       uint8_t velocity);
  // !! Note lockidx is lock index, not param_id

  uint8_t get_track_lock(uint8_t step, uint8_t lockidx);
  uint8_t get_track_lock_implicit(uint8_t step, uint8_t param);

  void record_track(uint8_t velocity);
  void record_track_locks(uint8_t track_param, uint8_t value);
  void record_track_pitch(uint8_t pitch);
  void clear_mute();
  void clear_mutes();
  void clear_oneshot();
  void clear_slide_data();
  void clear_step_locks(uint8_t step);
  // disable the step locks, but not remove them. so later can be re-activated.
  void disable_step_locks(uint8_t step);
  void enable_step_locks(uint8_t step);
  // access the step lock bitmap, masked by locks_enable bit.
  uint8_t get_step_locks(uint8_t step);
  void clear_conditional();
  void clear_step_lock(uint8_t step, uint8_t param_id);
  void clear_locks();
  virtual void clear_track(bool locks = true) override;
  void clear_param_locks(uint8_t param_id);
  bool is_param(uint8_t param_id);

  void merge_from_md(uint8_t track_number, MDPattern *pattern);

  virtual void set_length(uint8_t len, bool expand = false) override;
  void re_sync();
  ALWAYS_INLINE() bool request_speed_change(uint8_t new_speed) {
    if (count_down) {
      return false;
    }
    if (!MidiClock.isStarted()) {
      if (speed == new_speed && !has_pending_speed_change()) {
        return false;
      }
      clear_pending_speed_change();
      set_speed(new_speed, speed, true);
      return true;
    }
    return SeqTrack::request_speed_change(new_speed);
  }
  void request_swing_amount_change(uint8_t amount);

  virtual void rotate_left() override { modify_track(DIR_LEFT); }
  virtual void rotate_right() override { modify_track(DIR_RIGHT); }
  virtual void reverse() override { modify_track(DIR_REVERSE); }

  void modify_track(uint8_t dir);

  void set_speed(uint8_t new_speed, uint8_t old_speed = 255,
                 bool timing_adjust = true);

  void copy_step(uint8_t n, MDSeqStep *step);
  void paste_step(uint8_t n, MDSeqStep *step);
  void load_cache();

  void init_notes() NOINLINE();
  void process_note_locks(uint8_t param, uint8_t val, uint8_t *ccs);
  void send_notes_ccs(uint8_t *ccs, bool send_ccs);
  void send_notes(uint8_t first_note = 255, MidiUartClass *uart2_ = nullptr);
  void send_notes_on(MidiUartClass *uart2_ = nullptr);
  void send_notes_off(MidiUartClass *uart2_ = nullptr);
  void send_slides(volatile uint8_t *locks_params,
                   uint8_t channel = 0) override;

  uint8_t transpose_pitch(uint8_t pitch, int8_t offset);
  virtual void transpose(int8_t offset) override;
  void onControlChangeCallback_Midi(uint8_t track_param,uint8_t value);

protected:
  void dispatch_slide_value(uint8_t param, uint8_t value,
                            uint8_t channel) override;

private:
  struct SlideDispatchContext {
    bool is_midi_model;
    bool send_ccs;
    uint8_t *ccs;
  };

  SlideDispatchContext *slide_ctx = nullptr;
};

#endif /* MDSEQTRACK_H__ */
