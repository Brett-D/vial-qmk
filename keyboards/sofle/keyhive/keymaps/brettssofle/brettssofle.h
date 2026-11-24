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
