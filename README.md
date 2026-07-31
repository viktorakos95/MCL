# MCL

A high-performance MIDI sequencer, controller and live performance system.
Designed to for use with the Machinedrum and other MIDI devices.

Visit the [releases page](https://github.com/jmamma/MCL/releases) for:
- Latest firmware binaries
- User documentation
- Installation instructions

## Custom Features (this fork)

Two opt-in features on top of official MCL, both off by default. Discussed
with the original author here: https://github.com/jmamma/MCL/pull/197

**Manual Step.** Lets a MIDI CC message manually advance the Machinedrum
sequencer one step at a time, instead of the sequencer following the clock
like normal. This is useful if you want to trigger steps from a pad, a
footswitch, an envelope follower, or basically anything that can send a
MIDI CC. The clock itself keeps running normally for everything else (other
gear, LFOs), only the MD step advance gets taken over. Turn it on in the SEQ
menu, right under QUANT: `STEP MODE` turns it on/off, `STEP CC` picks the CC
number, `STEP PORT` picks MIDI2 or USB (it only ever listens on those two,
never MIDI1, so it can't collide with the Machinedrum's own CC traffic).
Settings are saved per project. One thing to know: it fires on every single
message on that CC, with no debounce at all, so a fast or continuous CC
source (like an envelope follower left open) can advance steps a lot faster
than you meant it to.

**Live Step Repeat / Roll.** On the Mixer page, hold LEFT and RIGHT together,
then hold a trig pad, and it repeats that track's sound at a chosen
subdivision for as long as you hold it, instead of waiting for its next
scheduled step. While LEFT and RIGHT are held, UP and DOWN cycle through the
subdivision (1/4 down to 1/64, including triplets), and a small card on
screen shows the current rate while it's armed. Two related options live in
the SYSTEM menu: `ROLL MUTE` decides whether rolling a muted track
temporarily unmutes it for the roll or just stays silent, and `ROLL
TRIPLETS` decides whether triplet rates show up at all when cycling. A few
things worth knowing: it only works while the Mixer page is showing and the
Primary device is selected, pressing REC or leaving that page stops the
roll, and there is currently no way to record a roll into a pattern through
live record.

### ⚠️ Project compatibility warning

This fork disables project conversion to free up flash space, so it **cannot
open or convert older project formats**. If you have projects from before
MCL 5.00, open and re-save them once with an official 5.00+ build first,
that will upgrade them to the current format. Projects already saved on
5.00, 5.01, or 5.02 need no action.

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






















