// Copyright 2026 Brett-D
// SPDX-License-Identifier: GPL-2.0-or-later
//
// v27: the right OLED's display settings (brightness, off, sleep timeout) and
// the Display menu on the INFO page; also carries the loop profiler's numbers
// between the halves (display.c).

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "perf.h"

void display_init(void); // keyboard_post_init_user (both halves)
void display_task(void); // housekeeping (master: menu timeout, sync to the right half)

// Master: the menu (INFO page). A right-knob turn opens it; turns move the
// choice or change the value being edited; a tap does the choice.
bool display_menu_open(void);
bool display_menu_turn(int8_t dir); // true = used
bool display_menu_press(void);      // a tap: true = used (false: leave the page)
void display_menu_close(void);
// Master: "Off" until a knob press (or the config page) wakes it.
bool display_is_off(void);
void display_wake(void);
// Master: the settings (config page, backups). bright 1-5, sleep index, off.
void display_get(uint8_t *bright, uint8_t *sleep, bool *off);
bool display_set(uint8_t bright, uint8_t sleep, bool off); // saves; false = out of range
// Master: the right half's profiler report (false: none yet, or a right half older than v27).
bool display_right_perf(uint8_t out[PERF_REPORT_LEN]);
void display_perf_reset(void); // both halves

#ifdef OLED_ENABLE
// Right half (oled.c): brightness now; true = the screen stays off (Off, or asleep).
bool display_oled_off(bool busy, uint32_t idle_ms);
// Right half (oled_pages.c): the menu over the INFO page; true = drawn.
bool display_menu_render(void);
bool display_menu_showing(void);
// Right half: the left half's longest loop pass, last 10 s (10 us units).
uint16_t display_left_loop(void);
#endif
