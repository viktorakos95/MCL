#include "GUI/Pages/Project/LoadProjectPage.h"
#include "GUI/MCLGfx.h"
#include "MCLGUI.h"
#include "MCLSd.h"
#include "MCLSysConfig.h"
#include "Project.h"
#include "Devices/DevicePanelRef.h"
#include "KeyInterface.h"
#include "GUI/MCLMenus.h"

// The name of the last project loaded or tried, so the page opens on it
// again, including straight after a failed load.
static char last_project[PRJ_NAME_LEN + 1];

void LoadProjectPage::init() {

  DEBUG_PRINTLN(F("load project page init"));
  DEBUG_PRINT_FN();
  DevicePanelRef::set_primary_key_repeat(1);
  FileBrowserPage::init();

#ifdef MCL_HAS_FILE_MOVE
  if (!move_destination_mode) {
#endif
    folder_browser_options();
    // A project is a folder: [Yes] selects it.
    select_dirs = true;
    show_copy = true;
    show_versions = false;
    strcpy(title, "PROJECT");
    strcpy(str_save, "[ NEW PROJECT ]");
    focus_current_project();
#ifdef MCL_HAS_FILE_MOVE
  }
#endif
  query_filesystem();
}

void LoadProjectPage::cleanup() {
  key_interface.ignoreNextEventClear(MDX_KEY_YES);
  key_interface.ignoreNextEventClear(MDX_KEY_NO);
}

void LoadProjectPage::setup() {
  FileBrowserPage::setup();
#ifndef __AVR__
  char path[64];
  strcpy(lwd, mcl_sd.full_path(PRJ_DIR, path, sizeof(path)));
#else
  strcpy(lwd, PRJ_DIR);
#endif
  position.reset();
}

void LoadProjectPage::on_select(const char *entry) {
  DEBUG_PRINT_FN();
  DEBUG_DUMP(entry);

#ifdef MCL_HAS_FILE_MOVE
  if (move_destination_mode) { return; }
#endif

  file.close();
  if (!is_project_dir(entry)) {
    _cd(entry);
    return;
  }

  char proj_filename[PRJ_PATH_LEN];
  if (!build_project_path(entry, proj_filename, sizeof(proj_filename))) {
    gfx.alert_error("BAD PATH");
    return;
  }
  strncpy(last_project, entry, PRJ_NAME_LEN);
  uint8_t count = 2;
  while (count--) {
    if (proj.load_project(proj_filename)) {
      if (count == 0) {
        // The current project is back after a failed load: stay here, on
        // the project that failed, ready for the next one, once a message
        // box has had its time (the list would redraw its highlight).
        while (oled_display.textbox_enabled) {
          oled_display.display();
        }
        init();
        return;
      }
      DEBUG_PRINTLN(F("loaded, setting grid"));
      mcl.setPage(GRID_PAGE);
      return;
    } else {
#ifndef MCL_HAS_PROJECT_CONVERSION
      if (proj.version == PROJ_MIN_READABLE_VERSION) {
        // A project from before MCL 5: this firmware cannot convert it. The
        // list's highlighted row (draw_filebrowser) is blanked first; its
        // white bar showed around the box.
        oled_display.fillRect(40, 2 + 8 * cur_row, MENU_WIDTH, 7, BLACK);
        oled_display.textbox_P(mclstr_upgrade, mclstr_project);
        oled_display.display();
      } else
#endif
      {
        gfx.alert("Error", "Not compatible");
      }
      strncpy(proj_filename, mcl_cfg.project, sizeof(proj_filename) - 1);
      proj_filename[sizeof(proj_filename) - 1] = '\0';
    }
  }
}

void LoadProjectPage::on_delete(const char *entry) {
  char project_path[PRJ_PATH_LEN];
  if (build_project_path(entry, project_path, sizeof(project_path)) &&
      path_starts_with_dir(mcl_cfg.project, project_path)) {
    gfx.alert_error("CURRENT PROJECT");
    return;
  }
  rm_dir(entry);
}

void LoadProjectPage::on_rename(const char *from, const char *to) {
  /*
  char to[f_len] = {0};
  strncpy(to,to_,f_len);
  char from[f_len] = {0};
  strncpy(from,from_,f_len);
*/
  DEBUG_PRINTLN(F("on rename"));
  bool project_dir = is_project_dir(from);
  char from_project_path[PRJ_PATH_LEN];
  if (!build_project_path(from, from_project_path, sizeof(from_project_path))) {
    gfx.alert_error("BAD PATH");
    return;
  }

  if (!project_dir) {
    if (path_starts_with_dir(mcl_cfg.project, from_project_path)) {
      gfx.alert_error("CURRENT PROJECT");
      return;
    }
    FileBrowserPage::on_rename(from, to);
    return;
  }

  char to_project_path[PRJ_PATH_LEN];
  if (!build_project_path(to, to_project_path, sizeof(to_project_path))) {
    gfx.alert_error("BAD PATH");
    return;
  }

  bool reload_current = false;

  if (!SD.chdir(from)) {
    goto error;
  }

  if (strcmp(mcl_cfg.project, from_project_path) == 0) {
    DEBUG_PRINTLN(F("reload current"));
    reload_current = true;
  }

  if (!proj.rename_project_files(from, to)) {
    goto error;
  }
  SD.chdir(lwd);
  if (SD.rename(from, to)) {
    if (reload_current) {
      proj.load_project(to_project_path);
    }
    gfx.alert_success("Project renamed.");
    return;
  }
error:
  SD.chdir(lwd);
  DEBUG_PRINTLN(F("error"));
  gfx.alert_error("Not renamed.");
}

void LoadProjectPage::on_copy(const char *from, const char *to) {
  if (strcmp(from, "..") == 0) {
    return;
  }

  bool ok = false;
#ifdef __AVR__
  if (is_project_dir(from)) {
    char from_project_path[PRJ_PATH_LEN];
    char to_project_path[PRJ_PATH_LEN];
    ok = build_project_path(from, from_project_path,
                            sizeof(from_project_path)) &&
         build_project_path(to, to_project_path, sizeof(to_project_path)) &&
         proj.copy_project(from_project_path, to_project_path);
  }
#else
  file.open(from, O_READ);
  bool dir = file.isDirectory();
  file.close();

  if (dir) {
    if (is_project_dir(from)) {
      char from_project_path[PRJ_PATH_LEN];
      char to_project_path[PRJ_PATH_LEN];
      ok = build_project_path(from, from_project_path,
                              sizeof(from_project_path)) &&
           build_project_path(to, to_project_path, sizeof(to_project_path)) &&
           proj.copy_project(from_project_path, to_project_path);
    } else {
      mcl_gui.draw_progress("CLONE", 0, 64);
      ok = mcl_sd.copy_dir(from, to, 0, 64, 64);
    }
  }
  else {
    mcl_gui.draw_progress("CLONE", 0, 64);
    ok = mcl_sd.copy_file(from, to, 0, 64, 64);
  }
#endif

  if (ok) {
    gfx.alert_success("Cloned.");
  } else {
    gfx.alert_error("Not cloned.");
  }
}

bool LoadProjectPage::handleEvent(gui_event_t *event) {
  if (!filemenu_active && EVENT_CMD(event) &&
      event->mask == EVENT_BUTTON_PRESSED) {
    switch (event->source) {
    case MDX_KEY_LEFT:
      set_cursor(0, 0);
      selection_change = true;
      return true;
    case MDX_KEY_RIGHT:
#ifdef MCL_HAS_FILE_MOVE
      if (!move_destination_mode) {
#endif
        setup();
        init();
        encoders[1]->old = encoders[1]->cur;
#ifdef MCL_HAS_FILE_MOVE
      }
#endif
      return true;
    }
  }

#ifdef MCL_HAS_FILE_MOVE
  if (move_destination_mode) {
    return FileBrowserPage::handleEvent(event);
  }
#endif

  if (EVENT_BUTTON(event) && EVENT_PRESSED(event, Buttons.BUTTON3) &&
      show_filemenu) {
    char entry[FILE_ENTRY_SIZE];
    uint8_t entry_type = FILE_TYPE;
    bool regular_entry = get_selected_entry(entry, entry_type);
    if (regular_entry) {
      entry_type =
          resolve_entry_type(encoders[1]->getValue(), entry, entry_type);
    }
    bool project_entry = regular_entry && entry_type == FILE_TYPE;
    uint16_t disabled = FM_MASK(FM_RECVALL) | FM_MASK(FM_SENDALL);
    if (!show_new_folder) {
      disabled |= FM_MASK(FM_NEW_FOLDER);
    }
#ifndef MCL_HAS_FILE_MOVE
    disabled |= FM_MASK(FM_MOVE);
#endif
#ifndef MCL_HAS_PROJECT_BACKUP
    disabled |= FM_MASK(FM_VERSIONS);
#endif
    if (!regular_entry) {
      disabled |= FM_MASK(FM_DELETE) | FM_MASK(FM_RENAME) | FM_MASK(FM_MOVE);
    }
#ifdef __AVR__
    if (!project_entry) {
      disabled |= FM_MASK(FM_DUPLICATE) | FM_MASK(FM_VERSIONS);
    }
#else
    if (!regular_entry) {
      disabled |= FM_MASK(FM_DUPLICATE);
    }
#ifdef MCL_HAS_PROJECT_BACKUP
    if (!project_entry) {
      disabled |= FM_MASK(FM_VERSIONS);
    }
#endif
#endif
    open_filemenu(disabled);
    return true;
  }
  return FileBrowserPage::handleEvent(event);
}

bool LoadProjectPage::_handle_filemenu() {
  uint8_t item = file_menu_page.menu.get_item_index(file_menu_encoder.cur);
#ifdef MCL_HAS_FILE_MOVE
  if (move_destination_mode) {
    return FileBrowserPage::_handle_filemenu();
  }
#endif

  if (item == FM_DELETE
#ifdef MCL_HAS_PROJECT_BACKUP
      || item == FM_VERSIONS
#endif
#ifdef MCL_HAS_FILE_MOVE
      || item == FM_MOVE
#endif
  ) {
    char entry[FILE_ENTRY_SIZE];
    uint8_t entry_type = FILE_TYPE;
    uint16_t entry_idx = encoders[1]->getValue();
    get_entry(entry_idx, entry, entry_type);

    if (item == FM_DELETE) {
      char message[32];
      build_delete_message(message, sizeof(message), entry);
      if (mcl_gui.wait_for_confirm(entry_type == DIR_TYPE ? "DEL FOLDER"
                                                          : "DEL PROJECT",
                                   message)) {
        on_delete(entry);
      }
      return true;
    }

#ifdef MCL_HAS_PROJECT_BACKUP
    if (item == FM_VERSIONS) {
      char project_path[PRJ_PATH_LEN];
      if (is_project_dir(entry) &&
          build_project_path(entry, project_path, sizeof(project_path))) {
        project_version_page.set_project(project_path);
        mcl.pushPage(PROJECT_VERSION_PAGE);
      }
      return false;
    }
#endif

#ifdef MCL_HAS_FILE_MOVE
    enter_project_move_destination(entry);
    return false;
#endif
  }
  return FileBrowserPage::_handle_filemenu();
}

#ifdef MCL_HAS_FILE_MOVE
// The move itself is the file browser's, on full card paths; the project
// page only refuses to move the current project or a folder holding it.
bool LoadProjectPage::enter_project_move_destination(const char *entry) {
  char project_path[PRJ_PATH_LEN];
  if (!build_project_path(entry, project_path, sizeof(project_path))) {
    gfx.alert_error("BAD PATH");
    return false;
  }
  if (path_starts_with_dir(mcl_cfg.project, project_path)) {
    gfx.alert_error("CURRENT PROJECT");
    return false;
  }

  return enter_move_destination(entry);
}
#endif

void LoadProjectPage::on_new() {
  file.close();
#ifdef MCL_HAS_FILE_MOVE
  if (move_destination_mode) {
    move_to_current_folder();
    return;
  }
#endif
  const char *parent = nullptr;
  if (!current_project_parent(&parent)) {
    gfx.alert_error("BAD PATH");
    return;
  }
  proj.new_project_prompt(parent);
}

void LoadProjectPage::on_cancel() {
#ifdef MCL_HAS_FILE_MOVE
  if (move_destination_mode) {
    cancel_move_destination();
    return;
  }
#endif
  mcl.popPage();
}

bool LoadProjectPage::current_project_parent(const char **parent) const {
#ifdef __AVR__
  const char *root_path = PRJ_DIR;
  constexpr size_t root_len = sizeof(PRJ_DIR) - 1;
#else
  char root[64];
  const char *root_path = mcl_sd.full_path(PRJ_DIR, root, sizeof(root));
  size_t root_len = strlen(root_path);
#endif

  if (strncmp(lwd, root_path, root_len) != 0) {
    return false;
  }
  if (lwd[root_len] == '\0') {
    *parent = "";
    return true;
  }
  if (lwd[root_len] == '/') {
    *parent = lwd + root_len + 1;
    return true;
  }
  return false;
}

bool LoadProjectPage::can_show_parent_entry() const {
  const char *parent = nullptr;
  return show_parent && current_project_parent(&parent) && parent[0] != '\0';
}

#ifdef PLATFORM_TBD
bool LoadProjectPage::tbd_can_cd_up() const {
  return can_show_parent_entry();
}
#endif

uint8_t LoadProjectPage::entry_type_for_dir(const char *entry) {
#ifdef MCL_HAS_FILE_MOVE
  if (move_destination_mode) {
    // Only folders are destinations, not projects.
    if (FileBrowserPage::entry_type_for_dir(entry) == SKIP_TYPE ||
        is_project_dir(entry)) {
      return SKIP_TYPE;
    }
    return DIR_TYPE;
  }
#endif
#ifdef __AVR__
  (void)entry;
  return UNKNOWN_DIR_TYPE;
#else
  return is_project_dir(entry) ? FILE_TYPE : DIR_TYPE;
#endif
}

uint8_t LoadProjectPage::resolve_entry_type(uint16_t n, const char *entry,
                                            uint8_t type) {
#ifdef __AVR__
  if (type == UNKNOWN_DIR_TYPE) {
    type = is_project_dir(entry) ? FILE_TYPE : DIR_TYPE;
    set_entry_type(n, type);
  }
#else
  (void)n;
  (void)entry;
#endif
  return type;
}

bool LoadProjectPage::is_project_dir(const char *entry) const {
  size_t len = strlen(entry);
  if (len == 0 || len > PRJ_NAME_LEN || strchr(entry, '/') != nullptr) {
    return false;
  }

  char project_file[PRJ_NAME_LEN * 2 + 6];
  if (!MCLSd::join_path(project_file, sizeof(project_file), entry, entry)) {
    return false;
  }
  strcat(project_file, ".mcl");
  return SD.exists(project_file);
}

bool LoadProjectPage::build_project_path(const char *entry, char *out,
                                         size_t out_len) const {
  const char *parent = nullptr;
  if (!current_project_parent(&parent)) {
    return false;
  }

  size_t entry_len = strlen(entry);
  if (entry_len == 0 || entry_len > PRJ_NAME_LEN) {
    return false;
  }
  return MCLSd::join_path(out, out_len, parent, entry);
}

void LoadProjectPage::focus_current_project() {
  if (last_project[0] != '\0') {
    strcpy(focus_match, last_project);
    return;
  }
  focus_match[0] = '\0';

  const char *parent = nullptr;
  if (!current_project_parent(&parent)) {
    return;
  }

  const char *project_path = mcl_cfg.project;
  size_t parent_len = strlen(parent);
  const char *focus = nullptr;

  if (parent_len == 0) {
    focus = project_path;
  } else if (strncmp(project_path, parent, parent_len) == 0 &&
             project_path[parent_len] == '/') {
    focus = project_path + parent_len + 1;
  } else {
    return;
  }

  const char *slash = strchr(focus, '/');
  size_t len = slash == nullptr ? strlen(focus) : (size_t)(slash - focus);
  if (len >= sizeof(focus_match)) {
    len = sizeof(focus_match) - 1;
  }
  memcpy(focus_match, focus, len);
  focus_match[len] = '\0';
}
