/* The project upgrader's screen: a log that scrolls up over a status bar,
 * like a package manager's. Encoder 2 scrolls back through the log at any
 * time, also while a project converts.
 */
#pragma once

#include "MCLDefines.h"

#ifdef MCL_PROJECT_UPGRADER
#include <stdint.h>

namespace upgrade_log {

// Clears the screen and takes the log's lines from the resource buffer,
// which the upgrader uses for nothing else.
void begin();
// A new line: left is RAM text, right_P an optional right-aligned PROGMEM
// word. The oldest line drops off once the log is full.
void add(const char *left, const char *right_P = nullptr);
void add_P(const char *left_P, const char *right_P = nullptr);
// A line whose right word counts up (progress) until set_right_P gives its
// result; only then does it go to the file.
void add_progress(const char *left);
// The newest line's right-aligned word, final.
void set_right_P(const char *right_P);
// The newest line's right word as a percentage, from the conversion. Also
// reads encoder 2, so the log scrolls while a project converts.
void progress(uint8_t percent);
// The status bar: text, and a progress bar of n of total when total > 0.
void status(const char *text, uint8_t n = 0, uint8_t total = 0);
// A screen of its own under the heading PROJECT UPGRADE: the question,
// detail below it when not empty, and NO and YES. True for Load|Yes, false
// for Save|No; the log is shown again after.
bool ask(const char *question, const char *detail);
// From now on the log is also written to /Logs/upgrade.txt in MCL's folder
// on the card, starting with the lines already shown. Each line is written
// and synced as soon as it is final, so a power cut keeps what was done. A run is
// added to the end of the file. If the card has no room, the log stays on
// the screen only.
void start_file();
// Finishes the file, then scrolls the log until the unit is turned off.
void idle() __attribute__((noreturn));

} // namespace upgrade_log

#endif
