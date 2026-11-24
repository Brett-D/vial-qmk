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
#ifdef OLED_ENABLE
// Right half: draws the config screen and returns true while config mode is on.
bool config_mode_oled_task(void);
#endif
// The firmware version (the INFO page; both halves compare theirs). Bump with every release.
#define BRETT_FW_VERSION 21
// Right half: the left half's version as last synced (0 = not known: v18 or older).
uint8_t config_mode_left_fw(void);
