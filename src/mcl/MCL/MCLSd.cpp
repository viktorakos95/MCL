#include "MCLSd.h"
#include "ResourceManager.h"
#include "MCLGUI.h"
#include "StackMonitor.h"
#include "Project.h"
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
// Opening a project converts it, so open every folder under the projects
// folder once. A folder that is not a project is searched one level down.
void upgrade_projects_in(char *path, uint8_t depth, uint8_t *upgraded) {
  File dir;
  File entry;
  proj.chdir_projects();
  if (!dir.open(path[0] ? path : ".", O_READ)) {
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
      oled_display.clearDisplay();
      oled_display.setCursor(0, 8);
      oled_display.println(F("UPGRADING"));
      oled_display.println(path);
      oled_display.display();
      const uint8_t skipped = proj.upgrade_skipped;
      if (proj.load_project(path)) {
        (*upgraded)++;
      } else if (depth && proj.upgrade_skipped == skipped) {
        upgrade_projects_in(path, depth - 1, upgraded);
      }
      proj.chdir_projects();
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

void upgrade_all_projects() {
  char path[PRJ_PATH_LEN] = "";
  uint8_t upgraded = 0;
  // Nothing on the card is touched until the user agrees.
  const bool confirmed =
      mcl_gui.wait_for_confirm("Upgrade", "Convert all projects?");
  proj.upgrade_skipped = 0;
  if (confirmed) {
    // Reading the free space of a large card takes a few seconds.
    oled_display.clearDisplay();
    oled_display.setCursor(0, 8);
    oled_display.println(F("CHECKING SD CARD"));
    oled_display.display();
    proj.upgrade_room = count_upgrade_room();
    upgrade_projects_in(path, 1, &upgraded);
  }
  oled_display.clearDisplay();
  oled_display.setCursor(0, 8);
  if (confirmed) {
    oled_display.print(upgraded);
    oled_display.println(F(" PROJECTS READY"));
    if (proj.upgrade_skipped) {
      oled_display.print(proj.upgrade_skipped);
      oled_display.println(F(" LEFT: SD FULL"));
    }
  } else {
    oled_display.println(F("NOTHING CHANGED"));
  }
  oled_display.println(F("INSTALL MCL " VERSION_STR));
  oled_display.display();
  while (1) {
  }
}
#endif

} // namespace

#ifdef MCL_PROJECT_UPGRADER
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
          // FADE and its length follow the persisted block, then their
          // complements.
          uint8_t *fade = (uint8_t *)&mcl_cfg.load_fade;
          memset(fade, 0, 4);
          mcl_cfg.cfgfile.read(fade, 4);
          if ((uint8_t)~fade[0] != fade[2] || (uint8_t)~fade[1] != fade[3] ||
              (uint8_t)(fade[0] + 1) > 2 || fade[1] > LOAD_FADE_MAX) {
            fade[0] = 0;
            fade[1] = 0;
          }
        }
        ptc_groups.load(mcl_cfg.ptc_group);

#ifdef MCL_PROJECT_UPGRADER
        upgrade_all_projects();
#endif
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
