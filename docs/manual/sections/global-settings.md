# Project and Configuration Menu

The Configuration menu is used for project management and global or per-project settings.

Open it from the Grid Page with:

```text
[Bank Group], then [Pattern/Song]
```

On MegaCommand and MegaCMD, **`Save | No`** + **`Load | Yes`** opens it too.

Use **[Up]** and **[Down]** to move through entries, **[Yes/Enter]** to select, and **[No/Exit]** to leave a menu.

## Top-Level Entries

The top-level menu is named `CONFIG`.

![config menu 1](../assets/images/config_menu_1.png)

| Entry | Function |
| --- | --- |
| `LOAD PROJECT` | Opens the project browser. |
| `MIDI` | Opens device, port, sync, routing, controller and program-change settings. |
| Device config entries | Dynamic entries for connected devices, such as Machinedrum, Monomachine, Analog Four, generic MIDI or TBD. |
| `SYSTEM` | Opens display, project-configuration, grid-encoder and `LOAD/SAVE` settings. |

## Project Browser

`LOAD PROJECT` opens the project browser. Projects can be grouped and nested in folders on the SD card.

![project browser](../assets/images/project_menu.png)

The project browser can show:

| Entry type | Meaning |
| --- | --- |
| `[ NEW PROJECT ]` | Create a new project in the current folder. |
| `..` | Move to the parent folder. |
| Folder | Open the folder. |
| Project | Load the project. |

The currently loaded project is marked in the browser.

The browser opens on the last project you loaded or tried to load. If a project fails to load, MCL keeps the current project and the browser stays open on the one that failed.

Browser shortcuts: **[Left]** jumps to the top of the current project list. **[Right]** returns to the project root and focuses the currently loaded project path.

## File Menu

Hold **[Global]** from the project browser to open the file menu.

![project file menu](../assets/images/project_file_menu.png)

| Entry | Function |
| --- | --- |
| `CANCEL` | Close the file menu without action. |
| `NEW DIR` | Create a folder in the current location. |
| `RENAME` | Rename the selected project or folder where allowed. |
| `MOVE` | Move the selected project or folder to another folder: the `DEST` view lists folders; open the destination and select `[ MOVE ]`. The loaded project, or a folder holding it, cannot be moved. |
| `CLONE` | Duplicate the selected project or folder. |
| `VERS` | Open project versions for the selected project. |
| `DELETE` | Delete the selected project or folder where allowed. |

The available actions depend on the selected entry and platform.

## Project Versions

The `VERS` action opens the project version browser. Versions are snapshots of the same project, useful before major edits or before converting an older project.

![project versions](../assets/images/project_versions.png)

Typical version actions:

| Action | Function |
| --- | --- |
| `BACKUP` | Create a new version snapshot, load that new version immediately, and return to the Grid Page. |
| Load version | Restore the selected project version and return to the Grid Page. |
| Delete version | Remove a non-active backup version. The base version cannot be deleted. |

## New Project

Creating a project opens a text-entry page. If a project or directory with the same name already exists, MCL reports an error instead of overwriting it.

![new project](../assets/images/new_project.png)

![charpane](../assets/images/charpane.png)

## Project Files

Projects are stored on the SD card with a project master file and grid files. Project and grid headers are versioned so older projects can be upgraded safely.

Back up projects before upgrading firmware. Supported older projects are upgraded when they are first loaded, and upgraded projects are not intended to be opened by older firmware.

### Projects From Older Versions

| Project from | Opening it in MCL 5.10 |
| --- | --- |
| MCL 5.00 to 5.02 | It opens directly and is upgraded as it opens. MCL 5.0x cannot open it afterwards. |
| Before MCL 5.00 | On TBD it is converted when it loads. On MegaCommand and MegaCMD the browser shows `UPGRADE PROJECT`: convert it with the project upgrader first. |

### Project Upgrader

The project upgrader is a separate firmware for MegaCommand and MegaCMD, `mcl_project_upgrade_1_0.hex`. It converts projects from before MCL 5.00.

1. Back up the SD card.
2. Install the upgrader.
3. At start-up it shows `PROJECT UPGRADE` and asks `SCAN FOR PROJECTS?`. **`Load | Yes`** starts the scan; **`Save | No`** stops.
4. The scan lists every project in the projects folder, and in the folders one level below it: `UPGRADE` (from before MCL 5.00), `UP TO DATE` (opens in MCL 5.10 as it is) or `CAN'T READ`. The scan changes nothing.
5. It asks `UPGRADE 9 PROJECTS?` (with the number found). Each conversion writes new grid files beside the old ones; when the card has room for only some of them, the question shows `ROOM FOR` and how many. **`Load | Yes`** converts them; **`Save | No`** stops with no project changed.
6. Each project's line counts up in percent, then shows `OK`, `NO ROOM` or `FAILED`. Do not turn the unit off while a project converts.
7. When the bottom line shows `NOW UPGRADE TO MCL 5.10`, upgrade to MCL 5.10 again.

Turn Encoder 2 to scroll back through the log on the screen. From the second **`Load | Yes`** on, the log is also written to `/Logs/upgrade.txt` on the card, and each run is added to the end of it.

Projects from MCL 5.00 to 5.02 are not touched: MCL 5.10 opens them directly. The upgrader does not change MCL's configuration.

Do not edit project files by hand. Use the project browser, file menu and version browser from MCL.

## Saving Projects

MCL writes to the current project's data files when you save slots or groups from the Save Page. After that, there is no separate full-project save step.
