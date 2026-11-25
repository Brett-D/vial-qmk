// Copyright 2026 Brett-D
// SPDX-License-Identifier: GPL-2.0-or-later
//
// v27 main-loop profiler (Brett: "keyboard is also lagging sometimes"): how
// long each pass of QMK's main loop takes on this half, split into sections,
// with the worst over the last 10 s (1 s buckets) and since the last reset.
//
// A key is read once per loop pass, so the longest pass is the longest a
// press can wait to be seen (plus the debounce; right-half keys also wait for
// the left half's next pass). Sections come from marks at points every pass
// goes through (perf_mark: the time since the previous mark) and from spans
// around our own code (perf_add):
//
//   loop start (end of housekeeping) -> matrix_scan_user        PF_SCAN
//   -> our LED background (render passes)                        PF_TASKS, PF_LEDBG
//   -> LED flush (overlay, pet, curve, data out)                  PF_TASKS, PF_FLUSH
//   -> housekeeping start                                        PF_TASKS
//   housekeeping parts                                           PF_DITHER .. PF_NET
//   EEPROM writes, wherever                                      PF_FLASH (also in its section)
//   right half: drawing the OLED page                            PF_OLED (inside PF_TASKS)

#pragma once

#include <stdbool.h>
#include <stdint.h>

enum perf_section {
    PF_SCAN,   // matrix scan + debounce (left: + the split sync with the right half)
    PF_TASKS,  // keys + USB reports, knobs, trackpad read (left); OLED draw + I2C (right); raw HID
    PF_LEDBG,  // our LED backgrounds (QMK's own effects count in PF_TASKS)
    PF_FLUSH,  // LED flush: key-press overlay, pet reaction, screensaver, curve, data out
    PF_OLED,   // right: drawing the OLED page (CPU only; the I2C transfer is in PF_TASKS)
    PF_DITHER, // LED dither re-sends
    PF_PET,    // the pet (left)
    PF_SYNC,   // OLED view, lighting and display sync to the right half (left)
    PF_NET,    // config mode / USB network
    PF_FLASH,  // EEPROM (flash) writes: the chip stops while they run
    PF_COUNT
};

// The report (perf_report, the display RPC, raw HID 0xF6 0x1F): times in 10 us units.
#define PERF_REPORT_LEN (7 + 2 * PF_COUNT)
enum {
    PR_LOOP_MAX  = 0, // u16: longest loop pass, last 10 s
    PR_WORST     = 2, // u16: longest since the last reset
    PR_WORST_SEC = 4, // its biggest section
    PR_SPIKES    = 5, // passes over 10 ms since the last reset (stops at 255)
    PR_AVG       = 6, // average pass, last 10 s, in 20 us units (stops at 255)
    PR_SECTIONS  = 7, // u16 x PF_COUNT: each section's longest, last 10 s
};

uint32_t perf_us(void);
void     perf_mark(uint8_t section);
void     perf_add(uint8_t section, uint32_t us);
void     perf_loop_end(void); // end of housekeeping_task_user
void     perf_report(uint8_t out[PERF_REPORT_LEN]);
void     perf_reset(void);
uint16_t perf_loop_max(void); // last 10 s, 10 us units

#define PERF_SPAN(section, stmt)              \
    do {                                      \
        uint32_t perf_t0_ = perf_us();        \
        stmt;                                 \
        perf_add(section, perf_us() - perf_t0_); \
    } while (0)
