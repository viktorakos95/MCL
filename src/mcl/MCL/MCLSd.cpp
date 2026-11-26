#include "MCLSd.h"
#include "ResourceManager.h"
#include "MCLGUI.h"
#include "StackMonitor.h"
#include "Project.h"
#include "UpgradeLog.h"
#include "Sequencer/PtcGroups.h"
#include "platform.h"
#include <string.h>

namespace {

constexpr uint8_t kSdPathLen = 128;

#ifdef __AVR__
constexpr uint8_t kRemoveDirMaxDepth = 8;

bool is_root_path(const char *path) {
  if (path == nullptr || path[0] == '\0') {
    return true;
  }
  while (*path == '/') {
    path++;
  }
  return *path == '\0';
}
#else
bool copy_sd_path(char *dst, uint8_t dst_len, const char *src) {
  if (dst_len == 0) {
    return false;
  }
  char *write = dst;
  char *end = dst + dst_len - 1;
  while (*src != '\0') {
    if (write == end) {
      *dst = '\0';
      return false;
    }
    *write++ = *src++;
  }
  *write = '\0';
  return true;
}

void trim_trailing_slashes(char *path) {
  size_t len = strlen(path);
  while (len > 1 && path[len - 1] == '/') {
    path[--len] = '\0';
  }
}

bool parent_path(char *path) {
  trim_trailing_slashes(path);
  char *slash = strrchr(path, '/');
  if (slash == nullptr) {
    return false;
  }
  if (slash == path) {
    path[1] = '\0';
  } else {
    *slash = '\0';
  }
  return true;
}
#endif

#ifdef MCL_HAS_PLATFORM_HEADLESS_BOOT
bool load_headless_project() {
  static const char kHeadlessProjectName[] = "project000";

  if (mcl_cfg.project[0] != '\0' && proj.load_project(mcl_cfg.project)) {
    return true;
  }
  if (proj.load_project(kHeadlessProjectName)) {
    return true;
  }
  if (!proj.new_project(kHeadlessProjectName)) {
    return proj.load_project(kHeadlessProjectName);
  }
  return proj.load_project(kHeadlessProjectName);
}

bool wait_for_project_or_headless_boot() {
  if (mcl_platform_headless_boot()) {
    bool ok = load_headless_project();
    DEBUG_PRINTLN(ok ? F("Headless project loaded")
                     : F("Headless project load failed"));
    return ok;
  }

  mcl_gui.wait_for_project();
  return true;
}
#else
#define wait_for_project_or_headless_boot() (mcl_gui.wait_for_project(), true)
#endif

#ifdef MCL_PROJECT_UPGRADER
// What the two passes over the projects folder count.
struct UpgradeCounts {
  uint8_t folders; // top-level folders, for the scan's bar
  uint8_t scanned; // of those, scanned
  uint8_t current, needed, unreadable;
  uint8_t index, ok, no_room, failed; // the conversions
};

// Writes text_P, or n in decimal, at p and returns the end.
char *put_P(char *p, const char *text_P) {
  strcpy_P(p, text_P);
  return p + strlen(p);
}
char *put_count(char *p, uint8_t n) {
  mcl_gui.put_value_at(n, p);
  return p + strlen(p);
}
char *put_projects(char *p, uint8_t n) {
  p = put_count(p, n);
  return put_P(p, n == 1 ? PSTR(" PROJECT") : PSTR(" PROJECTS"));
}

void upgrade_status(const char *text_P, uint8_t n = 0, uint8_t total = 0) {
  char text[32];
  put_P(text, text_P);
  upgrade_log::status(text, n, total);
}

// Opens the projects folder, or the folder path inside it. SdFat cannot
// open "." by name (it trims a name's trailing dots), so this changes to the
// folder and opens the working folder.
bool open_upgrade_dir(File *dir, const char *path) {
  proj.chdir_projects();
  if (path[0] && !SD.chdir(path)) {
    return false;
  }
  return dir->openCwd();
}

// The top-level folders, for the scan's progress bar.
uint8_t count_upgrade_folders() {
  File dir;
  File entry;
  uint8_t n = 0;
  if (!open_upgrade_dir(&dir, "")) {
    return 0;
  }
  while (entry.openNext(&dir, O_READ)) {
    if (entry.isDirectory() && n < 255) {
      n++;
    }
    entry.close();
  }
  dir.close();
  return n;
}

// Converts one project the scan listed, with a line that counts up in
// percent and ends OK, NO ROOM or FAILED.
void upgrade_convert(const char *path, UpgradeCounts *c) {
  char text[32];
  char *p = put_count(text, ++c->index);
  *p++ = '/';
  p = put_count(p, c->needed);
  *p++ = ' ';
  strncpy(p, path, text + sizeof(text) - 1 - p);
  text[sizeof(text) - 1] = '\0';
  upgrade_log::add_progress(text);

  p = put_P(text, PSTR("UPGRADING "));
  p = put_count(p, c->index);
  *p++ = '/';
  put_count(p, c->needed);
  upgrade_log::status(text, c->index - 1, c->needed);

  const uint8_t skipped = proj.upgrade_skipped;
  proj.upgrade_scan = false;
  const char *result;
  if (proj.load_project(path)) {
    result = PSTR("OK");
    c->ok++;
  } else if (proj.upgrade_skipped != skipped) {
    result = PSTR("NO ROOM");
    c->no_room++;
  } else {
    result = PSTR("FAILED");
    c->failed++;
  }
  upgrade_log::set_right_P(result);
}

// Every folder under the projects folder once, and one level down in a
// folder that is not a project. A scan lists each project with what it
// needs and writes nothing; otherwise the projects it listed as UPGRADE
// convert.
void upgrade_walk(char *path, bool top, bool scan, UpgradeCounts *c) {
  File dir;
  File entry;
  if (!open_upgrade_dir(&dir, path)) {
    return;
  }
  const uint8_t base = strlen(path);
  while (entry.openNext(&dir, O_READ)) {
    const bool is_dir = entry.isDirectory();
    uint8_t at = base;
    if (base) {
      path[at++] = '/';
    }
    if (is_dir && at + 14 <= PRJ_PATH_LEN) {
      entry.getName(path + at, PRJ_PATH_LEN - at);
    } else {
      path[at] = '\0';
    }
    entry.close();
    if (path[at] != '\0' && path[at] != '.') {
      proj.upgrade_scan = true;
      proj.load_project(path);
      const uint8_t state = proj.upgrade_state;
      if (state == UPGRADE_NOT_PROJECT) {
        if (top) {
          upgrade_walk(path, false, scan, c);
        }
      } else if (scan) {
        const char *word = PSTR("CAN'T READ");
        if (state == UPGRADE_NEEDED) {
          word = PSTR("UPGRADE");
          c->needed++;
        } else if (state == UPGRADE_CURRENT) {
          word = PSTR("UP TO DATE");
          c->current++;
        } else {
          c->unreadable++;
        }
        upgrade_log::add(path, word);
      } else if (state == UPGRADE_NEEDED) {
        upgrade_convert(path, c);
      }
      proj.chdir_projects();
    }
    if (top && scan && is_dir) {
      upgrade_status(PSTR("SCANNING"), ++c->scanned, c->folders);
    }
    path[base] = '\0';
  }
  dir.close();
}

// Counts how many conversions fit in the card's free space. Each one needs a
// new pair of grid files, plus a little for the project file kept as .old.
uint16_t count_upgrade_room() {
  const uint32_t cluster_bytes = SD.bytesPerCluster();
  const int32_t free_clusters = SD.freeClusterCount();
  if (cluster_bytes == 0 || free_clusters <= 0) {
    return 0;
  }
  const uint32_t grid_bytes = (uint32_t)GRID_SLOT_BYTES *
                              (uint32_t)(GRID_LENGTH + 2) *
                              (uint32_t)(GRID_WIDTH + 1);
  const uint32_t grid_clusters = (grid_bytes + cluster_bytes - 1) / cluster_bytes;
  const uint32_t room =
      (uint32_t)free_clusters / (grid_clusters * NUM_GRIDS + 2);
  return room > 0xFFFF ? 0xFFFF : room;
}

void upgrade_finish() __attribute__((noreturn));
void upgrade_finish() {
  upgrade_status(PSTR("NOW UPGRADE TO MCL " VERSION_STR));
  upgrade_log::idle();
}

// The upgrader's one job: scan the card, say what it found, ask, then
// convert the projects from before MCL 5.00 with a line for each. Projects
// that already open in this MCL are not touched. It ends on its last screen,
// so nothing after it at boot is built into the upgrader.
void upgrade_all_projects() __attribute__((noreturn));
void upgrade_all_projects() {
  char path[PRJ_PATH_LEN] = "";
  char text[32];
  char detail[32];
  char *p;
  UpgradeCounts c = {};
  upgrade_log::begin();
  upgrade_log::add_P(PSTR("MCL " VERSION_STR " PROJECT UPGRADE"));
  // Nothing is read until the user starts it.
  strcpy_P(text, PSTR("SCAN FOR PROJECTS?"));
  detail[0] = '\0';
  if (!upgrade_log::ask(text, detail)) {
    upgrade_log::add_P(PSTR("STOPPED, NO PROJECT CHANGED"));
    upgrade_finish();
  }
  upgrade_log::add_P(PSTR("SCANNING SD CARD"));
  c.folders = count_upgrade_folders();
  upgrade_status(PSTR("SCANNING"), 0, c.folders);
  proj.upgrade_skipped = 0;
  upgrade_walk(path, true, true, &c);

  p = put_P(text, PSTR("FOUND "));
  put_projects(p, c.current + c.needed + c.unreadable);
  upgrade_log::add(text);
  if (c.unreadable) {
    p = put_count(text, c.unreadable);
    put_P(p, PSTR(" CAN'T BE READ"));
    upgrade_log::add(text);
  }
  if (!c.needed) {
    upgrade_log::add_P(PSTR("NOTHING TO UPGRADE"));
    upgrade_finish();
  }
  p = put_count(text, c.needed);
  p = put_P(p, PSTR(" TO UPGRADE, "));
  p = put_count(p, c.current);
  put_P(p, PSTR(" UP TO DATE"));
  upgrade_log::add(text);

  // Reading the free space of a large card takes a few seconds.
  upgrade_status(PSTR("CHECKING FREE SPACE"));
  proj.upgrade_room = count_upgrade_room();
  if (proj.upgrade_room < c.needed) {
    p = put_P(text, PSTR("ROOM ON CARD FOR "));
    p = put_count(p, proj.upgrade_room);
    p = put_P(p, PSTR(" OF "));
    put_count(p, c.needed);
    upgrade_log::add(text);
  }

  p = put_P(text, PSTR("UPGRADE "));
  p = put_projects(p, c.needed);
  put_P(p, PSTR("?"));
  detail[0] = '\0';
  if (proj.upgrade_room < c.needed) {
    p = put_P(detail, PSTR("ROOM FOR "));
    put_count(p, proj.upgrade_room);
  }
  if (!upgrade_log::ask(text, detail)) {
    upgrade_log::add_P(PSTR("STOPPED, NO PROJECT CHANGED"));
    upgrade_finish();
  }

  upgrade_log::start_file();
  upgrade_walk(path, true, false, &c);
  upgrade_status(PSTR("DONE"), c.needed, c.needed);
  p = put_count(text, c.ok);
  put_P(p, PSTR(" UPGRADED"));
  upgrade_log::add(text);
  if (c.no_room) {
    p = put_count(text, c.no_room);
    put_P(p, PSTR(" LEFT, NO ROOM ON CARD"));
    upgrade_log::add(text);
    upgrade_log::add_P(PSTR("FREE SOME SPACE, RUN AGAIN"));
  }
  if (c.failed) {
    p = put_count(text, c.failed);
    put_P(p, PSTR(" FAILED"));
    upgrade_log::add(text);
  }
  upgrade_finish();
}
#endif

} // namespace

#ifndef WAV_DESIGNER
// The menu tables still name the WAV designer entry points.
void wavdesign_menu_handler() {}
void wav_render() {}
#endif

bool MCLSd::join_path(char *dst, uint8_t dst_len, const char *dir,
                      const char *entry) {
  if (dst_len == 0) {
    return false;
  }
  char *write = dst;
  char *end = dst + dst_len - 1;
  while (*dir != '\0') {
    if (write == end) {
      *dst = '\0';
      return false;
    }
    *write++ = *dir++;
  }
  if (write != dst && write[-1] != '/') {
    if (write == end) {
      *dst = '\0';
      return false;
    }
    *write++ = '/';
  }
  while (*entry != '\0') {
    if (write == end) {
      *dst = '\0';
      return false;
    }
    *write++ = *entry++;
  }
  *write = '\0';
  return true;
}

#ifdef __AVR__
void mcl_oled_set_sd_dedicated_spi(bool dedicated) {
  SD.setDedicatedSpi(dedicated);
}
#endif

/*
   Function for initialising the SD Card
*/

bool MCLSd::sd_init() {
  bool ret = false;
  DEBUG_PRINT_FN();
  DEBUG_PRINTLN(F("Initializing SD Card"));
  // File file("/test.mcl",O_WRITE);
  /*Configuration file used to store settings when Minicommand is turned off*/
  for (uint8_t n = 0; n < SD_MAX_RETRIES && ret == false; n++) {

    ret = SD.begin(SD_CONFIG);
    //ret = SD.begin(SdSpiConfig(SD_CS, DEDICATED_SPI, SD_SCK_MHZ(50)));
    if (!ret) {
      delay(50);
    }
  }
  if (ret == false) {
    sd_state = false;
    DEBUG_PRINTLN(F("SD Init fail"));
    return false;
  }
  sd_state = true;

#ifndef __AVR__
  if (SD.exists("/config.mcls")) {
    strcpy(mcl_root, "");
    DEBUG_PRINTLN(F("Root is /"));
  } else {
    strcpy(mcl_root, "/MCL");
    DEBUG_PRINTLN(F("Root is /MCL"));
    if (!SD.exists(mcl_root)) {
      SD.mkdir(mcl_root, true);
      char buf[64];
      strcpy(buf, mcl_root); strcat(buf, "/Projects");
      SD.mkdir(buf, true);
      strcpy(buf, mcl_root); strcat(buf, "/Samples");
      SD.mkdir(buf, true);
      strcpy(buf, mcl_root); strcat(buf, "/Samples/WAV");
      SD.mkdir(buf, true);
      strcpy(buf, mcl_root); strcat(buf, "/Samples/SYX");
      SD.mkdir(buf, true);
      strcpy(buf, mcl_root); strcat(buf, "/Sounds");
      SD.mkdir(buf, true);
    }
  }
#endif

  DEBUG_PRINTLN(F("SD Init okay"));
  return true;
}

#ifndef __AVR__
const char *MCLSd::full_path(const char *path, char *buffer, size_t size) {
  if (mcl_root[0] == '\0') return path;
  if (path[0] != '/') return path;

  size_t root_len = strlen(mcl_root);
  if (strncmp(path, mcl_root, root_len) == 0 &&
      (path[root_len] == '\0' || path[root_len] == '/')) {
    return path;
  }

  if (size == 0) return path;
  strncpy(buffer, mcl_root, size);
  buffer[size - 1] = '\0';
  if (path[1] != '\0') {
    strncat(buffer, path, size - strlen(buffer) - 1);
  }
  return buffer;
}
#endif
bool MCLSd::load_init() {
#ifdef MCL_PROJECT_UPGRADER
  // The upgrader converts project files and nothing else: it never opens
  // the config.
  if (sd_state) {
    upgrade_all_projects();
  }
#endif
  if (sd_state) {
    char path[64];
    if (mcl_cfg.cfgfile.open(full_path("/config.mcls", path, sizeof(path)), O_RDWR)) {
      DEBUG_PRINTLN(F("Config file open: success"));

      if (read_data((uint8_t *)&mcl_cfg, sizeof(MCLSysConfigData),
                    &mcl_cfg.cfgfile)) {
        DEBUG_PRINTLN(F("Config file read: success"));

        if (mcl_cfg.version != CONFIG_VERSION) {
          DEBUG_PRINTLN(F("Incompatible config version"));
          if (!mcl_cfg.cfg_init()) {
            DEBUG_PRINTLN(F("Could not init cfg"));
            return false;
          }
          gfx.draw_evil(R.icons_boot->evilknievel_bitmap);
          oled_display.clearDisplay();
          return wait_for_project_or_headless_boot();

        }

        // roll_ignores_mute lives one byte past the versioned struct (see
        // MCLSysConfig::write_cfg()) rather than inside it, so it doesn't
        // affect CONFIG_VERSION. A short read (upgrading from a build that
        // predates this byte) or a stray value both fall back to the
        // default (1, matching prior hardcoded behavior) rather than
        // trusting whatever preallocated/uninitialised byte was on disk.
        if (!read_data(&mcl_cfg.roll_ignores_mute,
                       sizeof(mcl_cfg.roll_ignores_mute), &mcl_cfg.cfgfile) ||
            mcl_cfg.roll_ignores_mute > 1) {
          mcl_cfg.roll_ignores_mute = 1;
        }

        // roll_triplets follows immediately after roll_ignores_mute — same
        // rationale/fallback as above (default 1, matching prior behavior
        // of every rate being available).
        if (!read_data(&mcl_cfg.roll_triplets, sizeof(mcl_cfg.roll_triplets),
                       &mcl_cfg.cfgfile) ||
            mcl_cfg.roll_triplets > 1) {
          mcl_cfg.roll_triplets = 1;
        }

        // roll_active follows immediately after roll_triplets, same
        // rationale/fallback as above (default 1, matching prior behavior
        // of the roll gesture always being active).
        if (!read_data(&mcl_cfg.roll_active, sizeof(mcl_cfg.roll_active),
                       &mcl_cfg.cfgfile) ||
            mcl_cfg.roll_active > 1) {
          mcl_cfg.roll_active = 1;
        }

        if (mcl_cfg.project_config > 1) {
          mcl_cfg.project_config = 0;
        }
        if (mcl_cfg.md_sample_bank > MD_SAMPLE_BANK_FIXED_LAST) {
          mcl_cfg.md_sample_bank = 0;
        }
        if (mcl_cfg.md_sample_bank_capture > 128) {
          mcl_cfg.md_sample_bank_capture = 0;
        }
        {
          // FADE, its length and SYSTEM > LOAD/SAVE follow the persisted
          // block, then their tag.
          uint8_t *panel = (uint8_t *)&mcl_cfg.load_fade;
          memset(panel, 0, 4);
          mcl_cfg.cfgfile.read(panel, 4);
          if (mcl_cfg.load_panel_tag != LOAD_PANEL_CFG_TAG ||
              panel[0] > (1 | LOAD_FADE_FORCE_MAX << 1) ||
              mcl_cfg.load_fade_len > LOAD_FADE_STORED_MAX ||
              mcl_cfg.io_stay > LOAD_SAVE_STAY) {
            memset(panel, 0, 3);
          }
          // A fade set longer than the longest now takes the longest.
          if (mcl_cfg.load_fade_len > LOAD_FADE_MAX) {
            mcl_cfg.load_fade_len = LOAD_FADE_MAX;
          }
        }
        ptc_groups.load(mcl_cfg.ptc_group);

        if (mcl_cfg.number_projects > 0) {
          DEBUG_PRINTLN(
              F("Project count greater than 0, try to load existing"));
          if (!proj.load_project(mcl_cfg.project)) {
            DEBUG_PRINTLN(F("error loading project"));
            return wait_for_project_or_headless_boot();

          } else {
            DEBUG_PRINTLN(F("Project loaded successfully, load grid"));
            return true;
          }
          return true;
        } else {
          return wait_for_project_or_headless_boot();
        }
      } else {
        DEBUG_PRINTLN(F("Could not read cfg file."));

        if (!mcl_cfg.cfg_init()) {
          return false;
        }
        return wait_for_project_or_headless_boot();
      }
    } else {
      DEBUG_PRINTLN(F("Could not open cfg file. Let's try to create it"));
      if (!mcl_cfg.cfg_init()) {
        return false;
      }
      oled_display.clearDisplay();
      return wait_for_project_or_headless_boot();
    }
    return true;
  }
  return false;
}

bool MCLSd::seek(uint32_t pos, File *filep) {
  bool ret;
  uint8_t n = 0;
  if (!filep) {
    DEBUG_PRINTLN(F("huh"));
    return false;
  }

  do {
    ret = filep->seekSet(pos);
    if (ret) {
      return true;
    }
    DEBUG_PRINTLN(F("seek retry"));
    DEBUG_PRINTLN(pos);
    delay(20);
    n++;
  } while (n < SD_MAX_RETRIES);

  return false;
}

bool MCLSd::write_data(void *data, size_t len, File *filep) {
  uint32_t pos = filep->curPosition();
  uint8_t n = 0;

  do {
    size_t b = filep->write((uint8_t *)data, len);
    if (b == len) {
      return true;
    }
    DEBUG_PRINTLN(F("write retry"));
    delay(20);
    write_fail++;
    filep->seekSet(pos);
    n++;
  } while (n < SD_MAX_RETRIES);

  return false;
}

/*
   Function for reading from the project file
*/
bool MCLSd::read_data(void *data, size_t len, File *filep) {
  uint32_t pos = filep->curPosition();
  uint8_t n = 0;

  do {
    size_t b = filep->read((uint8_t *)data, len);
    if (b == len) {
      return true;
    }
    DEBUG_PRINTLN(F("read retry"));
    delay(20);
    read_fail++;
    filep->seekSet(pos);
    n++;
  } while (n < SD_MAX_RETRIES);

  return false;
}

COLD_SMALL() bool MCLSd::copy_file(const char *src, const char *dst, uint8_t progress_base,
                      uint8_t progress_span, uint8_t progress_max) {
  File in;
  File out;
  if (!in.open(src, O_READ)) {
    return false;
  }
  if (!out.open(dst, O_RDWR | O_CREAT | O_EXCL)) {
    in.close();
    return false;
  }

  bool ok = true;
  uint32_t size = in.size();
  if (size > 0 && !out.preAllocate(size)) {
    ok = false;
  }

  uint8_t buf[512];
  uint32_t copied = 0;
  uint8_t progress_end = progress_base + progress_span;
  while (ok) {
    int n = in.read(buf, sizeof(buf));
    if (n < 0) {
      ok = false;
      break;
    }
    if (n == 0) {
      break;
    }
    ok = write_data(buf, (size_t)n, &out);
    copied += n;
    if (ok && progress_span > 0 && progress_max > 0 && size > 0) {
      uint8_t progress =
          progress_base + ((uint32_t)copied * progress_span) / size;
      if (progress > progress_end) {
        progress = progress_end;
      }
      mcl_gui.draw_progress_bar(progress, progress_max, false, 31, 21);
    }
  }
  if (ok) {
    ok = out.sync();
  }

  out.close();
  in.close();
  if (!ok) {
    SD.remove(dst);
  }
  return ok;
}

bool MCLSd::remove_dir(const char *dir) {
#ifdef __AVR__
  if (is_root_path(dir)) {
    return false;
  }

  struct Remover {
    static bool remove(const char *path, uint8_t depth) {
      if (depth > kRemoveDirMaxDepth) {
        return false;
      }

      File d;
      if (!d.open(path, O_READ) || !d.isDirectory()) {
        d.close();
        return false;
      }
      d.rewind();

      File entry_file;
      char entry[FILE_ENTRY_SIZE];
      char child[kSdPathLen];

      while (entry_file.openNext(&d, O_READ)) {
        entry_file.getName(entry, sizeof(entry));
        bool is_dir = entry_file.isDirectory();
        entry_file.close();

        if (entry[0] == '\0' || entry[0] == '.') {
          continue;
        }
        if (!MCLSd::join_path(child, sizeof(child), path, entry)) {
          d.close();
          return false;
        }
        if (is_dir) {
          if (!remove(child, depth + 1)) {
            d.close();
            return false;
          }
        } else if (!SD.remove(child)) {
          d.close();
          return false;
        }
      }

      entry_file.close();
      d.close();
      return SD.rmdir(path);
    }
  };

  return Remover::remove(dir, 0);
#else
  char root[kSdPathLen];
  char path[kSdPathLen];
  if (!copy_sd_path(root, sizeof(root), dir)) {
    return false;
  }
  trim_trailing_slashes(root);
  if (root[0] == '\0' || strcmp(root, "/") == 0) {
    return false;
  }
  if (!copy_sd_path(path, sizeof(path), root)) {
    return false;
  }

  while (true) {
    File d;
    if (!d.open(path, O_READ) || !d.isDirectory()) {
      d.close();
      return false;
    }
    d.rewind();

    File entry_file;
    char entry[FILE_ENTRY_SIZE];
    char child[kSdPathLen];
    bool found_child = false;

    while (entry_file.openNext(&d, O_READ)) {
      entry_file.getName(entry, sizeof(entry));
      bool is_dir = entry_file.isDirectory();
      entry_file.close();

      if (entry[0] == '\0' || entry[0] == '.') {
        continue;
      }
      if (!join_path(child, sizeof(child), path, entry)) {
        d.close();
        return false;
      }
      if (is_dir) {
        if (!copy_sd_path(path, sizeof(path), child)) {
          d.close();
          return false;
        }
        found_child = true;
        break;
      }
      if (!SD.remove(child)) {
        d.close();
        return false;
      }
      found_child = true;
      break;
    }

    entry_file.close();
    d.close();
    if (found_child) {
      continue;
    }
    if (!SD.rmdir(path)) {
      return false;
    }
    if (strcmp(path, root) == 0) {
      return true;
    }
    if (!parent_path(path)) {
      return false;
    }
  }
#endif
}

#ifndef __AVR__
bool MCLSd::copy_dir(const char *src, const char *dst, uint8_t progress_base,
                     uint8_t progress_span, uint8_t progress_max) {
  if (SD.exists(dst)) {
    return false;
  }
  if (!SD.mkdir(dst, true)) {
    return false;
  }

  File dir;
  if (!dir.open(src, O_READ) || !dir.isDirectory()) {
    dir.close();
    remove_dir(dst);
    return false;
  }
  dir.rewind();

  File entry_file;
  char entry[FILE_ENTRY_SIZE];
  char src_path[128];
  char dst_path[128];
  bool ok = true;

  while (ok && entry_file.openNext(&dir, O_READ)) {
    entry_file.getName(entry, sizeof(entry));
    bool is_dir = entry_file.isDirectory();
    entry_file.close();

    if (entry[0] == '\0' || entry[0] == '.') {
      continue;
    }
    if (!join_path(src_path, sizeof(src_path), src, entry) ||
        !join_path(dst_path, sizeof(dst_path), dst, entry)) {
      ok = false;
      break;
    }
    ok = is_dir ? copy_dir(src_path, dst_path, progress_base, progress_span,
                           progress_max)
                : copy_file(src_path, dst_path, progress_base, progress_span,
                            progress_max);
  }

  entry_file.close();
  dir.close();
  if (!ok) {
    remove_dir(dst);
  }
  return ok;
}
#endif

MCLSd mcl_sd;
