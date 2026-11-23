// Copyright 2026 Brett-D
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include QMK_KEYBOARD_H

enum layers {
    _QWERTY,
    _COLEMAK,
    _LOWER,
    _RAISE,
    _MOUSE, // must match AUTO_MOUSE_DEFAULT_LAYER
};

// QK_KB_n so Vial shows them by name (order must match vial.json customKeycodes).
enum custom_keycodes {
    CYCLE = QK_KB_0, // cycle the default layer between QWERTY and Colemak-DH
    DRAG_SCROLL,     // hold: trackpad movement scrolls instead of moving the cursor
    CONFIG_MODE,     // toggle config mode (sofle-config.html)
};

// Number of default layers CYCLE rotates through.
#define BASE_LAYERS 2

void trackpad_set_drag_scroll(bool enable);
#ifdef OLED_ENABLE
void oled_right_init(void);
#endif

#ifdef POINTING_DEVICE_ENABLE
typedef struct {
    uint8_t  z;         // live Z while touching (0-63)
    uint8_t  peak;      // highest Z of the current/last touch
    uint8_t  last_peak; // peak of the previous completed touch
    bool     touching;
    bool     pressed;   // press-to-click is holding the left button
    uint16_t touches;   // touch counter, so a new touch is visible
} trackpad_z_t;

const trackpad_z_t *trackpad_z_get(void);
uint8_t             trackpad_z_update(bool touch);
void                trackpad_z_set_pressed(bool pressed);
void                trackpad_z_post_init(void);
void                trackpad_z_sync_task(void);
bool                trackpad_press_enabled(void);
uint8_t             trackpad_press_threshold(void);
uint8_t             trackpad_press_release_z(void);
#endif

// Underglow idle timeout choice (0 = never, 1/2/5/10/30 min), saved in
// EECONFIG_USER bits 11-13 (trackpad_z.c).
uint8_t  user_config_rgb_idle_idx(void);
void     user_config_set_rgb_idle_idx(uint8_t idx);
uint32_t user_config_rgb_idle_ms(void);

#include "config_mode.h"
