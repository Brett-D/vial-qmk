// Copyright 2026 Brett-D
// SPDX-License-Identifier: GPL-2.0-or-later
//
// v28 (Brett: "add nice macros like delete line").
//
// Built-in macros: keycodes that tap a short, fixed sequence of standard keys,
// so they work in any app with nothing to set up. Each step is a tap of one
// keycode (modifiers included, e.g. S(KC_END)); held modifiers are lifted for
// the macro and put back afterwards. Windows-first (labels / tooltips:
// vial.json). Delete line selects over the line break with Shift+Right rather
// than pressing Delete twice: on an empty line Shift+End selects nothing, and
// the second Delete would eat the next line's first character.
//
// Names of Vial's dynamic macros: MACRO_NAME_COUNT x MACRO_NAME_LEN bytes after
// a 4-byte header, between Vial's macro buffer and the pet (config.h). Taken off
// the end of the macro buffer (nothing else moves; the EEPROM mark is unchanged).
// Zeros (a reset EEPROM, or what was unused macro space) read as no names.

#include QMK_KEYBOARD_H
#include "brettssofle.h"
#include "eeprom.h"
#include "macros.h"
#include "perf.h"

#ifndef BUILTIN_MACRO_STEP_MS
#    define BUILTIN_MACRO_STEP_MS 6 // key down for this long, then as long up (some remote desktops drop faster taps)
#endif

#define MAX_STEPS 6
static const uint16_t PROGMEM steps[][MAX_STEPS] = {
    [BM_DEL_LINE - BM_DEL_LINE]    = {KC_HOME, S(KC_END), S(KC_RIGHT), KC_DELETE},
    [BM_SEL_LINE - BM_DEL_LINE]    = {KC_HOME, S(KC_END)},
    [BM_DUP_LINE - BM_DEL_LINE]    = {KC_HOME, S(KC_END), C(KC_C), KC_END, KC_ENTER, C(KC_V)},
    [BM_DEL_WORD_L - BM_DEL_LINE]  = {C(KC_BACKSPACE)},
    [BM_DEL_WORD_R - BM_DEL_LINE]  = {C(KC_DELETE)},
    [BM_LINE_BELOW - BM_DEL_LINE]  = {KC_END, KC_ENTER},
    [BM_LINE_ABOVE - BM_DEL_LINE]  = {KC_HOME, KC_ENTER, KC_UP},
    [BM_PASTE_PLAIN - BM_DEL_LINE] = {C(S(KC_V))},
    [BM_SCREENSHOT - BM_DEL_LINE]  = {G(S(KC_S))},
    [BM_LOCK - BM_DEL_LINE]        = {G(KC_L)},
    [BM_DESKTOP - BM_DEL_LINE]     = {G(KC_D)},
    [BM_TASKMGR - BM_DEL_LINE]     = {C(S(KC_ESCAPE))},
    [BM_UNDO - BM_DEL_LINE]        = {C(KC_Z)},
    [BM_REDO - BM_DEL_LINE]        = {C(KC_Y)},
    [BM_EMOJI - BM_DEL_LINE]       = {G(KC_DOT)},
    [BM_CLIPBOARD - BM_DEL_LINE]   = {G(KC_V)},
};
_Static_assert(sizeof(steps) / sizeof(steps[0]) == BM_CLIPBOARD - BM_DEL_LINE + 1, "macros.c: a sequence for every BM_ keycode");

bool builtin_macro_process(uint16_t keycode, keyrecord_t *record) {
    if (keycode < BM_DEL_LINE || keycode > BM_CLIPBOARD) {
        return false;
    }
    if (!record->event.pressed) {
        return true;
    }
    uint8_t mods = get_mods(), weak = get_weak_mods();
    clear_mods();
    clear_weak_mods();
    send_keyboard_report();
    for (uint8_t i = 0; i < MAX_STEPS; i++) {
        uint16_t kc = pgm_read_word(&steps[keycode - BM_DEL_LINE][i]);
        if (!kc) {
            break;
        }
        tap_code16_delay(kc, BUILTIN_MACRO_STEP_MS);
        wait_ms(BUILTIN_MACRO_STEP_MS);
    }
    set_mods(mods);
    set_weak_mods(weak);
    send_keyboard_report();
    return true;
}

/* ---- names of Vial's macros ---- */

#define NAMES_EE ((uint8_t *)(uintptr_t)BRETT_MACRO_NAMES_EE_ADDR)
#define NAMES_MAGIC0 0x4D // "Mn"
#define NAMES_MAGIC1 0x6E
_Static_assert(BRETT_MACRO_NAMES_EE_ADDR == DYNAMIC_KEYMAP_EEPROM_MAX_ADDR + 1, "the macro names sit right after Vial's macro buffer");
_Static_assert(BRETT_MACRO_NAMES_EE_ADDR + 4 + MACRO_NAME_COUNT * MACRO_NAME_LEN <= BRETT_PET_EE_ADDR, "the macro names overlap the pet");

static uint8_t names_sum(void) {
    uint8_t s = 0x5A;
    for (uint16_t i = 0; i < MACRO_NAME_COUNT * MACRO_NAME_LEN; i++) {
        s = (uint8_t)((s << 1 | s >> 7) ^ eeprom_read_byte(NAMES_EE + 4 + i));
    }
    return s;
}

static bool names_valid(void) {
    uint8_t h[4];
    eeprom_read_block(h, NAMES_EE, sizeof h);
    return h[0] == NAMES_MAGIC0 && h[1] == NAMES_MAGIC1 && h[2] == 1 && h[3] == names_sum();
}

void macro_name_get(uint8_t i, char out[MACRO_NAME_LEN]) {
    memset(out, 0, MACRO_NAME_LEN);
    if (i < MACRO_NAME_COUNT && names_valid()) {
        eeprom_read_block(out, NAMES_EE + 4 + i * MACRO_NAME_LEN, MACRO_NAME_LEN);
    }
}

bool macro_name_set(uint8_t i, const char *in) {
    if (i >= MACRO_NAME_COUNT) {
        return false;
    }
    char n[MACRO_NAME_LEN];
    bool end = false;
    for (uint8_t k = 0; k < MACRO_NAME_LEN; k++) { // printable ASCII up to the first other byte, zero-padded
        char c = end ? 0 : in[k]; // nothing read past the end
        end |= c < 0x20 || c >= 0x7F;
        n[k] = end ? 0 : c;
    }
    uint32_t t0 = perf_us();
    if (!names_valid()) { // first name: the area starts out empty
        uint8_t zero[MACRO_NAME_LEN] = {0};
        for (uint8_t k = 0; k < MACRO_NAME_COUNT; k++) eeprom_update_block(zero, NAMES_EE + 4 + k * MACRO_NAME_LEN, MACRO_NAME_LEN);
        uint8_t h[3] = {NAMES_MAGIC0, NAMES_MAGIC1, 1};
        eeprom_update_block(h, NAMES_EE, sizeof h);
    }
    eeprom_update_block(n, NAMES_EE + 4 + i * MACRO_NAME_LEN, MACRO_NAME_LEN);
    eeprom_update_byte(NAMES_EE + 3, names_sum());
    perf_add(PF_FLASH, perf_us() - t0);
    return true;
}

void macro_name_raw_hid(uint8_t *data, bool allowed) {
    uint8_t op = data[2], i = data[3];
    bool    ok = op == 0 ? i < MACRO_NAME_COUNT : allowed && macro_name_set(i, (const char *)&data[4]);
    data[2]    = 0xA4;
    macro_name_get(i, (char *)&data[4]);
    data[16] = ok ? 1 : (op == 1 && !allowed ? 3 : 0);
}
