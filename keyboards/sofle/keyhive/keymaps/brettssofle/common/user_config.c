// Copyright 2026 Brett-D
// SPDX-License-Identifier: GPL-2.0-or-later
//
// EECONFIG_USER layout (shared with the config page settings):
//   bits 0-3   version
//   bit  4     press-to-click enabled
//   bits 5-10  press-to-click Z threshold (default 51)
//   bits 11-13 RGB idle timeout index (1 = 1 min)
//   bits 14-19 press-to-click release Z
//   bits 20-21 LED zones (0 effect on keys + underglow, 1 keys + solid
//              underglow, 2 keys only)
//   bits 22-27 solid underglow hue / 4
//   bits 28-31 solid underglow saturation / 17

#include QMK_KEYBOARD_H
#include "user_config.h"

#define USER_CONFIG_VERSION 3 // 2: hysteresis replaced by an absolute release Z; 3: LED zones in bits 20-31
#define RGB_IDLE_CHOICES 6

#ifndef TRACKPAD_PRESS_DEFAULT_ENABLED
#    define TRACKPAD_PRESS_DEFAULT_ENABLED 1
#endif
#ifndef TRACKPAD_PRESS_DEFAULT_THRESHOLD
#    define TRACKPAD_PRESS_DEFAULT_THRESHOLD 51
#endif
#ifndef TRACKPAD_PRESS_DEFAULT_RELEASE_Z
#    define TRACKPAD_PRESS_DEFAULT_RELEASE_Z 24
#endif
#ifndef LED_ZONE_DEFAULT_UG_HUE
#    define LED_ZONE_DEFAULT_UG_HUE 42 // x4 = 168, blue
#endif

typedef union {
    uint32_t raw;
    struct {
        uint32_t version : 4;
        uint32_t press_enabled : 1;
        uint32_t press_threshold : 6;
        uint32_t rgb_idle_timeout_idx : 3;
        uint32_t press_release_z : 6;
        uint32_t led_zone : 2;
        uint32_t ug_hue : 6;
        uint32_t ug_sat : 4;
    };
} user_config_t;
_Static_assert(sizeof(user_config_t) == sizeof(uint32_t), "user_config_t must fit EECONFIG_USER");

static user_config_t uc;

static void led_zone_set_defaults(void) {
    uc.led_zone = 0;
    uc.ug_hue   = LED_ZONE_DEFAULT_UG_HUE;
    uc.ug_sat   = 15;
}

static void set_defaults(void) {
    uc.raw                  = 0;
    uc.version              = USER_CONFIG_VERSION;
    uc.press_enabled        = TRACKPAD_PRESS_DEFAULT_ENABLED;
    uc.press_threshold      = TRACKPAD_PRESS_DEFAULT_THRESHOLD;
    uc.press_release_z      = TRACKPAD_PRESS_DEFAULT_RELEASE_Z;
    uc.rgb_idle_timeout_idx = 1;
    led_zone_set_defaults();
}

static bool valid(void) {
    return uc.version == USER_CONFIG_VERSION && uc.press_threshold >= 1 && uc.press_release_z < uc.press_threshold;
}

void eeconfig_init_user(void) {
    set_defaults();
    eeconfig_update_user(uc.raw);
}

void user_config_init(void) {
    uc.raw = eeconfig_read_user();
    if (uc.version == 2) {
        // v2 -> v3: bits 20-31 were reserved (zero); keep everything else.
        uc.version = USER_CONFIG_VERSION;
        led_zone_set_defaults();
        if (valid()) {
            eeconfig_update_user(uc.raw);
        }
    }
    if (!valid()) {
        eeconfig_init_user();
    }
}

bool trackpad_press_enabled(void) {
    return uc.press_enabled;
}
uint8_t trackpad_press_threshold(void) {
    return uc.press_threshold;
}
uint8_t trackpad_press_release_z(void) {
    return uc.press_release_z;
}

bool user_config_set_press(bool enabled, uint8_t threshold, uint8_t release_z) {
    if (threshold > 63 || release_z > 63) {
        return false;
    }
    user_config_t old  = uc;
    uc.press_enabled   = enabled ? 1 : 0;
    uc.press_threshold = threshold;
    uc.press_release_z = release_z;
    if (!valid()) {
        uc = old;
        return false;
    }
    if (uc.raw != old.raw) {
        eeconfig_update_user(uc.raw);
    }
    return true;
}

uint8_t user_config_rgb_idle_idx(void) {
    return uc.rgb_idle_timeout_idx < RGB_IDLE_CHOICES ? uc.rgb_idle_timeout_idx : 1;
}

void user_config_set_rgb_idle_idx(uint8_t idx) {
    if (idx < RGB_IDLE_CHOICES && idx != uc.rgb_idle_timeout_idx) {
        uc.rgb_idle_timeout_idx = idx;
        eeconfig_update_user(uc.raw);
    }
}

uint32_t user_config_rgb_idle_ms(void) {
    static const uint8_t minutes[RGB_IDLE_CHOICES] = {0, 1, 2, 5, 10, 30};
    return minutes[user_config_rgb_idle_idx()] * 60000UL;
}

void user_config_get_led_zone(uint8_t *zone, uint8_t *hue, uint8_t *sat) {
    *zone = uc.led_zone < 3 ? uc.led_zone : 0;
    *hue  = uc.ug_hue * 4;
    *sat  = uc.ug_sat * 17;
}

void user_config_set_led_zone(uint8_t zone, uint8_t hue, uint8_t sat) {
    user_config_t old = uc;
    uc.led_zone       = zone < 3 ? zone : 0;
    uc.ug_hue         = (hue + 2) / 4 > 63 ? 63 : (hue + 2) / 4;
    uc.ug_sat         = (sat + 8) / 17;
    if (uc.raw != old.raw) {
        eeconfig_update_user(uc.raw);
    }
}
