// Copyright 2026 Brett-D
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

/* ---------------------------------------------------------------------------
 * Vial
 * ------------------------------------------------------------------------ */
#define VIAL_KEYBOARD_UID { 0x21, 0x82, 0xA8, 0xF1, 0xCC, 0x55, 0x7D, 0x98 }
// Hold the four corner keys (` / ` top row and both Shifts) to unlock Vial.
#define VIAL_UNLOCK_COMBO_ROWS { 0, 5, 3, 8 }
#define VIAL_UNLOCK_COMBO_COLS { 0, 0, 0, 0 }

// QWERTY, Colemak-DH, Lower, Raise, Mouse + one spare layer for Vial.
#define DYNAMIC_KEYMAP_LAYER_COUNT 6

/* ---------------------------------------------------------------------------
 * Controller: Sea-Picro EXT (RP2040)
 * ------------------------------------------------------------------------ */
// Flash chip on the Sea-Picro EXT V1.0 (GD25Q64, per its BOM and docs).
#define RP2040_FLASH_GD25Q64CS

/* ---------------------------------------------------------------------------
 * Split
 * ------------------------------------------------------------------------ */
// USB always goes into the left half (it has the trackpad).
#define MASTER_LEFT

// The right half drives the only OLED, so it needs the master's state.
#define SPLIT_LAYER_STATE_ENABLE
#define SPLIT_LED_STATE_ENABLE
#define SPLIT_MODS_ENABLE
#define SPLIT_ACTIVITY_ENABLE
// keyhive/keyboard.json syncs the OLED on/off state from the master. The left
// half has no OLED (its init fails, so it reports "off"), which kept the right
// OLED switched off. The right half manages its own timeout instead (oled.c).
#undef SPLIT_OLED_ENABLE

/* ---------------------------------------------------------------------------
 * OLED (right half only)
 * ------------------------------------------------------------------------ */
// The stock timeout only sees the right half's own keys; oled.c uses the
// synced activity timestamp so typing on the left / trackpad keeps it on.
#define OLED_TIMEOUT 0
#define OLED_SLEEP_TIMEOUT_MS 60000
#define OLED_UPDATE_INTERVAL 50
#define OLED_BRIGHTNESS 128

/* ---------------------------------------------------------------------------
 * I2C (Cirque on the left, OLED on the right; both on GP2 SDA / GP3 SCL)
 * ------------------------------------------------------------------------ */
// Drop to 100000 if the trackpad ever stops responding (long/noisy wiring).
#define I2C1_CLOCK_SPEED 400000

/* ---------------------------------------------------------------------------
 * Cirque Pinnacle trackpad (23 mm, left half)
 * ------------------------------------------------------------------------ */
#define SPLIT_POINTING_ENABLE
#define POINTING_DEVICE_LEFT
#define POINTING_DEVICE_ROTATION_90
#define CIRQUE_PINNACLE_DIAMETER_MM 23
#define POINTING_DEVICE_GESTURES_CURSOR_GLIDE_ENABLE
#define POINTING_DEVICE_GESTURES_SCROLL_ENABLE // circular scroll around the edge
// Taps are handled by trackpad.c (tap, tap-and-drag, long-press right click),
// so the driver's immediate-click tap stays off.
#undef CIRQUE_PINNACLE_TAP_ENABLE

// Touching the pad turns on the _MOUSE layer (mouse buttons on the right hand).
#define POINTING_DEVICE_AUTO_MOUSE_ENABLE
#define AUTO_MOUSE_DEFAULT_LAYER 4
#define AUTO_MOUSE_TIME 650

// Gesture timing (ms) / movement threshold (counts). See trackpad.c.
#define TRACKPAD_TAP_TERM 200
#define TRACKPAD_TAP_DRAG_TERM 200
#define TRACKPAD_LONG_PRESS_TERM 500
#define TRACKPAD_DRAG_RELEASE_DELAY 300
#define TRACKPAD_MOVE_THRESHOLD 6
#define TRACKPAD_DRAG_SCROLL_DIVISOR 8

/* ---------------------------------------------------------------------------
 * RGB underglow
 * ------------------------------------------------------------------------ */
#ifdef RGBLIGHT_ENABLE
#    define RGBLIGHT_DEFAULT_MODE (RGBLIGHT_MODE_RAINBOW_SWIRL + 2)
#    define RGBLIGHT_EFFECT_ALTERNATING
#    define RGBLIGHT_EFFECT_RGB_TEST
#    define RGBLIGHT_EFFECT_RAINBOW_MOOD
#    define RGBLIGHT_EFFECT_STATIC_GRADIENT
#    define RGBLIGHT_EFFECT_RAINBOW_SWIRL
#    define RGBLIGHT_EFFECT_BREATHING
#    define RGBLIGHT_EFFECT_SNAKE
// Lights turn off after this much idle time (any key, encoder or trackpad).
#    define RGB_IDLE_TIMEOUT_MS 60000
#endif
