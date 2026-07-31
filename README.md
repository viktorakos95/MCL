# MCL

A high-performance MIDI sequencer, controller and live performance system.
Designed to for use with the Machinedrum and other MIDI devices.

Visit the [releases page](https://github.com/jmamma/MCL/releases) for:
- Latest firmware binaries
- User documentation
- Installation instructions

## Custom Features (this fork)

This fork adds two extra features on top of official MCL. They're both opt-in
(off by default) — if you don't turn them on, the firmware behaves exactly
like upstream. Built with AI help (Claude), discussed with the original
author here: https://github.com/jmamma/MCL/pull/197

### Manual Step (CC-triggered step advance)

Lets a MIDI CC message manually advance the Machinedrum sequencer one step
per message, instead of the sequencer following the clock. Useful for
triggering steps from a pad, footswitch, envelope follower, or anything else
that can send a MIDI CC — the clock keeps running normally for everything
else (other gear, LFOs), only the MD step-advance is taken over.

**How to use:** SEQ menu, right under QUANT — `STEP MODE` (on/off), `STEP CC`
(which CC number), `STEP PORT` (MIDI2 or USB). Settings save per-project.

**Warnings:**
- Only listens on MIDI2 or USB — never MIDI1, on purpose, so it can't
  collide with the Machinedrum's own CC traffic on MIDI1.
- Fires on *every* message on the chosen CC, regardless of value — no
  debounce. If your CC source sends continuous/repeated values (e.g. an
  envelope follower), it can advance steps much faster than you intend.
- Only affects Machinedrum tracks.

### Live Step Repeat / Roll

Hold LEFT+RIGHT on the Mixer page, then hold a trig pad, to repeat that
track's sound at a chosen subdivision — for as long as you hold it — instead
of waiting for its next scheduled step. While LEFT+RIGHT are held, UP/DOWN
cycles the subdivision (1/4 through 1/64, including triplets). A card shows
the current rate while armed.

**How to use:** just the gesture above, on the Mixer page, with the Primary
device selected. Two related toggles live in the SYSTEM menu:
- `ROLL MUTE` (default YES) — whether rolling a muted track temporarily
  unmutes it for the roll, or stays silent.
- `ROLL TRIPLETS` (default YES) — whether triplet rates are included when
  cycling with UP/DOWN, or skipped (leaving only 1/4, 1/8, 1/16, 1/32, 1/64).

**Warnings:**
- LEFT and RIGHT already do something on their own on the Mixer page (they
  preview mute sets). Holding both together is a deliberate combo, not a
  conflict — a short grace window tells a solo tap apart from the start of
  the chord — but if you're not going for the roll, a very fast press of
  just one arrow could occasionally be misread as the start of the chord.
- Only works while the Mixer page is showing and the Primary device is
  selected — if you press REC or otherwise leave the page, the roll stops.
  It is not currently possible to record a roll into a pattern via
  live-record (this was attempted and removed — it didn't work reliably).
- Requires the sequencer clock to be running; the timing is locked to the
  actual clock position, not to whenever you happen to press the pad.

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






















