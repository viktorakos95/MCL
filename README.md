# MCL

A high-performance MIDI sequencer, controller and live performance system.
Designed to for use with the Machinedrum and other MIDI devices.

Visit the [releases page](https://github.com/jmamma/MCL/releases) for:
- Latest firmware binaries
- User documentation
- Installation instructions

## Custom Features (this fork)

Two opt-in features on top of official MCL. Manual Step is off by default.
Live Step Repeat is on by default, toggle it off with `ROLL ACTIVE` in the
SYSTEM menu if you don't want it. Discussed with the original author here:
https://github.com/jmamma/MCL/pull/197

**Manual Step.** Lets a MIDI CC message manually advance the Machinedrum
sequencer one step at a time, instead of following the clock. Good for
triggering steps from a pad, footswitch, envelope follower, or anything
that sends a MIDI CC. 
SEQ menu, under QUANT: `STEP MODE`, `STEP CC`, `STEP PORT`. 
Settings save per project.
- Only listens on MIDI2 or USB, never MIDI1.
- Fires on every message with no debounce

**Live Step Repeat / Roll.** Hold LEFT and RIGHT on the Mixer page, then a
trig pad, to repeat that sound at a chosen subdivision for as long as you
hold it. UP and DOWN while held change the rate. 
SYSTEM menu: `ROLL MUTE`, (ignores sound mute so you an roll muted sounds as well)
`ROLL TRIPLETS` (add/remove triplets from the roll subdivision list)
`ROLL ACTIVE` (turns the whole feature on/off, YES by default)
- Only works while the Mixer page is showing. Pressing REC or leaving the page stops the roll.
- Can't currently be captured into a pattern through live record.

### ⚠️ Project compatibility warning

This fork disables project conversion to free up flash space, so it **cannot
open or convert older project formats**. If you have projects from before
MCL 5.00, open and re-save them once with an official 5.00+ build first,
that will upgrade them to the current format. Projects already saved on
5.00, 5.01, or 5.02 need no action.

### Installing this fork

The "Upgrade MCL" auto download steps below always fetch the official
jmamma/MCL release, not this fork, so they will not install this build.
Either use "Building from Source Code" below after cloning this fork
(recommended), or download the `.hex` from this fork's [Releases page](https://github.com/viktorakos95/MCL/releases/tag/5.02-custom) and
flash it yourself with a tool like  Waftlord’s [MCL Hex Flasher](https://wftlrd.uk/mclhexflasher/).

## Documentation

The [MCL User Documentation](https://jmamma.github.io/MCL/) is available online.

The editable manual lives in [`docs/manual`](docs/manual). It is written in
Markdown and built into a static GitHub Pages site with:

```bash
python3 tools/docs/validate_manual.py
python3 tools/docs/build_manual_site.py
```

## Platform Support

MCL can now be built to run across different hardware platforms using PlatformIO.

**Current platforms:**
- **AVR** - MegaCommand DIY and MegaCMD devices
- **RP2350** - TBD
- **RP2040**

## Upgrade MCL

1. **Install PlatformIO Core**
   ```bash
   pip install platformio
   ```

2.  **Clone the Repository**

    First, you need a local copy of the MCL repository.  Open your terminal, navigate to a directory of your choice, and run the following commands:
    ```bash
    git clone https://github.com/jmamma/MCL.git
    cd MCL
    git pull origin master
    ```
3.  **Place the MegaCommand in to OS UPGRADE mode**

    Hold down the < Page > button when powering-on the MegaCommand to enter the boot menu.

    Select "OS UPGRADE" to place the MegaCommand in to serial mode.

4.  **Run the Upload Command**

    Choose the command that corresponds to your hardware:

    **MegaCommand DIY:**
    ```bash
    platformio run -t nobuild -t upload -e megacommand_latest
    ```

    **MegaCMD:**
    ```bash
    platformio run -t nobuild -t upload -e megacmd_latest
    ```

    When you execute one of these commands, a script automatically performs the following steps:
    *   Fetches the latest MCL release manifest from this respostiory.
    *   Downloads the latest firmware file for your selected device.
    *   Flashes the downloaded firmware onto your device.
  
    Should platformio not detect the correct upload port you can specify it like so:
    ```
    platformio run -t nobuild -t upload --upload-port <port> -e <environment>
    ```
## Building from Source Code

Building and uploading MCL from Source:

   **MegaCommand DIY:**
   ```bash
   platformio run -e megacommand -t upload
   ```

   **MegaCMD:**
   ```bash
   platformio run -e megacmd -t upload
   ```

   **TBD:**
   ```bash
   platformio run -e tbd -t upload
   ```

## Architecture

```
.
├── art              Pixel-art and animations
├── build            Release manifest and compiled firmwares. 
├── include          Header files required for building specific libraries
├── resource         Compressable C++ data structures used by MCL
│
├── src
│   ├── mcl          MCL source code
│   ├── platform     Platform specific code
│   └── resources    Compressed C++ data structures
│
└── tools            Various tools for building the firmware
```

The AVR version is built on top of the MegaCore Arduino core, and is extended for relevant platforms in `src/platform/avr`.

The RP2040/RP2350 version is built on top of the arduino-pico core, and is extended for supported hardware in `src/platform/rp2040`.

## Libraries

MCL builds upon proven open-source libraries:
- [ArduinoPico](https://github.com/earlephilhower/arduino-pico) by Earle F. Philhower, III
- [MegaCore](https://github.com/MCUdude/MegaCore) by MCUdude
- [MIDICtrl Framework](https://github.com/wesen/mididuino) by Manuel Odendahl
- [SdFat Library](https://github.com/greiman/SdFat) by Bill Greiman
- [Adafruit GFX Library](https://github.com/adafruit/Adafruit-GFX-Library) by Adafruit






















