#include "UpgradeLog.h"

#ifdef MCL_PROJECT_UPGRADER
#include "MCLGUI.h"
#include "MCLSd.h"
#include "ResourceManager.h"
#include <string.h>
#include <util/atomic.h>

namespace upgrade_log {
namespace {

// Four log rows of the small font (6 px each) above a 7 px status bar.
constexpr uint8_t ROWS = 4;
constexpr uint8_t LEFT_LEN = 32;  // text, the width of the screen
constexpr uint8_t RIGHT_LEN = 12; // the right-aligned word
constexpr uint8_t LINE_BYTES = LEFT_LEN + RIGHT_LEN;
constexpr uint8_t LINES = 120;
static_assert((uint16_t)LINES * LINE_BYTES <= RM_BUFSIZE,
              "the log fits the resource buffer");
constexpr uint8_t TEXT_RIGHT = 124; // 1 px gap, then the scroll bar
constexpr uint8_t STATUS_Y = 25;

char *lines;
uint8_t head;   // the oldest line
uint8_t count;  // lines held
uint8_t scroll; // lines back from the newest; 0 follows new lines
char status_text[LEFT_LEN];
uint8_t status_n, status_total;
uint8_t status_pct; // of the project converting, for the progress bar
File log_file;
bool logging; // log_file is open

char *line(uint8_t i) { return lines + ((head + i) % LINES) * LINE_BYTES; }

// The small font's advance: 4 px, less for a few narrow characters.
uint8_t char_width(char c) {
  switch (c) {
  case ' ': case '!': case '\'': case '.': case ':': case 'i': case '|':
    return 2;
  case '(': case ')': case ',': case '1': case ';': case '`':
    return 3;
  }
  return 4;
}

uint8_t text_width(const char *s) {
  uint8_t w = 0;
  while (*s) {
    w += char_width(*s++);
  }
  return w;
}

// Prints s at baseline y from x, stopping before a character would pass
// limit. Returns where it stopped.
uint8_t print_clipped(uint8_t x, uint8_t y, const char *s, uint8_t limit) {
  oled_cursor(x, y);
  for (; *s; s++) {
    const uint8_t w = char_width(*s);
    if (x + w > limit + 1) {
      break;
    }
    oled_display.write(*s);
    x += w;
  }
  return x;
}

uint8_t max_scroll() { return count > ROWS ? count - ROWS : 0; }

void draw() {
  oled_display.clearDisplay();
  oled_font_small();
  oled_text_color(WHITE);
  const uint8_t top = max_scroll() - scroll;
  for (uint8_t r = 0; r < ROWS && top + r < count; r++) {
    const char *left = line(top + r);
    const char *right = left + LEFT_LEN;
    const uint8_t y = 5 + r * 6;
    if (right[0]) {
      const uint8_t rx = TEXT_RIGHT - text_width(right);
      print_clipped(0, y, left, rx - 3);
      print_clipped(rx, y, right, TEXT_RIGHT);
    } else {
      print_clipped(0, y, left, TEXT_RIGHT - 1);
    }
  }
  // Where the view is: a dotted track at the right edge and a 2 px thumb.
  if (count > ROWS) {
    for (uint8_t y = 0; y < 24; y += 2) {
      oled_display.drawPixel(127, y, WHITE);
    }
    uint8_t thumb = 24 * ROWS / count;
    if (thumb < 3) {
      thumb = 3;
    }
    const uint8_t pos = (uint16_t)(24 - thumb) * top / max_scroll();
    oled_display.fillRect(126, pos, 2, thumb, WHITE);
  }
  if (status_text[0]) {
    // A line under the log, then the status and MCL's progress bar, which
    // counts the projects done plus the share of the one converting.
    oled_display.drawFastHLine(0, STATUS_Y - 1, 128, WHITE);
    const uint8_t end = print_clipped(0, 31, status_text, 127);
    if (status_total) {
      const uint8_t bx = end + 4 > 74 ? end + 4 : 74;
      const uint8_t done =
          ((uint16_t)status_n * 100 + status_pct) / status_total;
      mcl_gui.draw_progress_bar(done, 100, true, bx, STATUS_Y + 1, 128 - bx,
                                5, true);
    }
  }
  oled_display.display();
}

// Encoder 2's clicks since the last call: left scrolls back, right forward.
bool read_scroll() {
  int8_t clicks;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    clicks = Encoders.encoders[1].normal;
    Encoders.encoders[1].normal = 0;
  }
  if (clicks == 0) {
    return false;
  }
  int16_t back = (int16_t)scroll - clicks;
  if (back < 0) {
    back = 0;
  } else if (back > max_scroll()) {
    back = max_scroll();
  }
  if (back == scroll) {
    return false;
  }
  scroll = back;
  return true;
}

// Drops encoder 2's clicks, turned while nothing scrolls.
void clear_scroll() {
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) { Encoders.encoders[1].normal = 0; }
}

// Save|No or Load|Yes down. The timer interrupt updates the buttons, which
// are not volatile: the barrier makes each call read them again, or a wait
// loop around it would test the first reading forever.
bool any_button_down() {
  asm volatile("" ::: "memory");
  return BUTTON_DOWN(Buttons.BUTTON1) || BUTTON_DOWN(Buttons.BUTTON4);
}

// Until both buttons have been up for 50 ms: a button still held from the
// last answer, or bouncing as it is let go, cannot answer the next question.
void wait_buttons_up() {
  uint16_t up_since = read_clock_ms();
  while (clock_diff(up_since, read_clock_ms()) < 50) {
    if (any_button_down()) {
      up_since = read_clock_ms();
    }
  }
}

// One line to the file: its text, then its right word, and synced. A
// failed write ends the file.
void write_line(const char *l) {
  if (!logging) {
    return;
  }
  const char *right = l + LEFT_LEN;
  const size_t left_len = strlen(l);
  const size_t right_len = strlen(right);
  bool ok = log_file.write(l, left_len) == left_len;
  if (right_len) {
    ok = ok && log_file.write("  ", 2) == 2 &&
         log_file.write(right, right_len) == right_len;
  }
  ok = ok && log_file.write("\r\n", 2) == 2 && log_file.sync();
  if (!ok) {
    log_file.close();
    logging = false;
  }
}

char *new_line() {
  if (count == LINES) {
    head = (head + 1) % LINES;
  } else {
    count++;
  }
  // Scrolled back, the view stays on the lines it shows.
  if (scroll && scroll < max_scroll()) {
    scroll++;
  }
  return line(count - 1);
}

void put_right_P(char *l, const char *right_P) {
  char *right = l + LEFT_LEN;
  right[0] = '\0';
  if (right_P) {
    strncpy_P(right, right_P, RIGHT_LEN - 1);
    right[RIGHT_LEN - 1] = '\0';
  }
}

} // namespace

void begin() {
  R.Clear();
  lines = (char *)R.Allocate((uint16_t)LINES * LINE_BYTES);
  head = count = scroll = 0;
  status_text[0] = '\0';
  clear_scroll();
  draw();
}

void add(const char *left, const char *right_P) {
  char *l = new_line();
  strncpy(l, left, LEFT_LEN - 1);
  l[LEFT_LEN - 1] = '\0';
  put_right_P(l, right_P);
  write_line(l);
  draw();
}

void add_P(const char *left_P, const char *right_P) {
  char *l = new_line();
  strncpy_P(l, left_P, LEFT_LEN - 1);
  l[LEFT_LEN - 1] = '\0';
  put_right_P(l, right_P);
  write_line(l);
  draw();
}

void add_progress(const char *left) {
  char *l = new_line();
  strncpy(l, left, LEFT_LEN - 1);
  l[LEFT_LEN - 1] = '\0';
  put_right_P(l, PSTR("0%"));
  draw();
}

void set_right_P(const char *right_P) {
  char *l = line(count - 1);
  put_right_P(l, right_P);
  write_line(l);
  draw();
}

void progress(uint8_t percent) {
  char *right = line(count - 1) + LEFT_LEN;
  char value[5];
  mcl_gui.put_value_at(percent, value);
  const uint8_t n = strlen(value);
  value[n] = '%';
  value[n + 1] = '\0';
  const bool changed = strcmp(right, value) != 0;
  if (changed) {
    strcpy(right, value);
    status_pct = percent;
  }
  if (read_scroll() || changed) {
    draw();
  }
}

void status(const char *text, uint8_t n, uint8_t total) {
  strncpy(status_text, text, LEFT_LEN - 1);
  status_text[LEFT_LEN - 1] = '\0';
  status_n = n;
  status_total = total;
  status_pct = 0;
  draw();
}

// A centred line in the 5x7 font (6 px a character), top at y.
static void centre_default(uint8_t y, const char *s) {
  const uint8_t w = strlen(s) * 6 - 1;
  oled_cursor(w < 128 ? (128 - w) / 2 : 0, y);
  oled_display.print(s);
}

// A 25 x 9 button on the bottom rows: its label in the small font, centred,
// with 1 px clear between the label and the frame above and below.
static void answer_box(uint8_t x, const char *label) {
  oled_display.drawRect(x, 23, 25, 9, WHITE);
  print_clipped(x + 1 + (24 - text_width(label)) / 2, 30, label, x + 23);
}

bool ask(const char *question, const char *detail) {
  oled_display.clearDisplay();
  oled_display.fillRect(0, 0, 128, 9, WHITE);
  oled_font_default();
  oled_text_color(BLACK);
  centre_default(1, "PROJECT UPGRADE");
  oled_text_color(WHITE);
  centre_default(12, question);
  oled_font_small();
  // Save|No on the left, Load|Yes on the right, the detail between them.
  answer_box(3, "NO");
  answer_box(100, "YES");
  if (detail[0]) {
    print_clipped((128 - text_width(detail)) / 2, 30, detail, 97);
  }
  oled_display.display();
  // Only the buttons' current state is read: the GUI loop that marks
  // presses is not running.
  wait_buttons_up();
  while (!any_button_down()) {
  }
  const bool yes = BUTTON_DOWN(Buttons.BUTTON4);
  wait_buttons_up();
  clear_scroll();
  draw();
  return yes;
}

void start_file() {
  char path[40];
  SD.mkdir(mcl_sd.full_path("/Logs", path, sizeof(path)), true);
  if (!log_file.open(mcl_sd.full_path("/Logs/upgrade.txt", path, sizeof(path)),
                     O_WRONLY | O_CREAT | O_APPEND)) {
    return;
  }
  logging = true;
  // A blank line between runs.
  if (log_file.fileSize()) {
    log_file.write("\r\n", 2);
  }
  for (uint8_t i = 0; i < count; i++) {
    write_line(line(i));
  }
}

void idle() {
  if (logging) {
    log_file.close();
    logging = false;
  }
  for (;;) {
    if (read_scroll()) {
      draw();
    }
  }
}

} // namespace upgrade_log

#endif
