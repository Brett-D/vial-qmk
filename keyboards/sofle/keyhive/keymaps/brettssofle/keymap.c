/* Copyright 2020 Josef Adamcik
 * Modification for VIA support and RGB underglow by Jens Bonk-Wiltfang
 * Vial / RP2040 / Cirque trackpad port by Brett-D
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "brettssofle.h"
#include "led_layers.h"

// Default keymap. Everything here can be changed live in Vial.
const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {
/*
 * QWERTY
 * ,-----------------------------------------.                    ,-----------------------------------------.
 * |  `   |   1  |   2  |   3  |   4  |   5  |                    |   6  |   7  |   8  |   9  |   0  |  `   |
 * |------+------+------+------+------+------|                    |------+------+------+------+------+------|
 * | ESC  |   Q  |   W  |   E  |   R  |   T  |                    |   Y  |   U  |   I  |   O  |   P  | Bspc |
 * |------+------+------+------+------+------|                    |------+------+------+------+------+------|
 * | Tab  |   A  |   S  |   D  |   F  |   G  |-------.    ,-------|   H  |   J  |   K  |   L  |   ;  |  '   |
 * |------+------+------+------+------+------| Mute  |    |       |------+------+------+------+------+------|
 * |LShift|   Z  |   X  |   C  |   V  |   B  |-------|    |-------|   N  |   M  |   ,  |   .  |   /  |RShift|
 * `-----------------------------------------/       /     \      \-----------------------------------------'
 *            | LGUI | LAlt | LCTR |LOWER | /Space  /       \Enter \  |RAISE | RCTR | RAlt | RGUI |
 *            `-----------------------------------'           '------''---------------------------'
 */
[_QWERTY] = LAYOUT(
   KC_GRV, KC_1   , KC_2   , KC_3   , KC_4   , KC_5   ,                         KC_6    , KC_7   , KC_8   , KC_9   , KC_0   , KC_GRV ,
   KC_ESC, KC_Q   , KC_W   , KC_E   , KC_R   , KC_T   ,                         KC_Y    , KC_U   , KC_I   , KC_O   , KC_P   , KC_BSPC,
   KC_TAB, KC_A   , KC_S   , KC_D   , KC_F   , KC_G   ,                         KC_H    , KC_J   , KC_K   , KC_L   , KC_SCLN, KC_QUOT,
  KC_LSFT, KC_Z   , KC_X   , KC_C   , KC_V   , KC_B   , KC_MUTE, OLED_PAGE,     KC_N    , KC_M   , KC_COMM, KC_DOT , KC_SLSH, KC_RSFT,
                  KC_LGUI, KC_LALT, KC_LCTL, MO(_LOWER), KC_SPC ,         KC_ENT  , MO(_RAISE), KC_RCTL, KC_RALT, KC_RGUI
),
/*
 * COLEMAK-DH
 * ,-----------------------------------------.                    ,-----------------------------------------.
 * |  `   |   1  |   2  |   3  |   4  |   5  |                    |   6  |   7  |   8  |   9  |   0  |  `   |
 * |------+------+------+------+------+------|                    |------+------+------+------+------+------|
 * | ESC  |   Q  |   W  |   F  |   P  |   B  |                    |   J  |   L  |   U  |   Y  |   ;  | Bspc |
 * |------+------+------+------+------+------|                    |------+------+------+------+------+------|
 * | TAB  |   A  |   R  |   S  |   T  |   G  |-------.    ,-------|   M  |   N  |   E  |   I  |   O  |  '   |
 * |------+------+------+------+------+------|       |    |       |------+------+------+------+------+------|
 * |LShift|   Z  |   X  |   C  |   D  |   V  |-------|    |-------|   K  |   H  |   ,  |   .  |   /  |RShift|
 * `-----------------------------------------/       /     \      \-----------------------------------------'
 *            | LGUI | LAlt | LCTR |LOWER | /Enter  /       \Space \  |RAISE | RCTR | RAlt | RGUI |
 *            `-----------------------------------'           '------''---------------------------'
 */
[_COLEMAK] = LAYOUT(
  KC_GRV , KC_1   , KC_2   , KC_3   , KC_4   , KC_5   ,                          KC_6    , KC_7   , KC_8   , KC_9   , KC_0   , KC_GRV ,
  KC_ESC , KC_Q   , KC_W   , KC_F   , KC_P   , KC_B   ,                          KC_J    , KC_L   , KC_U   , KC_Y   , KC_SCLN, KC_BSPC,
  KC_TAB , KC_A   , KC_R   , KC_S   , KC_T   , KC_G   ,                          KC_M    , KC_N   , KC_E   , KC_I   , KC_O   , KC_QUOT,
  KC_LSFT, KC_Z   , KC_X   , KC_C   , KC_D   , KC_V   , _______, _______,        KC_K    , KC_H   , KC_COMM, KC_DOT , KC_SLSH, KC_RSFT,
                  KC_LGUI, KC_LALT, KC_LCTL, MO(_LOWER), KC_ENT ,         KC_SPC  , MO(_RAISE), KC_RCTL, KC_RALT, KC_RGUI
),
/* LOWER
 * ,-----------------------------------------.                    ,-----------------------------------------.
 * |      |  F1  |  F2  |  F3  |  F4  |  F5  |                    |  F6  |  F7  |  F8  |  F9  | F10  | F11  |
 * |------+------+------+------+------+------|                    |------+------+------+------+------+------|
 * |  `   |   1  |   2  |   3  |   4  |   5  |                    |   6  |   7  |   8  |   9  |   0  | F12  |
 * |------+------+------+------+------+------|                    |------+------+------+------+------+------|
 * |      |   !  |   @  |   #  |   $  |   %  |-------.    ,-------|   ^  |   &  |   *  |   (  |   )  |   |  |
 * |------+------+------+------+------+------|       |    |       |------+------+------+------+------+------|
 * |      |  =   |  -   |  +   |   {  |   }  |-------|    |-------|   [  |   ]  |   ;  |   :  |   \  |      |
 * `-----------------------------------------/       /     \      \-----------------------------------------'
 */
[_LOWER] = LAYOUT(
  _______, KC_F1  , KC_F2  , KC_F3  , KC_F4  , KC_F5  ,                         KC_F6  , KC_F7  , KC_F8  , KC_F9  , KC_F10 , KC_F11 ,
  KC_GRV , KC_1   , KC_2   , KC_3   , KC_4   , KC_5   ,                         KC_6   , KC_7   , KC_8   , KC_9   , KC_0   , KC_F12 ,
  _______, KC_EXLM, KC_AT  , KC_HASH, KC_DLR , KC_PERC,                         KC_CIRC, KC_AMPR, KC_ASTR, KC_LPRN, KC_RPRN, KC_PIPE,
  _______, KC_EQL , KC_MINS, KC_PLUS, KC_LCBR, KC_RCBR, _______,    _______,    KC_LBRC, KC_RBRC, KC_SCLN, KC_COLN, KC_BSLS, _______,
                  _______, _______, _______, _______, _______,           _______, _______, _______, _______, _______
),
/* RAISE
 * ,-----------------------------------------.                    ,-----------------------------------------.
* |Cycle |      |      |      |      |      |                    |      |      |      |      |      |Config|
 * |------+------+------+------+------+------|                    |------+------+------+------+------+------|
 * |      | Ins  | Pscr | Menu |      |      |                    | PgUp |      |  Up  |      |      | Bspc |
 * |------+------+------+------+------+------|                    |------+------+------+------+------+------|
 * |      | LAlt | LCtl |LShift|      | Caps |-------.    ,-------| PgDn | Left | Down | Right|  Del | Bspc |
 * |------+------+------+------+------+------|       |    |       |------+------+------+------+------+------|
 * |      | Undo |  Cut | Copy | Paste|      |-------|    |-------|      |      |      |      |      |      |
 * `-----------------------------------------/       /     \      \-----------------------------------------'
 */
[_RAISE] = LAYOUT(
    CYCLE, _______, _______, _______, _______, _______,                         _______, _______, _______, _______, _______, CONFIG_MODE,
  _______, KC_INS , KC_PSCR, KC_APP , XXXXXXX, XXXXXXX,                         KC_PGUP, _______, KC_UP  , _______, _______, KC_BSPC,
  _______, KC_LALT, KC_LCTL, KC_LSFT, XXXXXXX, KC_CAPS,                         KC_PGDN, KC_LEFT, KC_DOWN, KC_RGHT, KC_DEL , KC_BSPC,
  _______, KC_UNDO, KC_CUT , KC_COPY, KC_PASTE, XXXXXXX, _______,   _______,    XXXXXXX, _______, XXXXXXX, _______, XXXXXXX, _______,
                   _______, _______, _______, _______, _______,          _______, _______, _______, _______, _______
),
/* MOUSE (turns on automatically while the trackpad is touched)
 * ,-----------------------------------------.                    ,-----------------------------------------.
 * |      |      |      |      |      |      |                    |      |      |      |      |      |      |
 * |------+------+------+------+------+------|                    |------+------+------+------+------+------|
 * |      |      |      |      | Right| Left |                    | Left | Right|      |      |      |      |
 * |------+------+------+------+------+------|                    |------+------+------+------+------+------|
 * |      |      |      |      |      |      |-------.    ,-------|DrgScr| Left | Right|Middle|      |      |
 * |------+------+------+------+------+------|       |    |       |------+------+------+------+------+------|
 * |      |      |      |      |      |      |-------|    |-------|      |      |      |      |      |      |
 * `-----------------------------------------/       /     \      \-----------------------------------------'
 * Hold T (or Y) and move the finger to drag.
 */
[_MOUSE] = LAYOUT(
  _______, _______, _______, _______, _______, _______,                         _______    , _______, _______, _______, _______, _______,
  _______, _______, _______, _______, MS_BTN2, MS_BTN1,                         MS_BTN1    , MS_BTN2, _______, _______, _______, _______,
  _______, _______, _______, _______, _______, _______,                         DRAG_SCROLL, MS_BTN1, MS_BTN2, MS_BTN3, _______, _______,
  _______, _______, _______, _______, _______, _______, _______,    _______,    _______    , _______, _______, _______, _______, _______,
                   _______, _______, _______, _______, _______,          _______, _______, _______, _______, _______
),
/* ADJUST: Lower + Raise held together (any keys mapped to MO(Lower) and
 * MO(Raise) work). Empty (transparent) to start; fill it in Vial or the
 * config page.
 */
[_ADJUST] = LAYOUT(
  _______, _______, _______, _______, _______, _______,                         _______, _______, _______, _______, _______, _______,
  _______, _______, _______, _______, _______, _______,                         _______, _______, _______, _______, _______, _______,
  _______, _______, _______, _______, _______, _______,                         _______, _______, _______, _______, _______, _______,
  _______, _______, _______, _______, _______, _______, _______,    _______,    _______, _______, _______, _______, _______, _______,
                   _______, _______, _______, _______, _______,          _______, _______, _______, _______, _______
),
};

#ifdef ENCODER_MAP_ENABLE
// Left knob: volume. Right knob: page up/down.
const uint16_t PROGMEM encoder_map[][NUM_ENCODERS][NUM_DIRECTIONS] = {
    [_QWERTY]  = { ENCODER_CCW_CW(KC_VOLD, KC_VOLU), ENCODER_CCW_CW(KC_PGDN, KC_PGUP) },
    [_COLEMAK] = { ENCODER_CCW_CW(_______, _______), ENCODER_CCW_CW(_______, _______) },
    [_LOWER]   = { ENCODER_CCW_CW(_______, _______), ENCODER_CCW_CW(_______, _______) },
    [_RAISE]   = { ENCODER_CCW_CW(_______, _______), ENCODER_CCW_CW(_______, _______) },
    [_MOUSE]   = { ENCODER_CCW_CW(_______, _______), ENCODER_CCW_CW(_______, _______) },
    [_ADJUST]  = { ENCODER_CCW_CW(_______, _______), ENCODER_CCW_CW(_______, _______) },
};
#endif

// Adjust is on while both Lower and Raise are on, however they were turned on.
layer_state_t layer_state_set_user(layer_state_t state) {
    return update_tri_layer_state(state, _LOWER, _RAISE, _ADJUST);
}

bool process_record_user(uint16_t keycode, keyrecord_t *record) {
    switch (keycode) {
        case CYCLE:
            if (record->event.pressed) {
                set_single_persistent_default_layer((get_highest_layer(default_layer_state) + 1) % BASE_LAYERS);
            }
            return false;
        case DRAG_SCROLL:
            trackpad_set_drag_scroll(record->event.pressed);
            return false;
        case CONFIG_MODE:
            if (record->event.pressed) {
                config_mode_toggle();
            }
            return false;
        case WIGGLE:
            if (record->event.pressed) {
                wiggler_toggle();
            }
            return false;
        case OLED_PAGE: {
            // On release, and not after a long hold (the hold-both-knobs
            // config combo takes 2 s), so entering config mode keeps the page.
            static uint16_t pressed_at;
            if (record->event.pressed) {
                pressed_at = timer_read();
            } else if (timer_elapsed(pressed_at) < 1000) {
                oled_page_next();
            }
            return false;
        }
    }
    return true;
}

#ifdef RGB_MATRIX_ENABLE
// Turn the LEDs off after the chosen idle time (config mode, LEDs tab)
// without any key, encoder or trackpad activity, and restore the saved state on
// the next input.
static void rgb_idle_task(void) {
    static bool idle_off = false;
    uint32_t    timeout  = user_config_rgb_idle_ms();
    bool        idle     = timeout && last_input_activity_elapsed() > timeout;

    if (idle && !idle_off) {
        if (rgb_matrix_is_enabled()) {
            rgb_matrix_disable_noeeprom();
            idle_off = true;
        }
    } else if (!idle && idle_off) {
        rgb_matrix_reload_from_eeprom();
        idle_off = false;
    }
}
#endif

void keyboard_post_init_user(void) {
#ifdef OLED_ENABLE
    oled_right_init();
#endif
#ifdef POINTING_DEVICE_ENABLE
    trackpad_z_post_init();
#endif
    oled_pages_init();
#ifdef RGB_MATRIX_ENABLE
    led_layers_init();
#endif
    config_mode_init();
}

void housekeeping_task_user(void) {
#ifdef RGB_MATRIX_ENABLE
    // RGB state is owned by the master and synced to the right half.
    if (is_keyboard_master()) {
        rgb_idle_task();
    }
#endif
#ifdef POINTING_DEVICE_ENABLE
    trackpad_z_sync_task();
#endif
    oled_pages_task();
    wiggler_task();
    config_mode_task();
}
