// Copyright 2026 Brett-D
// SPDX-License-Identifier: GPL-2.0-or-later
//
// EECONFIG_KB (kb_config.h): every field a code with 0 = its default.
//   bits 0-2   OLED brightness level - 1, xor 2 (default level 3)
//   bits 3-5   OLED sleep index, xor 1 (default 1 min)
//   bit  6     OLED off
//   bits 7-9   key-press effects at once - 1, xor 3 (default 4)
//   bits 10-12 screensaver after (index, 0 = off)
//   bits 13-16 screensaver effect (our own backgrounds)
//   bit  17    pet LED reactions NOT over the screensaver
//   bits 18-22 screensaver hue / 8
//   bits 23-25 screensaver saturation level
//   bits 26-28 screensaver brightness level
//   bits 29-30 screensaver speed level

#include QMK_KEYBOARD_H
#include "kb_config.h"
#include "perf.h"
#include "eeprom.h"
#include "nvm_eeprom_eeconfig_internal.h" // EECONFIG_KEYBOARD

// The 32-bit keyboard word is always in QMK's EEPROM layout (and zeroed by an
// EEPROM reset), but QMK only offers eeconfig_read/update_kb() to keyboards
// without a datablock; this keymap uses the datablock, so it goes direct.
static uint32_t kb_read(void) {
    return eeprom_read_dword(EECONFIG_KEYBOARD);
}
static void kb_write(uint32_t v) {
    eeprom_update_dword(EECONFIG_KEYBOARD, v);
}

#include "led_fx_core.h" // FX_OWN_BG_COUNT

static const uint8_t  ss_sat[SS_SAT_LEVELS]     = {255, 219, 182, 146, 109, 73, 36, 0};
static const uint8_t  ss_val[SS_VAL_LEVELS]     = {40, 10, 20, 30, 50, 60, 80, 100};
static const uint8_t  ss_speed[SS_SPEED_LEVELS] = {128, 64, 192, 255};
static const uint16_t ss_after[SS_AFTER_CHOICES] = {0, 1, 2, 5, 10, 15, 30};

uint8_t ss_sat_value(uint8_t level) {
    return ss_sat[level < SS_SAT_LEVELS ? level : 0];
}
uint8_t ss_val_percent(uint8_t level) {
    return ss_val[level < SS_VAL_LEVELS ? level : 0];
}
uint8_t ss_speed_value(uint8_t level) {
    return ss_speed[level < SS_SPEED_LEVELS ? level : 0];
}
uint16_t ss_after_minutes(uint8_t idx) {
    return idx < SS_AFTER_CHOICES ? ss_after[idx] : 0;
}

#define FIELD(raw, lo, bits) (((raw) >> (lo)) & ((1u << (bits)) - 1))

void kb_config_unpack(uint32_t raw, kb_config_t *c) {
    uint8_t b      = FIELD(raw, 0, 3) ^ (OLED_BRIGHT_DEFAULT - 1);
    c->oled_bright = b < OLED_BRIGHT_LEVELS ? b + 1 : OLED_BRIGHT_DEFAULT;
    uint8_t s      = FIELD(raw, 3, 3) ^ OLED_SLEEP_DEFAULT;
    c->oled_sleep  = s < OLED_SLEEP_CHOICES ? s : OLED_SLEEP_DEFAULT;
    c->oled_off    = FIELD(raw, 6, 1);
    c->fx_max      = (FIELD(raw, 7, 3) ^ (FX_MAX_DEFAULT - 1)) + 1;
    uint8_t a      = FIELD(raw, 10, 3);
    c->ss_after    = a < SS_AFTER_CHOICES ? a : 0;
    uint8_t e      = FIELD(raw, 13, 4);
    c->ss_effect   = e < FX_OWN_BG_COUNT ? e : 0;
    c->ss_pet      = !FIELD(raw, 17, 1);
    c->ss_hue      = FIELD(raw, 18, 5) * SS_HUE_STEP;
    c->ss_sat      = FIELD(raw, 23, 3);
    c->ss_val      = FIELD(raw, 26, 3);
    c->ss_speed    = FIELD(raw, 29, 2);
}

uint32_t kb_config_pack(const kb_config_t *c) {
    uint8_t b = c->oled_bright >= 1 && c->oled_bright <= OLED_BRIGHT_LEVELS ? c->oled_bright : OLED_BRIGHT_DEFAULT;
    uint8_t m = c->fx_max >= 1 && c->fx_max <= 8 ? c->fx_max : FX_MAX_DEFAULT;
    return (uint32_t)((b - 1) ^ (OLED_BRIGHT_DEFAULT - 1)) | (uint32_t)((c->oled_sleep < OLED_SLEEP_CHOICES ? c->oled_sleep : OLED_SLEEP_DEFAULT) ^ OLED_SLEEP_DEFAULT) << 3 |
           (uint32_t)(c->oled_off ? 1 : 0) << 6 | (uint32_t)((m - 1) ^ (FX_MAX_DEFAULT - 1)) << 7 | (uint32_t)(c->ss_after < SS_AFTER_CHOICES ? c->ss_after : 0) << 10 |
           (uint32_t)(c->ss_effect < FX_OWN_BG_COUNT ? c->ss_effect : 0) << 13 | (uint32_t)(c->ss_pet ? 0 : 1) << 17 | (uint32_t)(c->ss_hue / SS_HUE_STEP & 31) << 18 |
           (uint32_t)(c->ss_sat & 7) << 23 | (uint32_t)(c->ss_val & 7) << 26 | (uint32_t)(c->ss_speed & 3) << 29;
}

static kb_config_t cache;
static bool        loaded;

void kb_config_get(kb_config_t *c) {
    if (!loaded) {
        kb_config_unpack(kb_read(), &cache);
        loaded = true;
    }
    *c = cache;
}

void kb_config_set(const kb_config_t *c) {
    uint32_t raw = kb_config_pack(c);
    kb_config_unpack(raw, &cache); // what was stored (out-of-range values back to defaults)
    loaded = true;
    if (kb_read() != raw) {
        PERF_SPAN(PF_FLASH, kb_write(raw));
    }
}
