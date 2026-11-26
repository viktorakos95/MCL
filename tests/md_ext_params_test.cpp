// Names, value text, edit ranges and defaults of the Machinedrum's track
// parameters 24-37, as MDX shows and sets them.
// cmake --build BUILD_DIR --target mcl_md_ext_params_test && BUILD_DIR/mcl_md_ext_params_test
#include "MD.h"
#include "MDMessages.h"
#include "MDParams.h"
#include "DeviceCapabilities.h"

#include <cstdio>
#include <cstring>

namespace {

int failures = 0;

void expect(bool ok, const char *what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

void expect_text(uint8_t param, uint8_t value, const char *text, bool named) {
  char out[4] = {'?', '?', '?', '?'};
  const bool result = md_param_value_text(param, value, out);
  if (result != named || std::strcmp(out, text) != 0) {
    std::fprintf(stderr, "FAIL: param %u value %u gave \"%.4s\" (%d), want \"%s\" (%d)\n",
                 param, value, out, result, text, named);
    ++failures;
  }
}

void expect_range(uint8_t param, uint8_t min, uint8_t max) {
  const MidiDeviceParamRange range = md_param_range(param);
  if (range.min != min || range.max != max) {
    std::fprintf(stderr, "FAIL: param %u range %u-%u, want %u-%u\n", param,
                 range.min, range.max, min, max);
    ++failures;
  }
}

void check_names() {
  static const char *const names[SPS_EXTENDED_PARAM_COUNT] = {
      "ATK", "HLD", "DCY", "EMX", "SPD", "DEP", "SHP",
      "RTG", "RTM", "REN", "BS1", "BS2", "BS3", "DST"};
  for (uint8_t i = 0; i < SPS_EXTENDED_PARAM_COUNT; i++) {
    const char *name = model_param_name(GND_SN_MODEL, MD_PARAMS_PER_TRACK + i);
    if (name == nullptr || std::strcmp(name, names[i]) != 0) {
      std::fprintf(stderr, "FAIL: param %u named \"%s\", want \"%s\"\n",
                   MD_PARAMS_PER_TRACK + i, name ? name : "(null)", names[i]);
      ++failures;
    }
  }
}

void check_value_text() {
  expect_text(MODEL_RTRG, 0, "OFF", true);
  expect_text(MODEL_RTRG, 1, "1", false);
  expect_text(MODEL_RTRG, 126, "126", false);
  expect_text(MODEL_RTRG, 127, "INF", true);

  // 1/N: stored values below 2 read as 1/128, above 14 as 1/2.
  expect_text(MODEL_RTIM, 0, "128", true);
  expect_text(MODEL_RTIM, 1, "128", true);
  expect_text(MODEL_RTIM, 2, "128", true);
  expect_text(MODEL_RTIM, 3, "96", true);
  expect_text(MODEL_RTIM, 8, "16", true);
  expect_text(MODEL_RTIM, 13, "3", true);
  expect_text(MODEL_RTIM, 14, "2", true);
  expect_text(MODEL_RTIM, 15, "2", true);
  expect_text(MODEL_RTIM, 127, "2", true);

  expect_text(MODEL_RENV, 0, "OFF", true);
  expect_text(MODEL_RENV, 1, "ON", true);
  expect_text(MODEL_RENV, 64, "ON", true);

  expect_text(MODEL_ENVDEST, 0, "AMP", true);
  expect_text(MODEL_ENVDEST, 1, "FLF", true);
  expect_text(MODEL_ENVDEST, 2, "FLW", true);
  expect_text(MODEL_ENVDEST, 3, "AMP", true);
  expect_text(MODEL_ENVDEST, 127, "AMP", true);

  expect_text(MODEL_ENVATT, 64, "64", false);
  expect_text(MODEL_BUS3, 0, "0", false);
  expect_text(MODEL_LFO2MIX, 127, "127", false);
  expect_text(MODEL_P1, 5, "5", false);
}

void check_ranges() {
  expect_range(MODEL_RTRG, 0, 127);
  expect_range(MODEL_RTIM, 2, 14);
  expect_range(MODEL_RENV, 0, 1);
  expect_range(MODEL_ENVDEST, 0, 2);
  expect_range(MODEL_ENVATT, 0, 127);
  expect_range(MODEL_BUS1, 0, 127);
  expect_range(MODEL_P1, 0, 127);

  const MidiDeviceParamRange rtim = md_param_range(MODEL_RTIM);
  expect(rtim.clamp(0) == 2 && rtim.clamp(1) == 2 && rtim.clamp(9) == 9 &&
             rtim.clamp(15) == 14,
         "RTIM edits land in 2-14");
}

// The MD driver's parameter capability is what the hosted pages ask.
void check_device_capability() {
  DeviceParamCapability *params = MD.params();
  expect(params != nullptr, "MD has a parameter capability");
  if (params == nullptr) return;
  const DeviceContext ctx = DeviceContext::primary(&MD);
  char out[4] = {};
  expect(params->param_value_text(ctx, 3, MODEL_RTRG, 127, out) &&
             std::strcmp(out, "INF") == 0,
         "track RTRG 127 reads INF through the capability");
  expect(!params->param_value_text(ctx, 3, MODEL_P1, 10, out),
         "track P1 reads as a number through the capability");
  expect(!params->param_value_text(ctx, NUM_MD_TRACKS, 0, 0, out),
         "FX parameters read as numbers");
  const MidiDeviceParamRange rtim = params->param_range(ctx, 3, MODEL_RTIM);
  expect(rtim.min == 2 && rtim.max == 14,
         "track RTIM range through the capability");
  const MidiDeviceParamRange fx = params->param_range(ctx, NUM_MD_TRACKS, 1);
  expect(fx.min == 0 && fx.max == 127, "FX range is 0-127");
}

void check_defaults() {
  static const uint8_t expected[SPS_EXTENDED_PARAM_COUNT] = {
      0, 0, 127, 127, 64, 0, 0, 0, 2, 1, 0, 0, 0, 0};

  MDMachine machine;
  std::memset(&machine, 0x55, sizeof(machine));
  machine.track = 3;
  machine.init();
  expect(std::memcmp(machine.params + MD_PARAMS_PER_TRACK, expected,
                     sizeof(expected)) == 0,
         "new MD machine gets MDX's parameter 24-37 defaults");

  SPSMachine sps;
  std::memset(&sps, 0x55, sizeof(sps));
  sps.track = 3;
  sps.init();
  expect(std::memcmp(sps.params + MD_PARAMS_PER_TRACK, expected,
                     sizeof(expected)) == 0,
         "new SPS-X machine gets MDX's parameter 24-37 defaults");
}

} // namespace

int main() {
  check_names();
  check_value_text();
  check_ranges();
  check_device_capability();
  check_defaults();
  if (failures != 0) {
    std::fprintf(stderr, "Machinedrum parameters 24-37: %d failures\n",
                 failures);
    return 1;
  }
  std::fprintf(stderr,
               "Machinedrum parameters 24-37 passed: names, value text, "
               "edit ranges and new machine defaults\n");
  return 0;
}
