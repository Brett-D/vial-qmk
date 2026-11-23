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
// Left at the 100 kHz default: 400 kHz made the trackpad unreliable on this
// board, and the resulting I2C timeouts stalled the left half's main loop
// (choppy cursor + stuttering left-half LEDs).
// Fail fast on a bad transfer instead of the driver's 20 ms default, so an
// I2C hiccup can't freeze the left half. A normal read takes ~1 ms.
#define CIRQUE_PINNACLE_TIMEOUT 5

/* ---------------------------------------------------------------------------
 * Cirque Pinnacle trackpad (23 mm, left half)
 * ------------------------------------------------------------------------ */
#define SPLIT_POINTING_ENABLE
#define POINTING_DEVICE_LEFT
#define POINTING_DEVICE_ROTATION_90
#define CIRQUE_PINNACLE_DIAMETER_MM 23
#define POINTING_DEVICE_GESTURES_CURSOR_GLIDE_ENABLE
#define POINTING_DEVICE_GESTURES_SCROLL_ENABLE // circular scroll around the edge
// Taps are handled by trackpad.c, which ignores taps that moved or scrolled,
// so the driver's tap (which clicks on any short touch) stays off.
#undef CIRQUE_PINNACLE_TAP_ENABLE

// Touching the pad turns on the _MOUSE layer: T = left button (hold to drag),
// R = right button. It stays on while a mouse key is held and turns off
// AUTO_MOUSE_TIME after the finger lifts, or at once when any other key is
// pressed.
#define POINTING_DEVICE_AUTO_MOUSE_ENABLE
#define AUTO_MOUSE_DEFAULT_LAYER 4
#define AUTO_MOUSE_TIME 650

// Gesture timing (ms) / movement threshold (counts). See trackpad.c.
#define TRACKPAD_TAP_TERM 200
#define TRACKPAD_MOVE_THRESHOLD 6
#define TRACKPAD_DRAG_SCROLL_DIVISOR 8
// Touch and hold still for TRACKPAD_LONG_PRESS_TERM = right click. Off by
// default: a slow ring scroll looks like a still finger and would trigger it.
// #define TRACKPAD_LONG_PRESS_RIGHT_CLICK
#define TRACKPAD_LONG_PRESS_TERM 500

// Experiment: show the trackpad's Z (signal strength, 0-63) live on the right
// OLED, to see whether a light touch and a firm press are distinguishable.
// trackpad_z.html works without it (raw HID). Off by default.
// #define TRACKPAD_Z_DEBUG
// RPC_ID_USER_CFG mirrors config mode to the right OLED (config_mode.c).
#ifdef TRACKPAD_Z_DEBUG
#    define SPLIT_TRANSACTION_IDS_USER RPC_ID_USER_TP_Z, RPC_ID_USER_CFG
#else
#    define SPLIT_TRANSACTION_IDS_USER RPC_ID_USER_CFG
#endif

// Press-to-click (latching): Z crossing the threshold for TRACKPAD_PRESS_DEBOUNCE
// polls presses the left button; it stays held at normal touch pressure (for
// dragging) until the finger lifts or Z drops below the release Z. Enable,
// threshold and release Z are saved in EEPROM and can be changed live in
// trackpad_z.html; these are the defaults.
#define TRACKPAD_PRESS_DEFAULT_ENABLED 1
#define TRACKPAD_PRESS_DEFAULT_THRESHOLD 51  // Brett's light touches peaked at 36-49
#define TRACKPAD_PRESS_DEFAULT_RELEASE_Z 24  // well below light-touch levels; lifting always releases
#define TRACKPAD_PRESS_DEBOUNCE 2            // polls (~10 ms each)
#define TRACKPAD_PRESS_MIN_MS 30             // shortest click before Z can release it
#define TRACKPAD_PRESS_FREEZE_MS 60          // hold the cursor still after press/release

/* ---------------------------------------------------------------------------
 * RGB underglow
 * ------------------------------------------------------------------------ */
#ifdef RGBLIGHT_ENABLE
// Each half drives only its own 37 LEDs (keyhive split_count); see rgb_driver.c.
#    define WS2812_LED_COUNT 37
#    define RGBLIGHT_DEFAULT_MODE (RGBLIGHT_MODE_RAINBOW_SWIRL + 2)
#    define RGBLIGHT_EFFECT_ALTERNATING
#    define RGBLIGHT_EFFECT_RGB_TEST
#    define RGBLIGHT_EFFECT_RAINBOW_MOOD
#    define RGBLIGHT_EFFECT_STATIC_GRADIENT
#    define RGBLIGHT_EFFECT_RAINBOW_SWIRL
#    define RGBLIGHT_EFFECT_BREATHING
#    define RGBLIGHT_EFFECT_SNAKE
// Lights turn off after a chosen idle time (any key, encoder or trackpad):
// never / 1 / 2 / 5 / 10 / 30 min, default 1 min. Chosen in config mode (LEDs
// tab) and saved in EEPROM; see user_config_rgb_idle_ms() in trackpad_z.c.
#endif
