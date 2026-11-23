// Copyright 2026 Brett-D
// SPDX-License-Identifier: GPL-2.0-or-later
//
// OLED status for the right half (the only OLED on this board). The right half
// is the split target, so layer, LED, mod and activity state are synced to it
// (see config.h). The left half has no OLED and renders nothing.

#ifdef OLED_ENABLE

#    include "brettssofle.h"

#    ifndef OLED_SLEEP_TIMEOUT_MS
#        define OLED_SLEEP_TIMEOUT_MS 60000
#    endif

static void render_default_layer(void) {
    switch (get_highest_layer(default_layer_state)) {
        case _QWERTY:
            oled_write_P(PSTR("Qwrt "), false);
            break;
        case _COLEMAK:
            oled_write_P(PSTR("Clmk "), false);
            break;
        default:
            oled_write_P(PSTR("Mod  "), false);
            break;
    }
}

static void render_layer(void) {
    oled_write_P(PSTR("LAYER"), false);
    switch (get_highest_layer(layer_state | default_layer_state)) {
        case _QWERTY:
        case _COLEMAK:
            oled_write_P(PSTR("Base "), false);
            break;
        case _LOWER:
            oled_write_P(PSTR("Lower"), false);
            break;
        case _RAISE:
            oled_write_P(PSTR("Raise"), false);
            break;
        case _MOUSE:
            oled_write_P(PSTR("Mouse"), false);
            break;
        default:
            oled_write_P(PSTR("Undef"), false);
            break;
    }
}

static void render_mods(void) {
    uint8_t mods = get_mods() | get_oneshot_mods();
    oled_write_P(PSTR("S"), mods & MOD_MASK_SHIFT);
    oled_write_P(PSTR("C"), mods & MOD_MASK_CTRL);
    oled_write_P(PSTR("A"), mods & MOD_MASK_ALT);
    oled_write_P(PSTR("G"), mods & MOD_MASK_GUI);
    oled_write_P(PSTR(" "), false);
}

static void print_status_narrow(void) {
    oled_write_P(PSTR("\n\n"), false);
    render_default_layer();
    oled_write_P(PSTR("\n\n"), false);
    render_layer();
    oled_write_P(PSTR("\n\n"), false);
    led_t led_state = host_keyboard_led_state();
    oled_write_P(PSTR("CPSLK"), led_state.caps_lock);
    oled_write_P(PSTR("NUMLK"), led_state.num_lock);
    oled_write_P(PSTR("\n"), false);
    render_mods();
}

void keyboard_post_init_user(void) {
    // sofle.c only rotates the master's OLED (and ignores oled_init_user), so
    // re-initialise the right half's OLED in portrait for the narrow status.
    if (!is_keyboard_master()) {
        oled_init(OLED_ROTATION_270);
    }
}

bool oled_task_user(void) {
    if (is_keyboard_master()) {
        return false;
    }

    // Activity is synced from the master, so typing on either half or using
    // the trackpad keeps the screen on.
    if (last_input_activity_elapsed() > OLED_SLEEP_TIMEOUT_MS) {
        oled_off();
        return false;
    }
    oled_on();

    print_status_narrow();
    return false;
}

#endif
