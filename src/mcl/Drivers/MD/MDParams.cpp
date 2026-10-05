/* Copyright (c) 2009 - http://ruinwesen.com/ */

#include "helpers.h"
#include "MD.h"
#include "ResourceManager.h"

/**
 * \addtogroup MD Elektron MachineDrum
 *
 * @{
 *
 * \addtogroup md_params MachineDrum parameters
 *
 * @{
 **/

/** Names for the LFO parameters. **/
const char *MDLFONames[8] = {
  "TRK",
  "PRM",
  "SH1",
  "SH2",
  "TYP",
  "SPD",
  "DPT",
  "MIX"
};

/**
 * Names for the different machine models of the machinedrum.
 **/

#ifndef DISABLE_MACHINE_NAMES

#endif

/// Caller is responsible to make sure machine_names_short is loaded in RM
const char* getMDMachineNameShort(uint8_t machine, uint8_t type) {
  if (machine == 0) {
    if (type == 1) {
      return R.machine_names_short->md_machine_names_short[0].name2;
    }
  }
  return getMachineNameShort(
      machine, type, 
      R.machine_names_short->md_machine_names_short, 
      R.machine_names_short->countof_md_machine_names_short);
}

/// Caller is responsible to make sure machine_names_long is loaded in RM
const char* MDClass::getMachineName(uint8_t machine) {
  for (uint8_t i = 0; i < R.machine_names_long->countof_machine_names; i++) {
    if (R.machine_names_long->machine_names[i].id == machine) {
      return R.machine_names_long->machine_names[i].name;
    }
  }
  return NULL;
}

model_to_param_names_t model_param_names[] = {
  { GND_SN_MODEL, 0 },
  { GND_NS_MODEL, 9 },
  { GND_IM_MODEL, 11},
  { GND_SW_MODEL, 16},
  { GND_PU_MODEL, 25},

  { NFX_EV_MODEL, 582},
  { NFX_CO_MODEL, 591},
  { NFX_UC_MODEL, 600},

  { TRX_BD_MODEL, 34},
  { TRX_B2_MODEL, 43},
  { TRX_SD_MODEL, 61},
  { TRX_XT_MODEL, 70},
  { TRX_CP_MODEL, 78},
  { TRX_RS_MODEL, 87},
  { TRX_CB_MODEL, 91},
  { TRX_CH_MODEL, 98},
  { TRX_OH_MODEL, 104},
  { TRX_CY_MODEL, 110},
  { TRX_MA_MODEL, 117},
  { TRX_CL_MODEL, 126},
  { TRX_XC_MODEL, 133},
  { TRX_S2_MODEL, 52},

  { EFM_BD_MODEL, 141},
  { EFM_SD_MODEL, 150},
  { EFM_XT_MODEL, 159},
  { EFM_CP_MODEL, 168},
  { EFM_RS_MODEL, 177},
  { EFM_CB_MODEL, 186},
  { EFM_HH_MODEL, 194},
  { EFM_CY_MODEL, 203},

  { E12_BD_MODEL, 211},
  { E12_SD_MODEL, 220},
  { E12_HT_MODEL, 229},
  { E12_LT_MODEL, 238},
  { E12_CP_MODEL, 247},
  { E12_RS_MODEL, 256},
  { E12_CB_MODEL, 265},
  { E12_CH_MODEL, 274},
  { E12_OH_MODEL, 283},
  { E12_RC_MODEL, 292},
  { E12_CC_MODEL, 301},
  { E12_BR_MODEL, 310},
  { E12_TA_MODEL, 319},
  { E12_TR_MODEL, 328},
  { E12_SH_MODEL, 337},
  { E12_BC_MODEL, 346},

  { P_I_BD_MODEL, 355},
  { P_I_SD_MODEL, 362},
  { P_I_MT_MODEL, 370},
  { P_I_ML_MODEL, 379},
  { P_I_MA_MODEL, 384},
  { P_I_RS_MODEL, 390},
  { P_I_RC_MODEL, 397},
  { P_I_CC_MODEL, 406},
  { P_I_HH_MODEL, 415},

  { INP_GA_MODEL, 424},
  { INP_FA_MODEL, 430},
  { INP_EA_MODEL, 439},
  { INP_CA_MODEL, 609},

  { INP_GB_MODEL, 424},
  { INP_FB_MODEL, 430},
  { INP_EB_MODEL, 439},
  { INP_CB_MODEL, 609},

  { CTR_AL_MODEL, 473},
  { CTR_8P_MODEL, 482},

  { CTR_RE_MODEL, 507},
  { CTR_GB_MODEL, 515},
  { CTR_EQ_MODEL, 523},
  { CTR_DX_MODEL, 531},

#if !defined(__AVR__)  // labels for these are not in the AVR resource
  // OS X.14 model patcher machines
  { MM_DEN_MODEL, 618},
  { MM_FMDY_MODEL, 627},
  { MM_FMST_MODEL, 636},
  { MM_FMPA_MODEL, 645},
  { MM_SAW_MODEL, 654},
  { MM_ENS_MODEL, 663},
  { MM_BOX_MODEL, 672},
  { MM_DDR_MODEL, 679},
  { AN_BD_MODEL, 688},
  { AN_CY_MODEL, 697},
  { AN_HH_MODEL, 705},
  { AN_PC_MODEL, 714},
  { AN_RC_MODEL, 723},
  { AN_SD_MODEL, 732},
  { AN_SY_MODEL, 741},
  { CM_ACID_MODEL, 750},
  { CM_SAWPW_MODEL, 759},
  { CM_SPECT_MODEL, 766},
  { CM_FM4OP_MODEL, 774},
  { CM_FORMT_MODEL, 783},
  { NP_NOISE_MODEL, 791},
  { MM_WAV_MODEL, 800},
  { MM_SID_MODEL, 809},
  { MM_VO6_MODEL, 818},
  { MM_PLS_MODEL, 827},
#endif
};

#if defined(__AVR__)
static constexpr uint8_t MD_MODEL_PARAM_NAME_LIMIT = MD_PARAMS_PER_TRACK;
#else
static constexpr uint8_t MD_MODEL_PARAM_NAME_LIMIT = SPS_PARAMS_PER_TRACK;
#endif

static const char* get_param_name(const model_param_name_t *names, uint8_t param) {
  uint8_t i = 0;
  uint8_t id;
  if (names == NULL)
    return NULL;

  while ((id = names[i].id) != 127 && i < MD_MODEL_PARAM_NAME_LIMIT) {
    if (id == param) {
      return names[i].name ;
    }
    i++;
  }
  return NULL;
}

static uint16_t get_model_param_names(uint8_t model) NOINLINE();
static uint16_t get_model_param_names(uint8_t model) {
  for (uint16_t i = 0; i < countof(model_param_names); i++) {
    if (model == model_param_names[i].model) {
      return model_param_names[i].offset;
    }
  }
  return 0xFFFF;
}

#if !defined(__AVR__)
// Extended param names (params 24-36) — hardcoded to avoid resource changes
static const char* ext_param_name(uint8_t param) {
  switch (param) {
    case MODEL_ENVATT:  return "ATK";
    case MODEL_ENVHLD:  return "HLD";
    case MODEL_ENVDCY:  return "DCY";
    case MODEL_ENVMIX:  return "MIX";
    case MODEL_LFO2SPD: return "SP2";
    case MODEL_LFO2DEP: return "DP2";
    case MODEL_LFO2MIX: return "MX2";
    case MODEL_RTRG:    return "RTG";
    case MODEL_RTIM:    return "RTM";
    case MODEL_RENV:    return "REN";
    case MODEL_BUS1:    return "BUS1";
    case MODEL_BUS2:    return "BUS2";
    case MODEL_BUS3:    return "BUS3";
    default:            return NULL;
  }
}
#endif

/// Caller is responsible to make machine_param_names is loaded in RM
const char* model_param_name(uint8_t model, uint8_t param) {
  if (param == MODEL_MUTE) {
    return "MUT";
  } else if (param == MODEL_LEVEL) {
    return "LEV";
#if !defined(__AVR__)
  } else if (param >= MD_PARAMS_PER_TRACK && param < SPS_PARAMS_PER_TRACK) {
    return ext_param_name(param);
#endif
  }

  uint16_t model_idx;

  if (model >= MID_MODEL && model <= MID_16_MODEL) {
    model_idx = 448; // midi
  } else if (model >= CTR_8P_MODEL && model < ROM_01_MODEL) {
    model_idx = get_model_param_names(model);
  } else if (param >= 8 || model == 0xFF) {
    model_idx = 557; //generic
  } else if ((model >= ROM_01_MODEL && model <= ROM_32_MODEL) ||
      (model >= ROM_33_MODEL && model <= ROM_48_MODEL))  {
    model_idx = 539; // rom
  } else if (model == RAM_R1_MODEL ||
      model == RAM_R2_MODEL ||
      model == RAM_R3_MODEL ||
      model == RAM_R4_MODEL) {
    model_idx = 548; // ram_r
  } else if (model == RAM_P1_MODEL ||
	model == RAM_P2_MODEL ||
	model == RAM_P3_MODEL ||
	model == RAM_P4_MODEL) {
    model_idx = 539; // rom
  } else {
    model_idx = get_model_param_names(model);
  }

  if (model_idx >= R.machine_param_names->countof_md_model_param_names) {
    return nullptr;
  }
  else {
    return get_param_name(
        R.machine_param_names->md_model_param_names + model_idx, 
        param);
  }
}

uint8_t map_fx_to_model(uint8_t fx_type) {
 if ((fx_type > MD_FX_DYN) || (fx_type < MD_FX_ECHO)) { return 255; }
 return fx_type - MD_FX_ECHO + CTR_RE_MODEL;
}


const char* fx_param_name(uint8_t fx_type, uint8_t param) {
   return model_param_name(map_fx_to_model(fx_type), param);
}

static const uint8_t efm_rs_tuning[] PROGMEM = { 1, 8, 0, 3 };  // formula, 48 notes
static const uint8_t efm_hh_tuning[] PROGMEM = { 1, 64, 6, 15 };  // formula, 30 notes
static const uint8_t efm_cp_tuning[] PROGMEM = { 0, 33, 14, 17 };  // formula, 66 notes

static const uint8_t efm_xt_tuning[] PROGMEM = { 1, 101, 18, 19 };  // formula, 24 notes

static const uint8_t trx_cl_tuning[] PROGMEM = { 5, 55, 4, 9 };  // formula, 21 notes
static const uint8_t trx_sd_tuning[] PROGMEM = { 3, 85, 2, 8 };  // formula, 12 notes
static const uint8_t trx_xc_tuning[] PROGMEM = { 1, 75, 1, 14 };  // formula, 24 notes
static const uint8_t trx_xt_tuning[] PROGMEM = { 2, 69, 2, 13 };  // formula, 24 notes
static const uint8_t trx_bd_tuning[] PROGMEM = { 1, 75, 10, 14 };  // formula, 24 notes

static const uint8_t trx_s2_tuning[] PROGMEM = {
  3, 7, 11, 15, 20, 24, 30, 35, 41, 47, 54, 60, 68, 76, 84, 92, 101, 111, 121
};

static const uint8_t rom_tuning[] PROGMEM = {
  0, 2, 5, 7, 9, 12, 14, 16, 19, 21, 23, 26, 28, 31, 34, 37, 40, 43, 46, 49, 52, 
  55, 58, 61, 64, 67, 70, 73, 76, 79, 82, 85, 88, 91, 94, 97, 100, 102, 105, 107, 
  109, (112), 114, 116, 119, 121, 123, 125, 
};

static const uint8_t gnd_sn_tuning[] PROGMEM = { 0, 4, 2, 3 };  // formula, 96 notes

static const uint8_t trx_b2_tuning[] PROGMEM = {
  31, 33, 36, 39, 43, 45, 48, 51, 56, 60, 62, 66, 70, 74, 79, 83, 89, 94, 97, 104, 108, 113, 120, 125
};

static const uint8_t trx_rs_tuning[] PROGMEM = {
  2, 10, 17, 26, 36, 45, 55, 66, 78, 90, 103, 117
};

static const uint8_t efm_cb_tuning[] PROGMEM = {
  2, 6, 10, 14, 19, 23, 27, 32, 36, 40, 44, 48, 53, 57, 62, 66, 70, 74, 78, 83,
  87, 91, 96, 100, 104, 108, 113, 117, 121, 126
};

static const uint8_t efm_cy_tuning[] PROGMEM = {
  1, 6, 10, 14, 18, 22, 27, 31, 36, 40, 44, 49, 52, 57, 61, 65, 70, 74, 78, 82,
  87, 91, 95, 100, 103, 108, 112, 116, 121, 125
};

static const uint8_t e12_bc_tuning[] PROGMEM = {
  2, 4, 7, 9, 12, 15, 17, 20, 22, 25, 28, 31, 34, 36, 39, 42, 44, 47, 49, 52, 55, 57, 60, 62, 65,
  68, 71, 74, 76, 79, 82, 84, 87, 89, 92, 95, 97, 100, 103, 106, 108, 111, 114, 116, 119, 122,
  124, 127
};

static const uint8_t e12_cb_tuning[] PROGMEM = {
  1, 4, 6, 9, 12, 14, 17, 19, 22, 25, 27, 30, 33, 36, 39, 41, 44, 46, 49, 52, 54, 57, 59,
  62, 65, 68, 71, 73, 76, 79, 81, 84, 86, 89, 92, 94, 97, 100, 102, 105, 107, 111, 113, 116,
  119, 121, 124, 126
};

static const uint8_t e12_lt_tuning[] PROGMEM = {
  2, 5, 8, 11, 15, 18, 21, 25, 28, 31, 34, 37, 41, 44, 47, 50, 53, 57, 60, 63, 66, 70,
  73, 76, 79, 82, 85, 89, 92, 95, 98, 101, 105, 108, 111, 114, 118, 121, 124, 127
};

// Older machines that had no table. Measured by sweeping the pitch knob (tools/tuning), on the real MD or in the
// emulator running the stock 1.63 OS. Percussion notes are relative; the CC ladder is what matters.
// E12-OH: autocorr, 0.313 st/cc, max err 0.13 st, CC 0-126 (hardware)
static const uint8_t e12_oh_tuning[] PROGMEM = { 3, 16, 2, 5 };  // formula, 39 notes
// E12-HT: autocorr, 0.347 st/cc, max err 0.43 st, CC 0-103 (hardware)
static const uint8_t e12_ht_tuning[] PROGMEM = { 1, 49, 4, 17 };  // formula, 36 notes
// E12-CC: autocorr, 0.318 st/cc, max err 0.14 st, CC 12-72 (hardware)
static const uint8_t e12_cc_tuning[] PROGMEM = { 14, 22, 6, 7 };  // formula, 19 notes
// E12-RC: autocorr, 0.311 st/cc, max err 0.13 st, CC 0-45 (hardware)
static const uint8_t e12_rc_tuning[] PROGMEM = { 3, 29, 4, 9 };  // formula, 14 notes
// E12-BR: spectral, 0.375 st/cc, max err 0.17 st, CC 0-57 (emulator, OS 1.63)
static const uint8_t e12_br_tuning[] PROGMEM = { 2, 8, 1, 3 };  // formula, 21 notes
// E12-TA: autocorr, 0.190 st/cc, max err 0.15 st, CC 1-127 (emulator, OS 1.63)
static const uint8_t e12_ta_tuning[] PROGMEM = { 2, 21, 3, 4 };  // formula, 24 notes
// E12-TR: autocorr, 0.312 st/cc, max err 0.08 st, CC 3-122 (emulator, OS 1.63)
static const uint8_t e12_tr_tuning[] PROGMEM = { 6, 93, 11, 29 };  // formula, 37 notes
// P-I-BD: autocorr, 0.184 st/cc, max err 0.29 st, CC 12-126 (emulator, OS 1.63)
static const uint8_t p_i_bd_tuning[] PROGMEM = { 17, 49, 2, 9 };  // formula, 21 notes
// P-I-SD: spectral, 0.188 st/cc, max err 0.20 st, CC 8-121 (emulator, OS 1.63)
static const uint8_t p_i_sd_tuning[] PROGMEM = { 9, 69, 1, 13 };  // formula, 22 notes
// P-I-MT: autocorr, 0.188 st/cc, max err 0.25 st, CC 0-126 (emulator, OS 1.63)
static const uint8_t p_i_mt_tuning[] PROGMEM = { 2, 117, 21, 22 };  // formula, 24 notes
// P-I-ML: autocorr, 0.188 st/cc, max err 0.15 st, CC 0-105 (emulator, OS 1.63)
static const uint8_t p_i_ml_tuning[] PROGMEM = { 5, 16, 0, 3 };  // formula, 19 notes
// P-I-RC: autocorr, 0.182 st/cc, max err 0.16 st, CC 0-120 (emulator, OS 1.63)
static const uint8_t p_i_rc_tuning[] PROGMEM = { 3, 11, 0, 2 };  // formula, 22 notes
// P-I-CC: autocorr, 0.184 st/cc, max err 0.12 st, CC 0-120 (emulator, OS 1.63)
static const uint8_t p_i_cc_tuning[] PROGMEM = { 2, 49, 2, 9 };  // formula, 22 notes
// P-I-HH: autocorr, 0.184 st/cc, max err 0.13 st, CC 2-127 (emulator, OS 1.63)
static const uint8_t p_i_hh_tuning[] PROGMEM = { 7, 38, 5, 7 };  // formula, 23 notes

// OS X.14 model patcher machines, measured on hardware (tools/tuning/measure_tunings.py).
// table[i] = CC that sounds note base+i. Sub-oscillators / unison / chorus were off for MM-SAW, MM-PLS, SAWPW.
// MM-DEN: 0.99787 st/cc, max err 0.295 st
static const uint8_t mm_den_tuning[] PROGMEM = { 16, 1, 0, 1 };  // formula, 94 notes
// FM-DY: 0.99926 st/cc, max err 0.239 st
static const uint8_t fm_dy_tuning[] PROGMEM = { 34, 1, 0, 1 };  // formula, 92 notes
// FM-ST: 0.99981 st/cc, max err 0.105 st
static const uint8_t fm_st_tuning[] PROGMEM = { 27, 1, 0, 1 };  // formula, 96 notes
// FM-PA: 1.00005 st/cc, max err 0.058 st
static const uint8_t fm_pa_tuning[] PROGMEM = { 26, 1, 0, 1 };  // formula, 80 notes
// MM-ENS: 0.99988 st/cc, max err 0.077 st
static const uint8_t mm_ens_tuning[] PROGMEM = { 15, 1, 0, 1 };  // formula, 94 notes
// AN-PC: 0.99726 st/cc, max err 0.264 st
static const uint8_t an_pc_tuning[] PROGMEM = { 45, 1, 0, 1 };  // formula, 82 notes
// AN-RC: 0.99963 st/cc, max err 0.149 st
// ACID: 0.375 st/cc, max err 0.004 st
// FM4OP: 0.99861 st/cc, max err 0.179 st
// FORMT: 0.5 st/cc, max err 0.013 st
static const uint8_t formt_tuning[] PROGMEM = { 0, 2, 0, 1 };  // formula, 64 notes
// MM-VO6: 1.00001 st/cc, max err 0.03 st
// MM-SAW: 1.000 st/cc (CC = note), max err 0.08 st, subs off
// SAWPW: 0.500 st/cc, max err 0.01 st, sub/chorus off

static const tuning_t rom_tuning_t PROGMEM = { ROM_MODEL,    45, 
				       sizeof(rom_tuning), 4,   rom_tuning };


static const tuning_t rom_tonal_tuning_t PROGMEM = { ROM_MODEL, MIDI_NOTE_CS0, 64, TUNING_FORMULA | 0, formt_tuning };


static const tuning_t tunings_tonal[] PROGMEM = {

  { EFM_BD_MODEL, MIDI_NOTE_CS0, 64, TUNING_FORMULA | 0, formt_tuning },
  { EFM_SD_MODEL, MIDI_NOTE_CS0, 64, TUNING_FORMULA | 0, formt_tuning },
  { EFM_XT_MODEL, MIDI_NOTE_CS0, 64, TUNING_FORMULA | 0, formt_tuning },
  { EFM_CP_MODEL, MIDI_NOTE_CS2, 64, TUNING_FORMULA | 0, formt_tuning },
  { EFM_RS_MODEL, MIDI_NOTE_CS2, 64, TUNING_FORMULA | 0, formt_tuning },
  { EFM_CB_MODEL, MIDI_NOTE_CS1, 64, TUNING_FORMULA | 0, formt_tuning },
  { EFM_HH_MODEL, MIDI_NOTE_CS0, 64, TUNING_FORMULA | 0, formt_tuning },
  { EFM_CY_MODEL, MIDI_NOTE_CS3, 64, TUNING_FORMULA | 0, formt_tuning },

  { TRX_BD_MODEL, MIDI_NOTE_CS0, 64, TUNING_FORMULA | 0, formt_tuning },
  { TRX_SD_MODEL, MIDI_NOTE_CS1, 64, TUNING_FORMULA | 0, formt_tuning },
  { TRX_XT_MODEL, MIDI_NOTE_CS0, 64, TUNING_FORMULA | 0, formt_tuning },
  { TRX_RS_MODEL, MIDI_NOTE_CS2, 64, TUNING_FORMULA | 0, formt_tuning },
  { TRX_XC_MODEL, MIDI_NOTE_CS1, 64, TUNING_FORMULA | 0, formt_tuning },
  { TRX_B2_MODEL, MIDI_NOTE_CS0, 64, TUNING_FORMULA | 0, formt_tuning },
  { TRX_S2_MODEL, MIDI_NOTE_CS1, 64, TUNING_FORMULA | 0, formt_tuning },

  { GND_SN_MODEL, MIDI_NOTE_CS0, 64, TUNING_FORMULA | 3, formt_tuning },
  { GND_SW_MODEL, MIDI_NOTE_CS0, 64, TUNING_FORMULA | 3, formt_tuning },
  { GND_PU_MODEL, MIDI_NOTE_CS0, 64, TUNING_FORMULA | 3, formt_tuning },
  { NFX_UC_MODEL, MIDI_NOTE_CS0, 64, TUNING_FORMULA | 3, formt_tuning },
};

static const tuning_t tunings[] PROGMEM = {
  { EFM_RS_MODEL, MIDI_NOTE_B4, 48, TUNING_FORMULA | 4, efm_rs_tuning },
  { EFM_HH_MODEL, MIDI_NOTE_B4, 30, TUNING_FORMULA | 8, efm_hh_tuning },
  { EFM_CP_MODEL, MIDI_NOTE_B3, 66, TUNING_FORMULA | 3, efm_cp_tuning },
  { EFM_SD_MODEL, MIDI_NOTE_B3, 30, TUNING_FORMULA | 5, efm_hh_tuning },
  { EFM_XT_MODEL, MIDI_NOTE_F2, 24, TUNING_FORMULA | 7, efm_xt_tuning },
  { EFM_BD_MODEL, MIDI_NOTE_AB1, 48, TUNING_FORMULA | 4, efm_rs_tuning },
  { TRX_CL_MODEL, MIDI_NOTE_B6, 21, TUNING_FORMULA | 7, trx_cl_tuning },
  { TRX_SD_MODEL, MIDI_NOTE_F4, 12, TUNING_FORMULA | 12, trx_sd_tuning },
  { TRX_XC_MODEL, MIDI_NOTE_F3, 24, TUNING_FORMULA | 6, trx_xc_tuning },
  { TRX_XT_MODEL, MIDI_NOTE_B3, 24, TUNING_FORMULA | 6, trx_xt_tuning },
  { TRX_BD_MODEL, MIDI_NOTE_B1, 24, TUNING_FORMULA | 7, trx_bd_tuning },
  { GND_SN_MODEL, MIDI_NOTE_F2, 96, TUNING_FORMULA | 3, gnd_sn_tuning },
  { GND_SW_MODEL, MIDI_NOTE_F2, 96, TUNING_FORMULA | 3, gnd_sn_tuning },
  { GND_PU_MODEL, MIDI_NOTE_F2, 96, TUNING_FORMULA | 3, gnd_sn_tuning },
  { TRX_B2_MODEL, MIDI_NOTE_A1, sizeof(trx_b2_tuning), 8, trx_b2_tuning },
  { TRX_RS_MODEL, MIDI_NOTE_F4, sizeof(trx_rs_tuning), 13, trx_rs_tuning },
  { TRX_S2_MODEL, MIDI_NOTE_F2, sizeof(trx_s2_tuning), 0, trx_s2_tuning },
  { EFM_CB_MODEL, MIDI_NOTE_F3, sizeof(efm_cb_tuning), 5, efm_cb_tuning },
  { ROM_MODEL,    MIDI_NOTE_A3, sizeof(rom_tuning),    4, rom_tuning    },
  { EFM_CY_MODEL, MIDI_NOTE_B2, sizeof(efm_cy_tuning), 6, efm_cy_tuning },
  { E12_BC_MODEL, MIDI_NOTE_D3, sizeof(e12_bc_tuning), 4, e12_bc_tuning },
  { E12_CB_MODEL, MIDI_NOTE_DS3, sizeof(e12_cb_tuning), 4, e12_cb_tuning },
  { E12_LT_MODEL, MIDI_NOTE_FS5, sizeof(e12_lt_tuning), 4, e12_lt_tuning },
  { E12_OH_MODEL, 54, 39, TUNING_FORMULA | 2, e12_oh_tuning },
  { E12_HT_MODEL, 27, 36, TUNING_FORMULA | 1, e12_ht_tuning },
  { E12_CC_MODEL, 80, 19, TUNING_FORMULA | 2, e12_cc_tuning },
  { E12_RC_MODEL, 78, 14, TUNING_FORMULA | 2, e12_rc_tuning },
  { E12_BR_MODEL, 34, 21, TUNING_FORMULA | 1, e12_br_tuning },
  { E12_TA_MODEL, 73, 24, TUNING_FORMULA | 3, e12_ta_tuning },
  { E12_TR_MODEL, 35, 37, TUNING_FORMULA | 2, e12_tr_tuning },
  { P_I_BD_MODEL, 25, 21, TUNING_FORMULA | 3, p_i_bd_tuning },
  { P_I_SD_MODEL, 30, 22, TUNING_FORMULA | 3, p_i_sd_tuning },
  { P_I_MT_MODEL, 38, 24, TUNING_FORMULA | 3, p_i_mt_tuning },
  { P_I_ML_MODEL, 33, 19, TUNING_FORMULA | 3, p_i_ml_tuning },
  { P_I_RC_MODEL, 46, 22, TUNING_FORMULA | 3, p_i_rc_tuning },
  { P_I_CC_MODEL, 39, 22, TUNING_FORMULA | 3, p_i_cc_tuning },
  { P_I_HH_MODEL, 40, 23, TUNING_FORMULA | 3, p_i_hh_tuning },
  { MM_DEN_MODEL, 16, 94, TUNING_FORMULA | 1, mm_den_tuning },
  { MM_FMDY_MODEL, 15, 92, TUNING_FORMULA | 1, fm_dy_tuning },
  { MM_FMST_MODEL, 15, 96, TUNING_FORMULA | 1, fm_st_tuning },
  { MM_FMPA_MODEL, 14, 80, TUNING_FORMULA | 1, fm_pa_tuning },
  { MM_ENS_MODEL, 15, 94, TUNING_FORMULA | 1, mm_ens_tuning },
  { MM_DDR_MODEL, 15, 94, TUNING_FORMULA | 1, mm_ens_tuning },
  { AN_PC_MODEL, 16, 82, TUNING_FORMULA | 1, an_pc_tuning },
  { AN_RC_MODEL, 27, 78, TUNING_FORMULA | 1, fm_st_tuning },
  { CM_ACID_MODEL, 29, 48, TUNING_FORMULA | 1, efm_rs_tuning },
  { CM_SPECT_MODEL, 41, 48, TUNING_FORMULA | 1, efm_rs_tuning },
  { CM_FM4OP_MODEL, 15, 95, TUNING_FORMULA | 1, mm_ens_tuning },
  { CM_FORMT_MODEL, 24, 64, TUNING_FORMULA | 1, formt_tuning },
  { MM_WAV_MODEL, 15, 95, TUNING_FORMULA | 1, mm_ens_tuning },
  { MM_SID_MODEL, 15, 94, TUNING_FORMULA | 1, mm_ens_tuning },
  { MM_VO6_MODEL, 15, 86, TUNING_FORMULA | 1, mm_ens_tuning },
  { MM_SAW_MODEL, 15, 96, TUNING_FORMULA | 1, mm_ens_tuning },
  { MM_PLS_MODEL, 15, 95, TUNING_FORMULA | 1, mm_ens_tuning },
  { CM_SAWPW_MODEL, 24, 55, TUNING_FORMULA | 1, formt_tuning },
};


#if defined(__AVR__)
#define TUNING_RD(p) pgm_read_byte(p)
#define TUNING_PTR(t) ((const uint8_t *)pgm_read_word(&(t)->tuning))
#else
#define TUNING_RD(p) (*(p))
#define TUNING_PTR(t) ((t)->tuning)
#endif

uint8_t tuning_base(const tuning_t *t) { return TUNING_RD(&t->base); }
uint8_t tuning_len(const tuning_t *t) { return TUNING_RD(&t->len); }
uint8_t tuning_offset(const tuning_t *t) {
  return TUNING_RD(&t->offset) & ~TUNING_FORMULA;
}

uint8_t tuning_cc(const tuning_t *t, uint8_t i) {
  const uint8_t *p = TUNING_PTR(t);
  if (!(TUNING_RD(&t->offset) & TUNING_FORMULA)) {
    return pgm_read_byte(p + i);
  }
  uint16_t x = (uint16_t)i * pgm_read_byte(p + 1) + pgm_read_byte(p + 2);
  return pgm_read_byte(p) + x / pgm_read_byte(p + 3);
}

uint8_t tuning_note_from_cc(const tuning_t *t, uint8_t cc) {
  for (uint8_t i = 0; i < tuning_len(t); i++) {
    if (tuning_cc(t, i) >= cc) {
      uint8_t base = tuning_base(t);
      return i + (base - ((base / 12) * 12));
    }
  }
  return 255;
}

uint8_t tuning_cc_from_note(const tuning_t *t, uint8_t note, uint8_t fine_tune) {
  uint8_t base = tuning_base(t);
  note -= base - ((base / 12) * 12);
  if (note >= tuning_len(t)) {
    return 255;
  }
  int8_t pitch = (int8_t)tuning_cc(t, note) + (int8_t)fine_tune - 32;
  if (pitch < 0) {
    return 0;
  }
  return pitch > 127 ? 127 : (uint8_t)pitch;
}

const tuning_t PROGMEM *MDClass::getModelTuning(uint8_t model, bool tonal) {
  uint8_t i;

  // 128..191 is the ROM/RAM sample range, but patcher machines can sit in its
  // gaps (MM-PLS is 175) and must not inherit the ROM sample tuning.
  if ((model >= 128) && (model <= 191) && (model != MM_PLS_MODEL)) {
    //if (tonal) {
    //  return &rom_tonal_tuning_t;
   // }
   // else {
      return &rom_tuning_t;
   // }
  }

  //if ((model >= E12_SD_MODEL) && (model <= E12_BC_MODEL) && (tonal)) {
  //  return &rom_tonal_tuning_t;
  //}


  const tuning_t *t = tunings;
  uint8_t len = countof(tunings);

  if (tonal) {
    t = tunings_tonal;
    len = countof(tunings_tonal);
  }

  for (i = 0; i < len; i++) {
    if (model == TUNING_MODEL(&t[i])) {
      return t + i;
    }
  }

  return NULL;
}

/* @} @} */
