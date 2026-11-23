// Copyright 2026 Brett-D
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include QMK_KEYBOARD_H

enum layers {
    _QWERTY,
    _COLEMAK,
    _LOWER,
    _RAISE,
    _MOUSE,  // must match AUTO_MOUSE_DEFAULT_LAYER
    _ADJUST, // Lower + Raise held together (tri-layer, keymap.c)
};

// QK_KB_n so Vial shows them by name (order must match vial.json customKeycodes).
enum custom_keycodes {
    CYCLE = QK_KB_0, // cycle the default layer between QWERTY and Colemak-DH
    DRAG_SCROLL,     // hold: trackpad movement scrolls instead of moving the cursor
    CONFIG_MODE,     // toggle config mode (sofle-config.html)
    OLED_PAGE,       // next right-OLED page (status / trackpad / LEDs / info)
    WIGGLE,          // mouse wiggler on/off (wiggler.c)
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
// LED zones (0 effect everywhere, 1 keys + solid underglow, 2 keys only) and the
// solid underglow colour, saved in EECONFIG_USER bits 20-31 (trackpad_z.c).
void user_config_get_led_zone(uint8_t *zone, uint8_t *hue, uint8_t *sat);
void user_config_set_led_zone(uint8_t zone, uint8_t hue, uint8_t sat);

// Mouse wiggler (wiggler.c): intervals 30 s / 1 / 2 / 4 min without input.
#define WIGGLE_INTERVALS 4
bool    wiggler_on(void);
uint8_t wiggler_interval(void);
void    wiggler_set(bool enable, uint8_t interval);
void    wiggler_toggle(void);
void    wiggler_task(void); // housekeeping, master

// Right OLED pages (oled_pages.c).
enum oled_pages { OLED_PAGE_STATUS, OLED_PAGE_TRACKPAD, OLED_PAGE_LEDS, OLED_PAGE_INFO, OLED_PAGE_COUNT };
void oled_pages_init(void); // keyboard_post_init_user, both halves
void oled_page_next(void);  // master
void oled_pages_task(void); // housekeeping, master syncs the right half
#ifdef OLED_ENABLE
bool oled_pages_render(void); // right half: false = status page
void oled_pages_status_footer(void);
#endif

#include "config_mode.h"
