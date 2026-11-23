#include "GUI/Pages/SoundBrowserPage.h"
#include "ResourceManager.h"
#include "MCLGUI.h"
#include "../../../Drivers/MD/MD.h"
#include "../../../Drivers/MD/MDSound.h"

const char c_snd_root[] = "/Sounds";
const char *c_snd_suffix = ".snd";

void SoundBrowserPage::setup() {
  char path[64];
  SD.mkdir(mcl_sd.full_path(c_snd_root, path, sizeof(path)), true);
  FileBrowserPage::setup();
  _cd(mcl_sd.full_path(c_snd_root, path, sizeof(path)));
  position.reset();
}

void SoundBrowserPage::init() {
  // The browser options are shared by every file browser page: start from
  // the defaults, or the last page's settings stay (the project page selects
  // folders instead of opening them).
  FileBrowserPage::init();
  FileBrowserPage::selection_change = true;

  key_interface.off();

  folder_browser_options();
#ifdef MCL_HAS_FILE_MOVE
  // While a move waits for its destination, the base page has set up the
  // DEST view, which lists folders only.
  if (!move_destination_mode)
#endif
  {
    file_types.add(c_snd_suffix);
    strcpy_P(title, mclstr_title_sound);
    strcpy_P(str_save, mclstr_action_save_bracket);
  }
  query_filesystem();

  R.Clear();
  R.use_machine_names_short();
}

// ".." is shown only inside a folder, so the browser stays in /Sounds. It
// opens /Sounds and never goes above it, so a longer path is a folder.
bool SoundBrowserPage::can_show_parent_entry() const {
#ifdef __AVR__
  return show_parent && strlen(lwd) > sizeof(c_snd_root) - 1;
#else
  char path[64];
  return show_parent &&
         strlen(lwd) > strlen(mcl_sd.full_path(c_snd_root, path, sizeof(path)));
#endif
}

#ifdef PLATFORM_TBD
bool SoundBrowserPage::tbd_can_cd_up() const {
  return can_show_parent_entry();
}
#endif

void SoundBrowserPage::save_sound() {

  MDSound sound;
  char sound_name[8];
  uint8_t l = min((uint8_t)strlen(MD.kit.name), (uint8_t)4);
  memcpy(sound_name, MD.kit.name, l);
  const char *tmp = getMDMachineNameShort(MD.kit.get_model(MD.currentTrack), 2);
  if (tmp) {
    copyMachineNameShort(tmp, sound_name + l + 1);
  }
  sound_name[l+3] = '\0';
  sound_name[l] = '_';

  if (mcl_gui.wait_for_input(sound_name, "Sound Name", sizeof(sound_name) - 1)) {
    char temp_entry[FILE_ENTRY_SIZE];
    strcpy(temp_entry, sound_name);
    strcat(temp_entry, ".snd");
    sound.file.open(temp_entry, O_RDWR | O_CREAT);
    sound.fetch_sound(MD.currentTrack);
    sound.write_sound();
    sound.file.close();
    gfx.alert("File Saved", temp_entry);
  }
}

void SoundBrowserPage::load_sound() {

  if (file.isOpen()) {
    char temp_entry[FILE_ENTRY_SIZE];
    MDSound sound;
    file.getName(temp_entry, FILE_ENTRY_SIZE);
    file.close();
    DEBUG_PRINTLN(F("loading sound"));
    DEBUG_PRINTLN(temp_entry);
    if (!sound.file.open(temp_entry, O_READ)) {
      gfx.alert("Error", "Opening");
      return;
    }
    sound.read_sound();
    if (sound.id != SOUND_ID) {
      sound.file.close();
      gfx.alert("Error", "Not compatible");
      return;
    }
    sound.load_sound(MD.currentTrack);
    gfx.alert("Loaded", "Sound");
    sound.file.close();
  }
}



void SoundBrowserPage::on_new() {
#ifdef MCL_HAS_FILE_MOVE
  if (move_destination_mode) {
    move_to_current_folder();
    return;
  }
#endif
 save_sound();
 init();
}

void SoundBrowserPage::on_cancel() {
#ifdef MCL_HAS_FILE_MOVE
  if (move_destination_mode) {
    cancel_move_destination();
    return;
  }
#endif
  mcl.popPage();
}

void SoundBrowserPage::cleanup() {
#ifdef MCL_HAS_FILE_MOVE
  // The browser pages share the move state: a move not finished here must
  // not carry over to the next one.
  move_destination_mode = false;
#endif
}

void SoundBrowserPage::on_select(const char *__) {
  load_sound();
}

bool SoundBrowserPage::handleEvent(gui_event_t *event) {
  if (EVENT_BUTTON(event)) {
    // While a move waits for its destination, the base page's menu applies.
    if (EVENT_PRESSED(event, Buttons.BUTTON3) && show_filemenu &&
        !move_destination_mode) {
      // A new folder can be made from any row; delete, rename and move act
      // on a sound or folder, not on the save row or "..".
      char entry[FILE_ENTRY_SIZE];
      uint8_t entry_type;
      uint16_t disabled = FM_MASK(FM_DUPLICATE) | FM_MASK(FM_VERSIONS) |
                          FM_MASK(FM_RECVALL) | FM_MASK(FM_SENDALL);
#ifndef MCL_HAS_FILE_MOVE
      disabled |= FM_MASK(FM_MOVE);
#endif
      if (!get_selected_entry(entry, entry_type)) {
        disabled |= FM_MASK(FM_DELETE) | FM_MASK(FM_RENAME) | FM_MASK(FM_MOVE);
      }
      open_filemenu(disabled);
      return true;
    }

    if (EVENT_PRESSED(event, Buttons.BUTTON2)) {
      mcl.popPage();
      return true;
    }
  }
  return FileBrowserPage::handleEvent(event);
}

MCLEncoder soundbrowser_param1(0, 1, ENCODER_RES_SYS);
MCLEncoder soundbrowser_param2(0, 36, ENCODER_RES_SYS);
SoundBrowserPage sound_browser(&soundbrowser_param1, &soundbrowser_param2);
