// Copyright 2026 Brett-D
// SPDX-License-Identifier: GPL-2.0-or-later
//
// v28: built-in macro keycodes (BM_*, brettssofle.h) and the names of Vial's
// dynamic macros (macros.c).

#pragma once

#include <stdbool.h>
#include <stdint.h>

// process_record_user: true = it was one of ours (used up). Include after QMK_KEYBOARD_H (keyrecord_t).
bool builtin_macro_process(uint16_t keycode, keyrecord_t *record);

// Names for Vial's macros M0-M15 (the page's macro editor). Vial doesn't store
// names; these live in their own EEPROM area just below the pet (config.h).
#define MACRO_NAME_LEN 12
#define MACRO_NAME_COUNT 16
void macro_name_get(uint8_t i, char out[MACRO_NAME_LEN]); // not NUL-terminated when 12 long
bool macro_name_set(uint8_t i, const char *in);
// Raw HID 0xF6 0x21: [2] 0 get / 1 set, [3] index, [4..15] name -> [2] 0xA4, [3] index, [4..15] name, [16] done.
void macro_name_raw_hid(uint8_t *data, bool allowed);
