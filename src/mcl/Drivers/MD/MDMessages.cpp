/* Copyright (c) 2009 - http://ruinwesen.com/ */

#include "Elektron.h"
#include "MDMessages.h"
#include "MDParams.h"
#include "helpers.h"

#include "MD.h"
#include "MCLMemory.h"

const int8_t md_standard_drum_mapping[16] PROGMEM = {
    36, 38, 40, 41, 43, 45, 47, 48,
    50, 52, 53, 55, 57, 59, 60, 62};

#if defined(__AVR__)
static uint8_t scale_7bit(uint8_t value, uint8_t scale) {
  return ((uint16_t)value * scale) / 127;
}

void MDMachine::scale_vol(uint8_t scale) {
  params[MODEL_VOL] = scale_7bit(params[MODEL_VOL], scale);
  if ((lfo.destinationParam == MODEL_VOL) && (lfo.destinationTrack == track)) {
    params[MODEL_LFOD] = scale_7bit(params[MODEL_LFOD], scale);
    lfo.depth = params[MODEL_LFOD];
  }
}

uint8_t MDMachine::normalize_level() {
  uint8_t scale = level;
  if (scale == 127) {
    return 127;
  }
  level = 127;
  scale_vol(scale);
  return scale;
}
#else
void MDMachine::scale_vol(float scale) {
  params[MODEL_VOL] = (uint8_t)((float)params[MODEL_VOL] * scale);
  if (params[MODEL_VOL] > 127) {
    params[MODEL_VOL] = 127;
  }
  if ((lfo.destinationParam == MODEL_VOL) && (lfo.destinationTrack == track)) {
    params[MODEL_LFOD] = (uint8_t)((float)params[MODEL_LFOD] * scale);

    if (params[MODEL_LFOD] > 127) {
      params[MODEL_LFOD] = 127;
    }
    lfo.depth = params[MODEL_LFOD];
  }
}

float MDMachine::normalize_level() {
  if (level == 127) {
    return 1.0;
  }

  float scale = (float)level / (float)127;
  level = 127;

  scale_vol(scale);
  return scale;
}
#endif

bool MDGlobal::fromSysex(MidiClass *midi) {
  SysexView sysex(midi->midiSysex);
#if defined(__AVR__)
  uint16_t len = sysex.get_recordLen() - 5;
  uint16_t offset = 5;

  if (len < 4) {
    return false;
  }

  if (!ElektronHelper::checkSysexChecksum(sysex, offset, len)) {
    return false;
  }

  uint8_t version = sysex.getByte(offset + 1);
#else
  uint16_t record_len = sysex.get_recordLen();
  if (record_len < 9) {
    return false;
  }
  uint16_t len = record_len - 5;
  uint16_t offset = 5;

  uint8_t version = sysex.getByte(offset + 1);
  // Minimum encoded lengths, including the four-byte message envelope and
  // checksum/length trailer: v1-4=188, v5-6=190, extended=192.
  uint16_t min_len = 188;
  if (version >= 5) min_len += 2;
  if (version == SPSX_GLOBAL_VERSION) min_len += 2;
  if (len < min_len) {
    return false;
  }

  if (!ElektronHelper::checkSysexChecksum(sysex, offset, len)) {
    return false;
  }
#endif

  if (!mdGlobalWireVersionSupported(version)) return false;

  origPosition = sysex.getByte(offset + 3);
  ElektronSysexDecoder decoder(sysex, offset + 4);
  decoder.stop7Bit();
  decoder.get(drumRouting, 16);

  decoder.start7Bit();
  decoder.get(keyMap, 128);
  decoder.stop7Bit();

  decoder.get8(&baseChannel);
  decoder.get8(&unused);
  uint8_t tempo_lower;
  uint8_t tempo_upper;
  decoder.get8(&tempo_upper);
  decoder.get8(&tempo_lower);
  tempo = (tempo_upper << 7) | tempo_lower;
  decoder.get8(&extendedMode);

  uint8_t byte = 0;
  decoder.get8(&byte);
  clockIn = IS_BIT_SET(byte, 0);
  // The MD stores CTRL IN as a disable flag: bit 4 clear means enabled.
  transportIn = !IS_BIT_SET(byte, 4);
  clockOut = IS_BIT_SET(byte, 5);
  transportOut = IS_BIT_SET(byte, 6);
  decoder.getb(&localOn);

  decoder.get(&drumLeft, 10);

  if (version >= 5) {
    decoder.get8(&programChange);
    decoder.get8(&trigMode);
  } else {
    programChange = 0;
    trigMode = 0;
  }

  if (version == SPSX_GLOBAL_VERSION) {
    decoder.get8(&seqTempoMode);
    decoder.get8(&channelMode);
  } else {
    seqTempoMode = 0;
    channelMode = 0;
  }

  for (int i = 0; i < 128; i++) {
    if (keyMap[i] < 16) {
      drumMapping[keyMap[i]] = i;
    }
  }

  return true;
}

uint16_t MDGlobal::toSysex(ElektronDataToSysexEncoder *encoder) {
  uint8_t ver = MD.is_spsx ? SPSX_GLOBAL_VERSION : MDX_GLOBAL_VERSION;
  ElektronHelper::beginSysexEncode(encoder, machinedrum_sysex_hdr, sizeof(machinedrum_sysex_hdr), MD_GLOBAL_MESSAGE_ID, ver, origPosition);

  encoder->pack(drumRouting, 16);

  encoder->start7Bit();
  encoder->pack(keyMap, 128);
  encoder->stop7Bit();

  encoder->pack8(baseChannel);
  encoder->pack8(unused);
  uint8_t tempo_lower = (uint8_t)(tempo & 0x7F);
  uint8_t tempo_upper = (uint8_t)(tempo >> 7);
  encoder->pack8(tempo_upper);
  encoder->pack8(tempo_lower);
  encoder->packb(extendedMode ? 1 : 0);

  uint8_t byte = 0;
  //  clockIn = false;
  //  transportIn = true;
  //  clockOut = true;
  //  transportOut = true;
  if (clockIn)
    //        byte = byte + 1;
    SET_BIT(byte, 0);
  // The MD stores CTRL IN as a disable flag: bit 4 set means disabled.
  if (!transportIn)
    SET_BIT(byte, 4);
  if (clockOut)
    //      byte = byte + 16;
    SET_BIT(byte, 5);
  if (transportOut)
    //        byte = byte + 32;
    SET_BIT(byte, 6);
  encoder->pack8(byte);
  encoder->packb(localOn ? 1 : 0);

  //	encoder->pack(&drumLeft, 12);

  encoder->pack8(drumLeft);
  encoder->pack8(drumRight);

  encoder->pack8(gateLeft);
  encoder->pack8(gateRight);
  encoder->pack8(senseLeft);
  encoder->pack8(senseRight);
  encoder->pack8(minLevelLeft);
  encoder->pack8(minLevelRight);
  encoder->pack8(maxLevelLeft);
  encoder->pack8(maxLevelRight);

  encoder->pack8(programChange);
  encoder->pack8(trigMode);

  if (MD.is_spsx) {
    encoder->pack8(seqTempoMode);
    encoder->pack8(channelMode);
  }

  return ElektronHelper::finishSysexEncode(encoder);
}

uint8_t MDMachine::get_model() { return model; }
bool MDMachine::get_tonal() {
  if ((model & 0x20000UL) != 0) {
    return true;
  }
  return false;
}

#if !defined(__AVR__)
void SPSMachine::scale_vol(float scale) {
  params[MODEL_VOL] = (uint8_t)((float)params[MODEL_VOL] * scale);
  if (params[MODEL_VOL] > 127) {
    params[MODEL_VOL] = 127;
  }
  if ((lfos[0].destinationParam == MODEL_VOL) && (lfos[0].destinationTrack == track)) {
    params[MODEL_LFOD] = (uint8_t)((float)params[MODEL_LFOD] * scale);

    if (params[MODEL_LFOD] > 127) {
      params[MODEL_LFOD] = 127;
    }
    lfos[0].depth = params[MODEL_LFOD];
  }
  if ((lfos[1].destinationParam == MODEL_VOL) && (lfos[1].destinationTrack == track)) {
    params[MODEL_LFO2DEP] = (uint8_t)((float)params[MODEL_LFO2DEP] * scale);

    if (params[MODEL_LFO2DEP] > 127) {
      params[MODEL_LFO2DEP] = 127;
    }
    lfos[1].depth = params[MODEL_LFO2DEP];
  }
}

float SPSMachine::normalize_level() {
  if (level == 127) {
    return 1.0;
  }

  float scale = (float)level / (float)127;
  level = 127;

  scale_vol(scale);
  return scale;
}

uint8_t SPSMachine::get_model() { return model; }
bool SPSMachine::get_tonal() {
  if ((model & 0x20000UL) != 0) {
    return true;
  }
  return false;
}
#endif

// ---- Compact LFO state pack/unpack (SPS-X v65+ kits) ----
// State layout matches host/elektron/MDTypes.cpp (pack/unpack_compact_lfo_state).
// type % 3: 0=FREE (saves 2-byte phase), 1=TRIG (no state), 2=HOLD (saves two int16 holds).
static void pack_compact_lfo_state(const MDLFO &lfo, uint8_t out[4]) {
  uint8_t base_mode = lfo.type % 3;
  if (base_mode == 0) {
    int32_t time;
    memcpy(&time, &lfo.state[27], sizeof(int32_t));
    uint16_t phase = (uint16_t)(time & 0x7FFF);
    memcpy(out, &phase, 2);
    out[2] = 0;
    out[3] = 0;
  } else if (base_mode == 2) {
    int32_t hold0, hold1;
    memcpy(&hold0, &lfo.state[11], sizeof(int32_t));
    memcpy(&hold1, &lfo.state[15], sizeof(int32_t));
    int16_t h0 = (int16_t)hold0;
    int16_t h1 = (int16_t)hold1;
    memcpy(out, &h0, 2);
    memcpy(out + 2, &h1, 2);
  } else {
    memset(out, 0, 4);
  }
}

static void unpack_compact_lfo_state(MDLFO &lfo, const uint8_t in[4], int track) {
  memset(lfo.state, 0, 31);
  uint32_t seed0 = 0x1234 + (uint32_t)(track * 0x1111);
  uint32_t seed1 = 0x5678 + (uint32_t)(track * 0x2222);
  memcpy(&lfo.state[19], &seed0, sizeof(uint32_t));
  memcpy(&lfo.state[23], &seed1, sizeof(uint32_t));

  uint8_t base_mode = lfo.type % 3;
  if (base_mode == 0) {
    uint16_t phase;
    memcpy(&phase, in, 2);
    int32_t time = (int32_t)(phase & 0x7FFF);
    memcpy(&lfo.state[27], &time, sizeof(int32_t));
  } else if (base_mode == 2) {
    int16_t h0, h1;
    memcpy(&h0, in, 2);
    memcpy(&h1, in + 2, 2);
    int32_t hold0 = (int32_t)h0;
    int32_t hold1 = (int32_t)h1;
    memcpy(&lfo.state[11], &hold0, sizeof(int32_t));
    memcpy(&lfo.state[15], &hold1, sizeof(int32_t));
  }
}

void MDKit::get_lfo(uint8_t track, MDLFO *lfo) const {
  memcpy(lfo, &lfos[track], sizeof(MDLFO));
}

void MDKit::set_lfo(uint8_t track, const MDLFO *lfo) {
  memcpy(&lfos[track], lfo, sizeof(MDLFO));
}

uint8_t MDKit::get_model(uint8_t track) { return models[track]; }
bool MDKit::get_tonal(uint8_t track) {
  if ((models[track] & 0x20000UL) != 0) {
    return true;
  }
  return false;
}

uint8_t *MDKit::fx_params(uint8_t fx) {
  switch (fx) {
  case MD_FX_ECHO:
    return delay;
  case MD_FX_DYN:
    return dynamics;
  case MD_FX_REV:
    return reverb;
  case MD_FX_EQ:
    return eq;
  default:
    return nullptr;
  }
}

#if defined(__AVR__)
// Kit 65.1 fields that only kit transfers and track load/save touch live in
// the bank-one tail: the nine-byte LFO-B record of each track (five settings
// and four bytes of run state, as the kit carries them), sixteen output
// routes and the reserved bytes, which go back as received.
static constexpr uint8_t kKit65LfoB = 9;
static constexpr uint8_t kKit65Routes = 16 * kKit65LfoB;
static constexpr uint8_t kKit65Reserved = kKit65Routes + 16;
static_assert(BANK1_KIT65_LEN == kKit65Reserved +
                  (SPS_USER_BUS_FX_COUNT + 1) * SPS_USER_FX_PARAM_COUNT,
              "kit 65.1 store differs from the reserved SRAM region");

static volatile uint8_t *kit65_ptr(uint8_t at) {
  return reinterpret_cast<volatile uint8_t *>(uint16_t(BANK1_KIT65_START + at));
}

static void kit65_write(uint8_t at, const uint8_t *src, uint8_t count) NOINLINE();
static void kit65_write(uint8_t at, const uint8_t *src, uint8_t count) {
  volatile uint8_t *dst = kit65_ptr(at);
  while (count--) {
    put_byte_bank1(dst++, *src++);
  }
}

static void kit65_read(uint8_t at, uint8_t *dst, uint8_t count) NOINLINE();
static void kit65_read(uint8_t at, uint8_t *dst, uint8_t count) {
  volatile uint8_t *src = kit65_ptr(at);
  while (count--) {
    *dst++ = get_byte_bank1(src++);
  }
}
#endif

// The Machinedrum's defaults for a new Kit or machine: envelope open
// (ATK 0, HLD 0, DCY 127, EMX 127), LFO-B speed centred, retrig off at the
// fastest time (1/128) with envelope reset on, no bus sends, ENV DEST AMP.
static const uint8_t md_extended_param_defaults[SPS_PARAMS_PER_TRACK -
                                                MD_PARAMS_PER_TRACK] PROGMEM = {
    0, 0, 127, 127, 64, 0, 0, 0, MD_RTIM_FASTEST_INDEX, 1, 0, 0, 0, 0};

void MDMachine::init_extended() {
  memcpy_P(&params[MD_PARAMS_PER_TRACK], md_extended_param_defaults,
           sizeof(md_extended_param_defaults));
  memset(lfoB, 0, sizeof(lfoB));
  lfoB[0] = track;
  outputRouting = MD_KIT_ROUTE_GLOBAL;
}

void MDMachine::upgrade_v1() {
  // Everything after the parameter row moves up by the 14 new parameters.
  memmove(&track, &params[MD_PARAMS_PER_TRACK],
          sizeof(MDMachineV1) - MD_PARAMS_PER_TRACK);
  init_extended();
}

void MDKit::get_ext(uint8_t track, MDMachine *machine) const {
#if defined(__AVR__)
  kit65_read(track * kKit65LfoB, machine->lfoB, 5);
  kit65_read(kKit65Routes + track, &machine->outputRouting, 1);
#else
  memcpy(machine->lfoB, &lfosB[track].destinationTrack, 5);
  machine->outputRouting = outputRouting[track];
#endif
}

void MDKit::set_ext(uint8_t track, const MDMachine *machine) {
#if defined(__AVR__)
  // The run state stays with the track, as it does in the full record. It is
  // kept in the kit's packed form, where FREE and HOLD share the same bytes,
  // so a change between them starts the new mode from rest.
  uint8_t record[kKit65LfoB];
  kit65_read(track * kKit65LfoB, record, sizeof(record));
  if (record[4] % 3 != machine->lfoB[4] % 3) {
    memset(record + 5, 0, 4);
  }
  memcpy(record, machine->lfoB, 5);
  kit65_write(track * kKit65LfoB, record, sizeof(record));
  kit65_write(kKit65Routes + track, &machine->outputRouting, 1);
#else
  memcpy(&lfosB[track].destinationTrack, machine->lfoB, 5);
  outputRouting[track] = machine->outputRouting;
#endif
}

bool MDKit::fromSysex(MidiClass *midi) {
  SysexView sysex(midi->midiSysex);
#if defined(__AVR__)
  uint16_t len = sysex.get_recordLen() - 5;
  uint16_t offset = 5;

  if (len < 420) {
    DEBUG_PRINTLN(F("kit too short"));
    return false;
  }

  if (!ElektronHelper::checkSysexChecksum(sysex, offset, len)) {
    DEBUG_PRINTLN(F("wrong checksum"));
    return false;
  }
#else
  uint16_t record_len = sysex.get_recordLen();
  if (record_len < 9) {
    return false;
  }
  uint16_t len = record_len - 5;
  uint16_t offset = 5;
#endif

  uint8_t version = sysex.getByte(1 + offset);

  // Accept stock (1-4), MDX (64), and SPS-X v65.
  bool is_spsx_kit = false;
  if ((version >= 1 && version <= 4) || version == MDX_KIT_VERSION) {
    /* legacy/MDX layout: 24 params/track, full 36-byte LFO-A, no LFO-B */
  } else if (version == SPSX_KIT_VERSION) {
    is_spsx_kit = true;
#if defined(__AVR__)
    if (len != mdKitChecksumLengthForVersion(SPSX_KIT_VERSION)) {
      return false;
    }
#endif
  } else {
    DEBUG_PRINTLN(F("unknown kit version"));
    return false;
  }

#if !defined(__AVR__)
  const uint16_t expected_len = mdKitChecksumLengthForVersion(version);
  if (len != expected_len) {
    DEBUG_PRINTLN(F("kit too short"));
    return false;
  }

  if (!ElektronHelper::checkSysexChecksum(sysex, offset, len)) {
    DEBUG_PRINTLN(F("wrong checksum"));
    return false;
  }
  if (is_spsx_kit) {
    const uint16_t routes = record_len - 4u - 16u - (SPS_USER_BUS_FX_COUNT + 1u) * SPS_USER_FX_PARAM_COUNT;
    for (uint8_t track = 0; track < 16; ++track)
      if (!mdKitRouteValid(sysex.getByte(routes + track))) return false;
  }
  memset(outputRouting, MD_KIT_ROUTE_GLOBAL, sizeof(outputRouting));

#endif

  origPosition = sysex.getByte(3 + offset);
  ElektronSysexDecoder decoder(sysex, offset + 4);
  decoder.stop7Bit();
  decoder.get((uint8_t *)name, 16);
  name[16] = '\0';

  const uint8_t params_per_track =
      version == SPSX_KIT_VERSION ? SPS_PARAMS_PER_TRACK : MD_PARAMS_PER_TRACK;
  for (uint8_t i = 0; i < 16; i++) {
    decoder.get((uint8_t *)params[i], params_per_track);
  }

  decoder.get(levels, 16);

  decoder.start7Bit();
  decoder.get32(models, 16);
  decoder.stop7Bit();

  // LFO-A section: compact 9 bytes/track for SPS-X, full 36 for legacy/MDX
  if (is_spsx_kit) {
    decoder.start7Bit();
    for (uint8_t i = 0; i < 16; i++) {
      uint8_t compact[9];
      decoder.get(compact, 9);
      lfos[i].destinationTrack = compact[0];
      lfos[i].destinationParam = compact[1];
      lfos[i].shape1 = compact[2];
      lfos[i].shape2 = compact[3];
      lfos[i].type = compact[4];
      unpack_compact_lfo_state(lfos[i], compact + 5, i);
      lfos[i].speed = 64;
      lfos[i].depth = 0;
      lfos[i].mix = 0;
    }
    decoder.stop7Bit();
  } else {
    decoder.start7Bit();
    for (uint8_t i = 0; i < 16; i++) {
      decoder.get((uint8_t *)&lfos[i], 5 + 31);
    }
    decoder.stop7Bit();
  }

  // reverb/delay/eq/dynamics are four consecutive uint8_t[8] members; read the
  // 32-byte run in one call (identical bytes, fewer call sites).
  decoder.get(reverb, 32);

  // trigGroups/muteGroups are two consecutive uint8_t[16] members; read the
  // 32-byte run in one call (stream-based 7-bit, identical bytes).
  decoder.start7Bit();
  decoder.get(trigGroups, 32);
  decoder.stop7Bit();

#if !defined(__AVR__)
  if (is_spsx_kit) {
    // SPS-X LFO-B section: compact 9 bytes/track (lives after trig/mute groups)
    decoder.start7Bit();
    for (uint8_t i = 0; i < 16; i++) {
      uint8_t compact[9];
      decoder.get(compact, 9);
      lfosB[i].destinationTrack = compact[0];
      lfosB[i].destinationParam = compact[1];
      lfosB[i].shape1 = compact[2];
      lfosB[i].shape2 = compact[3];
      lfosB[i].type = compact[4];
      unpack_compact_lfo_state(lfosB[i], compact + 5, i);
      // speed/depth/mix live in params[MODEL_LFO2SPD/DEP/MIX]; do not
      // mirror them into the lfosB struct fields. The wire format never
      // carries them in the LFO-B compact section, and host treats
      // params[] as the single source of truth (MDTypes.cpp:454-466).
    }
    decoder.stop7Bit();

    decoder.get(outputRouting, sizeof(outputRouting));
    decoder.get(&userBusFx[0][0], sizeof(userBusFx));
    decoder.get(userPostFx, sizeof(userPostFx));
  } else {
    // Legacy/MDX kit: synthesize SPS-X side state with safe defaults so a
    // later toSysex(SPS-X) round-trips cleanly. Mirrors host MDTypes.cpp
    // (versions <65 fallback) — envelope bypass-on, retrig off + RENV on.
    for (uint8_t i = 0; i < 16; i++) {
      memcpy_P(&params[i][MD_PARAMS_PER_TRACK], md_extended_param_defaults,
               sizeof(md_extended_param_defaults));
      lfosB[i].init(i);
    }
    init_user_fx();
  }
#else
  {
    // LFO-B records, then routing and the reserved bytes. A legacy kit gets
    // the defaults.
    uint8_t ext[kKit65Reserved - kKit65Routes +
                (SPS_USER_BUS_FX_COUNT + 1) * SPS_USER_FX_PARAM_COUNT];
    decoder.start7Bit();
    for (uint8_t i = 0; i < 16; i++) {
      uint8_t compact[kKit65LfoB] = {i, 0, 0, 0, 0, 0, 0, 0, 0};
      if (is_spsx_kit) {
        decoder.get(compact, sizeof(compact));
      } else {
        memcpy_P(&params[i][MD_PARAMS_PER_TRACK], md_extended_param_defaults,
                 sizeof(md_extended_param_defaults));
      }
      kit65_write(i * kKit65LfoB, compact, sizeof(compact));
    }
    decoder.stop7Bit();
    memset(ext, MD_KIT_ROUTE_GLOBAL, 16);
    memset(ext + 16, SPS_USER_FX_DEFAULT_PARAM, sizeof(ext) - 16);
    if (is_spsx_kit) {
      decoder.get(ext, sizeof(ext));
    }
    kit65_write(kKit65Routes, ext, sizeof(ext));
  }
#endif

  DEBUG_PRINTLN(F("md kit okay"));
  return true;
}

uint16_t MDKit::toSysex() {
  ElektronDataToSysexEncoder encoder(MD.uart);
  return toSysex(&encoder);
}

uint16_t MDKit::toSysex(ElektronDataToSysexEncoder *encoder) {
  // Pick the current SPS-X wire version when SPS firmware is connected.
  // Stock MD firmware accepts version 64 with the legacy 24-param/36-byte-LFO
  // layout. Real differentiation between stock and MDX isn't needed: both parse
  // the same body; only the version byte differs.
#if !defined(__AVR__)
  const bool emit_spsx = MD.is_spsx;
  const uint8_t kit_ver =
      emit_spsx ? SPSX_KIT_VERSION : MDX_KIT_VERSION;
#else
  const bool emit_spsx = MD.x20();
  const uint8_t kit_ver = emit_spsx ? SPSX_KIT_VERSION : MDX_KIT_VERSION;
#endif

#if !defined(__AVR__)
  if (emit_spsx)
    for (uint8_t route : outputRouting)
      if (!mdKitRouteValid(route)) return 0;
#endif

  ElektronHelper::beginSysexEncode(encoder, machinedrum_sysex_hdr,
                                   sizeof(machinedrum_sysex_hdr),
                                   MD_KIT_MESSAGE_ID, kit_ver, origPosition);

  encoder->pack((uint8_t *)name, 16);
  name[16] = '\0';

  const uint8_t params_per_track =
      emit_spsx ? SPS_PARAMS_PER_TRACK : MD_PARAMS_PER_TRACK;
  for (uint8_t i = 0; i < 16; i++) {
    encoder->pack((uint8_t *)params[i], params_per_track);
  }
  encoder->pack(levels, 16);

  encoder->start7Bit();
  for (uint8_t track = 0; track < 16; ++track) {
    // Legacy kit recipients understand tonal on/off, but not octave flags.
    const uint32_t model = kit_ver == SPSX_KIT_VERSION ? models[track]
                                                     : models[track] & ~0xc0000UL;
    encoder->pack32(model);
  }
  encoder->stop7Bit();

  // LFO-A section: compact 9 bytes/track for SPS-X, full 36 for MDX/legacy.
  if (emit_spsx) {
    encoder->start7Bit();
    for (uint8_t i = 0; i < 16; i++) {
      encoder->pack(&lfos[i].destinationTrack, 5);
      uint8_t compact_state[4];
      pack_compact_lfo_state(lfos[i], compact_state);
      encoder->pack(compact_state, 4);
    }
    encoder->stop7Bit();
  } else {
    encoder->start7Bit();
    for (uint8_t i = 0; i < 16; i++) {
      // Ensure LFSR magic marker is set for firmware compat
      lfos[i].ensure_lfsr_magic();
      encoder->pack((uint8_t *)&lfos[i], 5 + 31);
    }
    encoder->stop7Bit();
  }

  // reverb/delay/eq/dynamics are four consecutive uint8_t[8] members; pack the
  // 32-byte run in one call (identical bytes, fewer call sites).
  encoder->pack(reverb, 32);

  // trigGroups/muteGroups are two consecutive uint8_t[16] members; pack the
  // 32-byte run in one call. 7-bit packing is stream-based, so the emitted
  // bytes are identical to two separate pack calls.
  encoder->start7Bit();
  encoder->pack(trigGroups, 32);
  encoder->stop7Bit();

#if !defined(__AVR__)
  if (emit_spsx) {
    // SPS-X LFO-B section: compact 9 bytes/track, after trig/mute groups.
    encoder->start7Bit();
    for (uint8_t i = 0; i < 16; i++) {
      encoder->pack(&lfosB[i].destinationTrack, 5);
      uint8_t compact_state[4];
      pack_compact_lfo_state(lfosB[i], compact_state);
      encoder->pack(compact_state, 4);
    }
    encoder->stop7Bit();

    encoder->pack(outputRouting, sizeof(outputRouting));
    encoder->pack(&userBusFx[0][0], sizeof(userBusFx));
    encoder->pack(userPostFx, sizeof(userPostFx));
  }
#else
  if (emit_spsx) {
    uint8_t ext[kKit65Reserved - kKit65Routes +
                (SPS_USER_BUS_FX_COUNT + 1) * SPS_USER_FX_PARAM_COUNT];
    encoder->start7Bit();
    for (uint8_t i = 0; i < 16; i++) {
      kit65_read(i * kKit65LfoB, ext, kKit65LfoB);
      encoder->pack(ext, kKit65LfoB);
    }
    encoder->stop7Bit();
    kit65_read(kKit65Routes, ext, sizeof(ext));
    encoder->pack(ext, sizeof(ext));
  }
#endif

  return ElektronHelper::finishSysexEncode(encoder);
}

uint8_t swapNumber(uint8_t num, uint8_t a, uint8_t b) {
  if (num == a) {
    return b;
  } else if (num == b) {
    return a;
  } else {
    return num;
  }
}

void MDKit::swapTracks(uint8_t srcTrack, uint8_t dstTrack) {
#if !defined(__AVR__)
  const uint8_t route = outputRouting[srcTrack];
  outputRouting[srcTrack] = outputRouting[dstTrack];
  outputRouting[dstTrack] = route;
#endif

  uint8_t _params[MD_KIT_PARAMS_PER_TRACK];
  uint8_t _level;
  uint32_t _model;
  MDLFO _lfo;
  uint8_t _trigGroup;
  uint8_t _muteGroup;

  /* adjust lfo destination, as well as trig and mute groups. */
#if 0
	for (uint8_t i = 0; i < 16; i++) {
		lfos[i].destinationTrack = swapNumber(lfos[i].destinationTrack, srcTrack, dstTrack);
		trigGroups[i] = swapNumber(trigGroups[i], srcTrack, dstTrack);
		muteGroups[i] = swapNumber(muteGroups[i], srcTrack, dstTrack);
		if (models[i] == CTR_8P_MODEL) {
			for (uint8_t j = 8; j < 24; j += 2) {
				params[i][j] = swapNumber(params[i][j], srcTrack, dstTrack);
			}
		}
	}
#endif

  /* swap params */
  memcpy(_params, params[srcTrack], MD_KIT_PARAMS_PER_TRACK);
  memcpy(params[srcTrack], params[dstTrack], MD_KIT_PARAMS_PER_TRACK);
  memcpy(params[dstTrack], _params, MD_KIT_PARAMS_PER_TRACK);

  memcpy(&_lfo, &lfos[srcTrack], sizeof(_lfo));
  memcpy(&lfos[srcTrack], &lfos[dstTrack], sizeof(_lfo));
  memcpy(&lfos[dstTrack], &_lfo, sizeof(_lfo));

#if !defined(__AVR__)
  memcpy(&_lfo, &lfosB[srcTrack], sizeof(_lfo));
  memcpy(&lfosB[srcTrack], &lfosB[dstTrack], sizeof(_lfo));
  memcpy(&lfosB[dstTrack], &_lfo, sizeof(_lfo));
#endif

  /* swap level, model, trig group, mute group */
  _level = levels[srcTrack];
  levels[srcTrack] = levels[dstTrack];
  levels[dstTrack] = _level;

  _model = models[srcTrack];
  models[srcTrack] = models[dstTrack];
  models[dstTrack] = _model;

  _trigGroup = trigGroups[srcTrack];
  trigGroups[srcTrack] = trigGroups[dstTrack];
  trigGroups[dstTrack] = _trigGroup;

  _muteGroup = muteGroups[srcTrack];
  muteGroups[srcTrack] = muteGroups[dstTrack];
  muteGroups[dstTrack] = _muteGroup;
}

bool MDSong::fromSysex(MidiClass *midi) {
  SysexView sysex(midi->midiSysex);
  uint16_t len = sysex.get_recordLen() - 5;
  uint16_t offset = 5;

  if (len < 0x1a - 7)
    return false;

  if (!ElektronHelper::checkSysexChecksum(sysex, offset, len)) {
    return false;
  }

  numRows = (len - (0x1A - 7)) / 12;

  origPosition = sysex.getByte(offset + 3);
  ElektronSysexDecoder decoder(sysex, offset + 4);
  decoder.stop7Bit();
  decoder.get((uint8_t *)name, 16);
  name[16] = '\0';

  for (int i = 0; i < numRows; i++) {
    decoder.start7Bit();
    decoder.get((uint8_t *)&rows[i], 4);
    decoder.get16(&rows[i].mutes);
    decoder.get16(&rows[i].tempo);
    decoder.get(&rows[i].startPosition, 2);
    decoder.stop7Bit();
  }

  return true;
}

void MDKit::init_eq() {
  eq[MD_EQ_LF]   = 0x40;
  eq[MD_EQ_LG]   = 0x40;
  eq[MD_EQ_HF]   = 0x40;
  eq[MD_EQ_HG]   = 0x40;
  eq[MD_EQ_PF]   = 0x40;
  eq[MD_EQ_PG]   = 0x40;
  eq[MD_EQ_PQ]   = 0x40;
  eq[MD_EQ_GAIN] = 0x7F;
}

void MDKit::init_dynamix() {
  dynamics[MD_DYN_ATCK] = 0x7F;
  dynamics[MD_DYN_REL]  = 0x7F;
  dynamics[MD_DYN_TRHD] = 0x7F;
  dynamics[MD_DYN_RTIO] = 0x7F;
  dynamics[MD_DYN_KNEE] = 0x7F;
  dynamics[MD_DYN_HP]   = 0x7F;
  dynamics[MD_DYN_OUTG] = 0;
  dynamics[MD_DYN_MIX]  = 0;
}

#if !defined(__AVR__)
void MDKit::init_user_fx() {
  memset(userBusFx, SPS_USER_FX_DEFAULT_PARAM, sizeof(userBusFx));
  memset(userPostFx, SPS_USER_FX_DEFAULT_PARAM, sizeof(userPostFx));
}
#endif

uint16_t MDSong::toSysex(ElektronDataToSysexEncoder *encoder) {
  ElektronHelper::beginSysexEncode(encoder, machinedrum_sysex_hdr, sizeof(machinedrum_sysex_hdr), MD_PATTERN_MESSAGE_ID, 0x04, origPosition);
  encoder->pack((uint8_t *)name, 16);

  for (uint8_t i = 0; i < numRows; i++) {
    encoder->start7Bit();
    encoder->pack((uint8_t *)&rows[i].pattern, 4);
    encoder->pack16(rows[i].mutes);
    encoder->pack16(rows[i].tempo);
    encoder->pack(&rows[i].startPosition, 2);
    encoder->stop7Bit();
  }

  return ElektronHelper::finishSysexEncode(encoder);
}
