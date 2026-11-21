# Load Page

The Load Page loads grid slots into the active tracks or destination slots.

Open it from the Grid Page with:

```text
[Yes/Enter]
```

![load from a](../assets/images/load_from_a.png)

On MegaCommand, MegaCMD and TBD, Load keeps the Grid Page visible: the left column shows the Load settings and the grid shows the selected slots.

Load stays open after a load, ready for the next one. Close it with **[No/Exit]**.

## Load Behavior

Loading a slot can restore:

- sequence data
- sound or device state, if `SOUND` is enabled for the slot
- length, loop and jump settings
- page-specific state for performance, route, auxiliary and tempo groups

Use the Slot Menu `SOUND` option to make a slot load sequence-only.

## Controls

| Control | Assignment |
| --- | --- |
| **[Global]**, **`Load | Yes`** | Hold to edit the Load settings. |
| Encoders | As on the Grid Page. With **[Global]** or **`Load | Yes`** held, they edit the Load settings. |
| **[Trig]** keys | Select slots to load. |
| **[Up/Down]** | Move to another row. |
| **[Left/Right]** | Scroll the columns shown. |
| **[Scale]** | Toggle Grid X / Grid Y while selecting. |
| **[Function]** | In Manual mode, select a destination offset. |
| **[Yes/Enter]**, **`Shift | Menu`** | Hold to open group selection; release to load selected groups. |
| **[No/Exit]**, **`Save | No`**, a tap of **`Load | Yes`** | Close Load. |
| **[Bank A/B/C]** | Shortcut to Manual, Auto or Queue mode. |
| **[Bank D]** (the Slide key) | Turn `FADE` on or off. |
| **[Play]**, **[Stop]** | Start and stop the sequencer. Load stays open. |

## Load Settings

Hold **[Global]** on the Machinedrum, or the **`Load | Yes`** button, to edit the Load settings. While it is held, one of the rows under `LOAD` is highlighted: `QUANT`, `LEN` or `FADE`, or the load mode.

- Encoder 2 or **[Up/Down]** choose a row.
- Encoder 1 or **[Left/Right]** change it.
- On `FADE`, turning steps through `-`, `ON`, `IN` and `OUT`. Hold Encoder 1's button while turning to set the fade length.

While it is released, the encoders work as on the Grid Page.

## Fade

`FADE` fades the level of Machinedrum tracks when their slots load. It applies in Manual and Auto mode. Queue loads do not fade.

Turn it on with **[Bank D]** (the Slide key) or in the Load settings. The left column shows `FADE -` when it is off and `FADE ON` when it is on.

With `FADE ON`, each track fades by what it was playing:

| The track was | You load | Result |
| --- | --- | --- |
| Playing | A different slot | The playing slot fades out, the new slot switches in, then fades in. |
| Playing | The slot it is playing | The track fades out and stays silent. |
| Silent after a fade out, or not loaded yet | Any slot | The slot switches in and fades in. |

- The fade lasts the `QUANT` length, unless a fade length is set: 1 to 1024 steps, shown in place of `ON`. With `QUANT` at `-`, it lasts the slot's own length.
- When the playing slot fades out first, the new slot switches in one fade length after the load point.
- `FADE IN` and `FADE OUT`, set in the Load settings, make every loaded track fade in or fade out.
- A load made while the sequencer is stopped fades when **[Play]** is pressed. A track that will fade in is silent until then.
- A track that a fade left silent gets its level back the next time a slot is loaded on it.
- A fade does not change the track's saved `LEV`.

## Loading Individual Slots

1. Open Load.
2. Select one or more slots with the **[Trig]** keys.
3. Release the selection to queue or perform the load according to the active load mode.

The visible grid determines which grid the trig keys target. Use **[Scale]** to switch grids while building a selection.

## Group Load

Hold **[Yes/Enter]**, or the **`Shift | Menu`** button, from Load to open `LOAD GROUPS`.

![group select page](../assets/images/group_select_page.png)

| Group | Loads |
| --- | --- |
| 1 | Primary device group. |
| 2 | Secondary device group. |
| 3 | Performance state group. |
| 4 | Auxiliary or route state group. |
| 5 | Tempo/clock state group. |

Use **[Trig 1-5]** to toggle the desired groups, then release **[Yes/Enter]** to load them from the current row.

Group selection is also used by bank/trig row loading and program-change row loading.

## Load Modes

| Mode | Behavior |
| --- | --- |
| `MAN` | Load the selected slots at the next transition interval. Existing queues for matching slots are cleared. |
| `AUT` | Load the selected slots, then follow each slot's `LOOP` and `JUMP` settings when its loop count is reached. |
| `QUE` | Add the selected row or slot to that column's queue. Queued entries repeat in order. |

Each slot/column has its own chain state. This allows one track to follow an automatic chain, another to play a queued set of rows, and another to remain static.

## Queue Length Override

In Queue mode, the `LEN` row sets a length override.

| Value | Behavior |
| --- | --- |
| `-` | Use the queued slot's stored length. |
| 2-64 | Override the queued length in steps. Encoder 1 steps through powers of two; holding its button while turning allows intermediate values such as `3`, `5` or `6`. |

This is useful when a short track should loop for a longer phrase, or when a group of slots should advance together despite having different stored lengths.

## Quantization

Load quantization sets the transition interval for the selected load.

| Value | Meaning |
| --- | --- |
| `-` | Load at the destination track's next transition point, based on its current length and speed. |
| 2-64 | Load at the next matching step interval. Encoder 1 steps through powers of two; holding its button while turning allows intermediate values. |

Use quantization to keep manual, auto and queue loads aligned with the phrase length.

## Destination Offset

In Manual mode, hold **[Function]** from Load to choose a destination offset.

![load destination](../assets/images/load_destination.png)

1. Hold **[Function]**.
2. Press a **[Trig]** key to choose the destination slot.
3. Release the selection to load the selected source slot or range into the chosen destination area.

Destination offset is ignored outside Manual mode.

## Bank And Trig Row Loading

From the Grid Page, **[Bank]** + **[Trig]** loads rows by bank position.

| Gesture | Result |
| --- | --- |
| First row selected | Jump to or load that row. It uses Auto only if Auto was already active; otherwise it uses Manual. |
| Additional rows selected | Queue the selected rows as a row chain. |

After the row selection is applied, MCL restores the load mode that was active before the bank/trig gesture.

## Track Levels During Load

For Machinedrum tracks, MCL does not continuously transmit track `LEV` while the sequencer is running. This lets the performer use the Machinedrum level control for live fades during and after slot loads.

Use `CONFIG > MACHINEDRUM > NORMALIZE` to keep saved track loudness predictable when loading between slots.

`FADE` can fade the track levels for you when slots load. See Fade above.
