# System Settings

The System menu contains MCL-wide display and configuration-scope options.

Open it from:

```text
CONFIG > SYSTEM
```

## Menu Entries

| Entry | Values | Function |
| --- | --- | --- |
| `DISPLAY` | `INT`, `INT+EXT` | Enables the internal display only, or internal plus external display mirroring. |
| `PROJ CFG` | `OFF`, `ON` | Chooses whether supported configuration stored in the project is applied when that project loads. |
| `GRID ENCOD` | `--`, `PERF` | Reassigns the Grid Page encoders to performance-controller behavior when enabled. |
| `LOAD/SAVE` | `LEGACY`, `CLOSE`, `STAY` | Sets how the encoders and arrow keys work while Load or Save is open, and whether they close after a load or save. |

## Display

`DISPLAY` controls whether MCL mirrors the display to an external display endpoint where available.

Use `INT` for normal hardware use. Use `INT+EXT` only for an external display mirror, because mirroring can reduce GUI performance on constrained hardware.

## Project Configuration

`PROJ CFG` controls how project-stored configuration is applied.

| Value | Behavior |
| --- | --- |
| `OFF` | Keep the current global system/device configuration when loading a project. The project's Machinedrum `SAMPLEBANK` link is still restored. |
| `ON` | Apply supported configuration stored in the project when it loads. |

Project configuration includes device and MIDI-related settings that affect how the grid maps to connected instruments. Use `ON` when different projects require different device layouts.

After a major firmware upgrade, stored configuration may be reset to defaults on first boot. Re-check device layout, MIDI ports, sync, project-configuration mode and any project samplebank link before continuing work.

## Grid Encoder Mode

`GRID ENCOD` changes the Grid Page encoder behavior.

| Value | Behavior |
| --- | --- |
| `--` | Encoders keep their normal grid-navigation behavior. |
| `PERF` | Encoders act as performance controllers from the Grid Page. |

## Load/Save

`LOAD/SAVE` applies on MegaCommand, MegaCMD and TBD, where Load and Save open on the Grid Page.

| Value | Behavior |
| --- | --- |
| `LEGACY` | The default. While Load is open, Encoder 1 sets the load mode, Encoder 2 `LEN` (Queue mode) and Encoders 3 and 4 `QUANT`. The arrow keys close Load or Save and move the Grid Page cursor. Load and Save close after a load or save. |
| `CLOSE` | While Load is open, the encoders work as on the Grid Page, **[Up/Down]** move to another row and **[Left/Right]** scroll the columns shown. On Save the arrow keys close Save. Load and Save close after a load or save. |
| `STAY` | As `CLOSE`, and the arrow keys work the same way on Save. Load and Save stay open for the next load or save. |

In every setting, holding **`Load | Yes`** or **[Global]** edits the Load settings. See the Load Page.
