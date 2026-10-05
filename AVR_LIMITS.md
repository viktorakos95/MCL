# AVR (MegaCommand / MegaCMD) limits

The AVR build is close to full on three separate budgets. Adding a feature can break
any of them, and only some failures show up at build time. Check all three before
flashing.

## 1. Flash (program size)

- **Hard limit in practice: 960 pages = 245,760 bytes (240 KB).** A bigger image
  flashes fine but then shows "SD CARD ERROR" or loops on the boot screen
  (seen with 968 pages = 247,654 bytes; 245,744 and smaller boot). The cause is not
  known (the chip and bootloader would allow more), so treat it as fixed.
  `board_upload.maximum_size = 245760` in `platformio.ini` makes the `megacommand` build
  fail above it (observed on a MegaCommand; the MegaCMD build keeps PlatformIO's default). The web flasher shows the page count before writing.
- Shown by PlatformIO as `Flash: ... used N bytes from 245760`.
- Past trade-offs made in this fork to fit features:
  - Project conversion disabled on AVR (`MCL_DISABLE_PROJECT_CONVERSION`), so older
    project formats can't be opened (see README warning). Made room for manual step.
  - File browser MOVE disabled on AVR. Made room for the Euclidean sequencer.
  - WAV Designer was disabled for a while, then swapped for the project-conversion
    flag at the maintainer's request.
- Ways used to save flash without losing features: chromatic tables that are exact
  straight lines are stored as 4-byte formulas (`TUNING_FORMULA` in `MDParams.h`),
  identical formulas are shared, and the note/CC conversion lives in one place
  (`tuning_note_from_cc` / `tuning_cc_from_note`).
- Watch for `always_inline` functions with more than one call site: each call
  copies the whole body. `run_md_tick` (called from `seq()` and manual step) is
  `noinline` on AVR for this reason, which saved ~470 bytes.
- Read-only data read with `pgm_read_byte` must stay in the first 64 KB of flash.
  Today all PROGMEM data ends below ~16 KB, so this is not close, but check with
  `avr-nm -n firmware.elf` if a lot of tables are added.

## 2. RAM (.data + .bss)

- Shown as `RAM: ... used N bytes` (external RAM, the 8192 figure is misleading).
- When it is exceeded the link fails with
  `section '.bss' is not within region 'data'`.
- Plain `const` arrays of structs end up in RAM on AVR. Large constant tables must be
  `PROGMEM` and read with `pgm_read_*`. The chromatic tuning index (`tunings[]`) was
  moved to flash for this reason (read through the `TUNING_*` accessors in
  `MDParams.h`).

## 3. Resource buffer (the one that causes "SD CARD ERROR")

- `ResourceManager` unpacks the compressed resources from `resource/*.cpp` into a
  fixed buffer, `RM_BUFSIZE` = 6500 bytes on AVR, with no run-time size check.
- Pages load several resources at once. If their unpacked sizes add up to more
  than the buffer, memory is silently overwritten. The usual symptom is
  **"SD CARD ERROR :-("** at boot or random failures, while the build itself
  succeeds and Flash/RAM look fine.
- Largest combinations (unpacked bytes, see `__total_size` in
  `src/resources/avr/R.h`):
  - SeqPage: icons_knob + machine_names_short + machine_param_names
  - FXPage: icons_knob + icons_page + machine_param_names
  - MenuPage: icons_knob + machine_names_short + menu_layouts + menu_options + options
  - GridPage: icons_knob + icons_logo + machine_names_short
- `ResourceManager.cpp` has `static_assert`s for these sets, so an oversized
  resource now fails the AVR build instead of failing on the device.
- Example: adding parameter labels for the X.14 patcher machines grew
  `machine_param_names` from 4045 to 5135 bytes and pushed SeqPage to 6769 bytes,
  which gave the SD CARD ERROR. Those labels are now left out on AVR
  (`#ifndef AVR` in `resource/machine_param_names.cpp`); the machine names and the
  chromatic tables are kept.
