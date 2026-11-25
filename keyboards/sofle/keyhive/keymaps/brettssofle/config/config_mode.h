// Copyright 2026 Brett-D
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Config mode: edit the keymap, knobs, underglow and trackpad from the
// keyboard itself, with sofle-config.html as the display (see readme).

#pragma once

#include <stdbool.h>
#include <stdint.h>

void config_mode_init(void); // keyboard_post_init_user
void config_mode_task(void); // housekeeping_task_user (after rgb_idle_task)
void config_mode_toggle(void);
bool config_mode_active(void);
bool config_mode_pages_visible(void); // master: the right OLED shows its pages (v27 Display menu)
bool config_mode_led_cal_busy(void); // LED calibration owns the LEDs (no overlay, linear output)
bool config_mode_underglow_dark(void); // LED zone "keys only" (both halves)
// No pet LED reactions now (master): LED calibration, typed delivery, keyboard-only
// mode; ambient ones (moods, sleep) also not in network mode or a web session.
bool config_mode_leds_quiet(bool ambient);
#ifdef OLED_ENABLE
// Right half: draws the config screen and returns true while config mode is on.
bool config_mode_oled_task(void);
bool config_mode_oled_busy(void); // a config / network / delivery screen must stay on
#endif
// The firmware version (the INFO page; both halves compare theirs). Bump with every release.
#define BRETT_FW_VERSION 27
// Right half: the left half's version as last synced (0 = not known: v18 or older).
uint8_t config_mode_left_fw(void);
