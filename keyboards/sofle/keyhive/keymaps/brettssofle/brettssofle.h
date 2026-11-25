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
    // v28 built-in macros (common/macros.c), in vial.json's order; labels and
    // tooltips come from vial.json.
    BM_DEL_LINE,
    BM_SEL_LINE,
    BM_DUP_LINE,
    BM_DEL_WORD_L,
    BM_DEL_WORD_R,
    BM_LINE_BELOW,
    BM_LINE_ABOVE,
    BM_PASTE_PLAIN,
    BM_SCREENSHOT,
    BM_LOCK,
    BM_DESKTOP,
    BM_TASKMGR,
    BM_UNDO,
    BM_REDO,
    BM_EMOJI,
    BM_CLIPBOARD,
};

// Number of default layers CYCLE rotates through.
#define BASE_LAYERS 2
