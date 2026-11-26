# Machinedrum Configuration

The Machinedrum configuration page controls Machinedrum-specific project and transfer behavior.

![machinedrum menu](../assets/images/machinedrum_menu.png)

Open it from the dynamic device entry in the Configuration menu. When the Machinedrum is assigned as a grid device, the entry is named for the Machinedrum instead of the old fixed `DRIVER 1` or `DRIVER 2` label.

## Menu Entries

| Entry | Function |
| --- | --- |
| `IMPORT` | Import Machinedrum patterns into the MCL grid. |
| `RAM LINK` | Select `MONO` or `STEREO` RAM page behavior. |
| `NORMALIZE` | Enable or disable automatic MD level normalization when saving tracks. |
| `SAMPLEBANK` | Link the project to a fixed Machinedrum +Drive sample bank, or disable linking with `OFF`. |

## Import

`IMPORT` opens the Machinedrum pattern import page.

![machinedrum import](../assets/images/machinedrum_import.png)

| Field | Function |
| --- | --- |
| `SRC` | Starting source pattern on the Machinedrum. |
| `DEST` | Destination row on the MCL grid. |
| `COUNT` | Number of patterns to import. |
| `RUN` | Start the import. |

Imports can be used to bring existing Machinedrum material into the MCL project grid. Older project data is upgraded on load where supported.

Patterns from MDX import with each track's own length and speed, as the Machinedrum plays them. This includes patterns with an `INF` or long master length, and patterns whose ALL length and speed differ from the master length. Each imported track loops on its own.

From a Machinedrum running MDX X.20 or later, locks on steps without a trig are imported too. From older firmware, a lock is imported only where its step has a trig.

## RAM Link

`RAM LINK` controls whether the RAM pages act as `MONO` or `STEREO` during recording and playback.

This option moved from the old Page Setup area into the Machinedrum configuration page.

## Normalize

When `NORMALIZE` is set to `AUTO`, saved MD tracks have their track `LEV` raised to 127 while related volume parameters and locks are adjusted to preserve the perceived loudness.

This makes the track level control a predictable maximum-level control during performance. While the sequencer is running, MCL does not transmit the track `LEV` parameter continuously, so the performer can still fade tracks manually from the Machinedrum.

Use `OFF` if saved MD track levels should remain untouched.

## Samplebank

`SAMPLEBANK` links the current project to a Machinedrum +Drive sample bank.

| Value | Behavior |
| --- | --- |
| `OFF` | Do not load a fixed sample bank with the project. |
| `1..128` | Load the selected +Drive sample bank when the project is loaded. |

The linked sample bank is stored with the project configuration.

MCL sends the sample-bank load when the project is loaded and Machinedrum is the primary grid device. The connected Machinedrum must support +Drive sample-bank switching.

## Machinedrum MIDI Input

Machinedrum chromatic, trig and polyphonic input behavior is configured from:

```text
CONFIG > MIDI > CONTROLLER > INPUT
```

## MDX X.20 Or Later

With a Machinedrum running MDX X.20 or later, MCL also:

- reads the names of the Machinedrum's USR machines after connecting, and shows them in the grid and on the sequencer pages. Until the names arrive, the grid shows `??` for a USR machine.
- uses the USR machines' parameter labels as parameter names.
- keeps each track's parameters 24 to 37, second LFO and output routing when it sends a kit, so a kit sent from MCL does not reset them.
- reads edits made on the Machinedrum to those settings as they happen.
