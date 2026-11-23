# GUI


![megacommand gui](../assets/images/megacommand_gui.png)


## Function Buttons

From the Grid Page the MC's function buttons perform the following actions:


- **`Save | No`**: Enters the Save Page.
- **`Load | Yes`**: Enters the Load page.
- **`Page`**: Enters the PageSelect page.
- **`Shift | Menu`**: Opens the slot Menu.


Combined Button Presses:


- **`Save | No` + `Load | Yes`**: Opens the MCL Configuration menu.

While Load or Save is open:

- **`Shift | Menu`**: Hold to open group selection; release to load or save the selected groups.
- **`Save | No`**: Closes Load or Save.
- **`Load | Yes`**: On Load, tap to close Load, or hold to edit the Load panel: Encoder 2 moves the cursor and Encoder 1 changes the value under it. On Save, closes Save.


## Encoder Buttons

Encoder buttons can increase rotation speed for coarse edits. The exact multiplier depends on the page and control.

On MD and TBD-16, arrow keys can select and edit visible MCL page parameters on most pages.


## Machinedrum GUI: Enhanced Mode


![machinedrum gui](../assets/images/machinedrum_gui.png)


The MDX firmware introduces a third editing mode beyond Classic and Extended termed **Enhanced Mode**.


Enhanced mode is activated automatically when the MD is connected to the MegaCommand.


When in Enhanced mode, both Classic and Extended LEDs will be lit.

With the sequencer stopped, hold down the MD's **[Classic/Extended]** key for 1.5 seconds to switch between Enhanced and Classic/Extended modes.


![enhanced mode](../assets/images/enhanced_mode.png)


Enhanced mode disables editing access to the MD's internal sequencer and enables the Machinedrum's GUI to be fully integrated with MCL.


## MD + MCL command summary


**General**


- **[Classic/Extended]** toggle between Classic, Extended and Enhanced Modes. (When in Enhanced mode, the sequencer must be stopped to switch modes.)
- **[Function] + [Bank Group]** toggle Bank Group selection.
- **[Rec]** Enter Step edit mode.
- **[Global]** Open the current Page's shift menu.
- **[Global] + [Up/Down/Left/Right]** Navigate the shift menu.
- **[Bank Group]** Hold to Open the Page Select menu.
- **[Bank Group] + [Trig]** MCL Page Select.
- **[Bank Group] + [Pattern/Song]** MCL Config Menu.
- **[Bank Group] + [No/Exit]** Return to Grid page.
- **[Function] + [Encoder]** Simultaneously modify parameters across all tracks.
- **[Function] + [Classic/Extended]** Simultaneously reset parameters to last load value across all tracks.


**Fill (MDX X.20 or later)**


- **[Scale] + [Up]** hold fill, or end a latched fill.
- **[Scale] + [Up] + [Down]** latch fill.
- **[Scale] + [Down]** while playing: fill for one cycle of the longest Machinedrum track. Again to cancel it.
- **[Bank] + [Scale]** open the Machinedrum's FILL view to choose the tracks a fill plays on.
- **[Scale]** alone acts when it is released.


**Loading**


- **[Bank] + [Trig]** loads slots from the selected row according to Group selection.
- **[Bank] + [Multiple Trigs]** creates a chain of slots from selected rows according to Group selection.


**Grid Page**


- **[Up/Down/Left/Right]** position grid cursor.
- **[Function] + [Up/Down/Left/Right]** grid cursor position fast travel.
- **[Scale]** Toggle active grid X or Y.
- **[Function] + [Clear/Copy/Paste]** Clear/Copy/Paste the MD tracks, EXT MIDI tracks and Machinedrum master FX currently loaded in the sequencer.
- Hold **[No/Exit]** key to open Slot Menu:
  - **[Bank A,B,C]** keys can be used to select load mode: MAN, AUT, QUE.
  - **[Up/Down/Left/Right]** to select multiple slots in the grid.
  - **[Clear/Copy/Paste]** to clear/copy/paste selected slot(s).
  - **[Yes/Enter]** load selected slots. Slots are loaded according to the current load MODE setting. If more than one row is selected, the MODE is automatically set to QUE, and the loaded slots are added to each column's playback queue.


**[Yes/Enter]** opens **Load Page**


- Hold **[Yes/Enter]** to open slot Group Select. Release **[Yes/Enter]** to load by group. Group selection is editable via **[Trig]** keys 1-5.
- **[Trig]** keys are used to select and load sequencer tracks from slots of the current row.
- **[Up/Down]** move to another row and **[Left/Right]** scroll the columns shown, with `CONFIG > SYSTEM > LOAD/SAVE` at `CLOSE` or `STAY`. At `LEGACY` they close Load and move the grid cursor.
- **[Bank A,B,C]** keys can be used to quickly select the load mode: MAN, AUT, QUE.
- **[Bank D]** (the Slide key): tap to turn FADE on or off, hold for the FADE screen.
- **[Function]** in Manual mode: press a trig for the destination slot, then select and release the source slots to load them there.
- Hold **[Global]** to edit the Load panel: the load mode, QUANT, then FADE and FLEN (LEN in Queue mode). **[Up/Down]** move the cursor and **[Left/Right]** change the value.
- **[No/Exit]** closes the Load Page. Whether it closes after a load is set by `CONFIG > SYSTEM > LOAD/SAVE`.


**[Function] + [Yes/Enter]** opens **Save Page**


- Hold **[Yes/Enter]** to open slot Group Select. Release **[Yes/Enter]** to save by group. Group selection editable via **[Trig]** keys 1-5.
- **[Trig]** keys are used to select and save individual sequencer tracks to slots of the current row. If none are selected entire row (pattern) will be saved.


**Mixer Page**


- **[Classic/Extended]** Open/Close the Mixer Page while the sequencer is running.
- **[Classic/Extended]** + **[Trig]** Mute/Unmute selected track(s).
- **[Scale]** Switch Grid X Y tracks.
- **[Trig]** Select track.
- **[Trigs] + [Encoder]** Simultaneously modify parameters across selected tracks.
- **[Trigs] + [Classic/Extended]** Simultaneously reset parameters to last load value across selected tracks.
- **[Trig]** + **[Yes/Enter]** Mute/Unmute selected track(s).
- **[Trig]** + **[No/Exit]** Solo selected track(s).
- **[Function]** + **[Yes/Enter]** Flip active mutes.
- **[Up/Down/Left/Right]** Preview and edit Performance Sets.
- **[Up/Down/Left/Right]** + **[Yes/Enter]** Load selected Performance Set.
- **[Up/Down/Left/Right]** + **[Mute/Bank A]** Disable active device's mutes from selected Performance Set.
- **[Up/Down/Left/Right]** + **[Accent/Bank B]** Set selected Performance Set to load automatically with PF Slot in Grid Y.
- **`Encoders1-4`** Performance Controllers (A,B,C,D) scene morphs.
- Hold a Perf encoder button + **[Global]** / **`Shift | Menu`** to clear both assigned scenes for that controller.
- Hold a Perf encoder button + **`Load | Yes`** to autofill that controller's right scene from changed kit parameters.
- **[Up/Down/Left/Right] + [No/Exit] + `Encoders1-4`** Set performance controller lock to mute set.
- **[Function] + `Encoders1-4`** Hard pan performance controller left or right.
- **[Trig]** + **[Global]** Sequencer mute record for selected tracks.
- **[Trig]** + **[Kit]** Clear sequencer mute record for selected tracks.
- **[Global]** + **[Kit]** Clear all active sequencer mute recordings.
- **[No/Exit] + [Left]** Toggle DELAY pages.
- **[No/Exit] + [Down]** Toggle REVERB pages.


**Performance Page**


- **[Global]** Open Performance menu.
- **[Global] + [Trig1-4]** Select active controller 1-4.
- **[Left] + [Trig1-8]** Set active controller LEFT scene 1-8.
- **[Right] + [Trig1-8]** Set active controller RIGHT scene 1-8.
- **[Trig] + [MD Encoder] Or Ext Midi Controller** Assign lock to scene.
- **[Trig] + [Clear/Copy/Paste]** Clear, Copy, Paste scenes (repeat to UNDO).
- **[Trig] + [Yes/Enter]** Scene preview.
- **[Trig] + [Up/Down]** View and edit 16 active scene locks.


**Route Page:**


- **[Trigs]** Toggle between Main output (--) and selected Route output on chosen tracks.
- **`Encoder 1`** Set Route output (C/D/E/F).
- **`Encoder 2`** Set quantize amount.


**Step Editor**


- **[Rec]** enter/exit MCL step editing.
- **[Rec] + [Play]** enter realtime record mode (from any page).
- **[Function/Scale/Trig] + [Clear/Copy/Paste]** clear/copy/paste for track/page/step.
- **[Step] + [Left/Right]** microtiming.
- **[Step] + [Up/Down]** conditionals.
- With MDX X.20 or later, the first arrow press on a held step opens the Machinedrum's microtiming popup; later presses edit. **[Page]** sets the condition mode.
- **[Step] + [No/Exit]** Mute/unmute step.
- **[Function] + [Left/Right]** shift track sequence left or right.
- **[Function] + [Up]** reverse track sequence.
- Hold **[Global]** to open the Track Menu for `SPEED` and `LENGTH`.
- **[Function] + [Bank B]** edit Step Mute toggle.
- **[Function] + [Bank C]** edit Swing toggle.
- **[Function] + [Bank D]** edit Slide toggle.


**LFO Page**


- Use the LFO setup page to switch LFO mode (`FRE`, `TRG`, `1SH`, `TRK`).
- **[Trigs]** Enter LFO resets (`TRG` and `1SH` modes only).
- **[Scale]** Move between trigger-mask pages in `TRG` and `1SH` modes.
- **`Load | Yes`** cycles LFO subpages.
- **[Yes/Enter]** LFO On/Off.
- **`Encoder 1`** LFO shape.


**Pianoroll Editor**


- **[Yes/Enter]** Add or remove notes/cc.
- **[Left/Right]** Move cursor along time axis.
- **[Yes/Enter] + [Left/Right]** Nudge cursor along time axis (fine control).
- **[No/Exit] + [Left/Right]** Adjust cursor width.
- **[Yes/Enter] + [No/Exit] + [Left/Right]** Nudge cursor width (fine control).
- **[Up/Down]** Move cursor along note axis.
- **[Yes/Enter] + [No/Exit] + [Up/Down]** Zoom in and out.
- **[Function] + [Up/Down/Left/Right]** Cursor fast travel.
- **[Clear/Copy/Paste]** Clear/copy/paste for track.
- **[Scale]** Toggle sequencer page.
- Hold **[Global]** to open the Track Menu for `SPEED` and `LENGTH`.
- **[Trigs]** Position the cursor at step intervals relative to the current page.
- **[Global]** hold to open Track configuration menu.
- **[Global] + [Trigs]** Access to EXT track select (1-6) and EXT track mutes (9-14).


**Chromatic Page**


- **[Global]** Toggle shift menu for access to Arpeggiator, Poly tracks etc.
- **[Function]** + **[Clear/Copy/Paste]** Clear/copy/paste track.
- **[Left/Right]** Move focus between `OCT`, `DET`, `LEN`/`PLEN` and `SCA`.
- **[Up/Down]** Adjust the focused parameter. To transpose the scale root, hold **[Global]** and use Track Menu `KEY`.
- **[Scale]** Switch input device MD/Midi.
- **`Encoder 1`** Octave.
- **`Encoder 2`** Detune.
- **`Encoder 3`** Track length.
- **`Encoder 4`** Scale type.


**Sample Manager Page**


- **[Up/Down]** Navigate menu.
- **[Yes/Enter]** Enter a folder, start receive, or send the selected sample to a Machinedrum sample slot.
- **[No/Exit]** Exit/back/cancel.
- **[Global]** File options menu (New directory/rename/delete).


**WAV Designer**


- **[Global] + [Left/Right]** Shift menu select OSC1-3 and Mixer pages.
- **[Left/Right]** Pitch, note increments.
- **[Up/Down]** Fine tune.
- **[No/Exit]** Display corresponding frequency.
- **`Encoder 3`** Pulsewidth for TRI, PUL and SAW.
- **[Trigs] + `Encoder 4`** SIN add overtones/USR modify sample values.
- Mixer Page:
  - **`Encoder 1`** OSC1 Level.
  - **`Encoder 2`** OSC2 Level.
  - **`Encoder 3`** OSC3 Level.
  - **[Global]** Oscillator mixer page menu for TRANSFER.


**FX Delay Page**


- **[No/Exit] + [Left]** Select Delay/Echo or switch its parameter group.
- **`Encoders 1-4`** FX parameters.


**FX Reverb Page**


- **[No/Exit] + [Down]** Select Reverb or switch its parameter group.
- **`Encoders 1-4`** FX parameters.


**RAM Machines**


- MD **[Yes]** / **`Save | No`** Queue RAM recording.
- MD **[No]** / **`Load | Yes`** Queue RAM playback or apply normal slicing.
- **[Global]** Apply dice slicing.
- **`Encoder 1`** Choose recording source.
- **`Encoder 2`** Dice mode.
- **`Encoder 3`** Slice amount.
- **`Encoder 4`** Record/playback length in sequencer steps.
