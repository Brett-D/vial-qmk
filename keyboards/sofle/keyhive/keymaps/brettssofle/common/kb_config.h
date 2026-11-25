// Copyright 2026 Brett-D
// SPDX-License-Identifier: GPL-2.0-or-later
//
// v27 settings in EECONFIG_KEYBOARD (32 bits QMK keeps for the keyboard, unused
// by the sofle code): the right OLED's display settings, the key-press effect cap and
// the LED screensaver. Every field is stored as a code where 0 is its default,
// so an EEPROM reset (EECONFIG_KB = 0) or an older firmware that never wrote it
// gives the defaults, and the stable EEPROM layout doesn't move. Master only.

#pragma once

#include <stdbool.h>
#include <stdint.h>

#define OLED_BRIGHT_LEVELS 5 // 1-5 (oled display.c: 16, 64, 128, 192, 255)
#define OLED_BRIGHT_DEFAULT 3
#define OLED_SLEEP_CHOICES 6 // 30 s, 1, 2, 5, 10, 30 min
#define OLED_SLEEP_DEFAULT 1
#define FX_MAX_DEFAULT 4      // key-press effects at once, 1-8
#define SS_AFTER_CHOICES 7    // Off, 1, 2, 5, 10, 15, 30 min
#define SS_HUE_STEP 8         // the hue is kept in 32 steps
#define SS_SAT_LEVELS 8       // 100 % .. 0 %
#define SS_VAL_LEVELS 8       // % of the maximum brightness: 40 (default), 10, 20, 30, 50, 60, 80, 100
#define SS_SPEED_LEVELS 4     // 128 (default), 64, 192, 255

typedef struct {
    uint8_t oled_bright; // 1-OLED_BRIGHT_LEVELS
    uint8_t oled_sleep;  // index (0 = 30 s)
    bool    oled_off;
    uint8_t fx_max;      // 1-8
    uint8_t ss_after;    // index (0 = no screensaver)
    uint8_t ss_effect;   // our own background, 0-9 (led_fx_table.h order)
    bool    ss_pet;      // pet LED reactions still play over it
    uint8_t ss_hue;      // multiple of SS_HUE_STEP
    uint8_t ss_sat;      // level index (0 = 100 %)
    uint8_t ss_val;      // level index (0 = 40 %)
    uint8_t ss_speed;    // level index (0 = 128)
} kb_config_t;

void kb_config_get(kb_config_t *c);
void kb_config_set(const kb_config_t *c); // saves (only when it changed)
void kb_config_migrate(void);              // v29b, keyboard_pre_init_user: out of EECONFIG_KEYBOARD (QMK's datablock version)
uint32_t kb_config_pack(const kb_config_t *c);
void     kb_config_unpack(uint32_t raw, kb_config_t *c);

uint8_t ss_sat_value(uint8_t level);   // 0-255
uint8_t ss_val_percent(uint8_t level); // 10-100
uint8_t ss_speed_value(uint8_t level); // 0-255
uint16_t ss_after_minutes(uint8_t idx); // 0 = off
