// Copyright 2026 Brett-D
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Keeps the keymap and settings across firmware flashes.
//
// Vial marks the EEPROM with a random per-build number (BUILD_ID), so every
// new firmware wipes the keymap, knobs, LEDs, LED map, trackpad settings...
// Here the mark comes from BRETT_EEPROM_LAYOUT_VERSION (config.h) and the
// sizes that decide where everything is stored, so a reflash keeps it all,
// and only a real layout change resets it (once).
//
// The asserts below pin those sizes for the current version: if one fails,
// the stored layout changed -> bump BRETT_EEPROM_LAYOUT_VERSION and record the
// new values in a new block (the error names the value that changed). Even
// without that, a size change also changes the mark, so mismatched data is
// never loaded.

#include "brettssofle.h"
#include "via.h"
#include "nvm_eeprom_eeconfig_internal.h"
#include "nvm_eeprom_via_internal.h"
#ifdef VIAL_ENABLE
#    include "vial.h"
#endif
#ifdef QMK_SETTINGS
#    include "qmk_settings.h"
#endif
#ifndef NUM_ENCODERS
#    define NUM_ENCODERS 0
#endif

#ifndef BRETT_EEPROM_LAYOUT_VERSION
#    error "define BRETT_EEPROM_LAYOUT_VERSION in config.h"
#endif

#ifdef QMK_SETTINGS
#    define L_QMK_SETTINGS sizeof(qmk_settings_t)
#else
#    define L_QMK_SETTINGS 0
#endif
#ifdef VIAL_TAP_DANCE_ENABLE
#    define L_TAP_DANCE (sizeof(vial_tap_dance_entry_t) * VIAL_TAP_DANCE_ENTRIES)
#else
#    define L_TAP_DANCE 0
#endif
#ifdef VIAL_COMBO_ENABLE
#    define L_COMBO (sizeof(vial_combo_entry_t) * VIAL_COMBO_ENTRIES)
#else
#    define L_COMBO 0
#endif
#ifdef VIAL_KEY_OVERRIDE_ENABLE
#    define L_KEY_OVERRIDE (sizeof(vial_key_override_entry_t) * VIAL_KEY_OVERRIDE_ENTRIES)
#else
#    define L_KEY_OVERRIDE 0
#endif
#ifdef VIAL_ALT_REPEAT_KEY_ENABLE
#    define L_ALT_REPEAT (sizeof(vial_alt_repeat_key_entry_t) * VIAL_ALT_REPEAT_KEY_ENTRIES)
#else
#    define L_ALT_REPEAT 0
#endif

// Everything that moves stored data (same order as nvm_dynamic_keymap.c).
#define LAYOUT_VALUES(X)                                            \
    X(eeconfig_size, EECONFIG_SIZE)                                 \
    X(eeconfig_kb_data, EECONFIG_KB_DATA_SIZE)                      \
    X(eeconfig_user_data, EECONFIG_USER_DATA_SIZE)                  \
    X(via_config_end, VIA_EEPROM_CONFIG_END)                        \
    X(layers, DYNAMIC_KEYMAP_LAYER_COUNT)                           \
    X(rows, MATRIX_ROWS)                                            \
    X(cols, MATRIX_COLS)                                            \
    X(encoders, NUM_ENCODERS)                                       \
    X(qmk_settings, L_QMK_SETTINGS)                                 \
    X(tap_dance, L_TAP_DANCE)                                       \
    X(combos, L_COMBO)                                              \
    X(key_overrides, L_KEY_OVERRIDE)                                \
    X(alt_repeat, L_ALT_REPEAT)                                     \
    X(macro_count, DYNAMIC_KEYMAP_MACRO_COUNT)                      \
    X(eeprom_bytes, TOTAL_EEPROM_BYTE_COUNT)

// What each version stands for. Add a block when bumping the version.
#if BRETT_EEPROM_LAYOUT_VERSION == 1
#    define EXPECTED(X)                                                                                                                                  \
        X(eeconfig_size, 125) X(eeconfig_kb_data, 88) X(eeconfig_user_data, 0) X(via_config_end, 129) X(layers, 6) X(rows, 10) X(cols, 7) X(encoders, 2) \
        X(qmk_settings, 40) X(tap_dance, 320) X(combos, 320) X(key_overrides, 320) X(alt_repeat, 192) X(macro_count, 16) X(eeprom_bytes, 4096)
#else
#    error "record the layout values of the new BRETT_EEPROM_LAYOUT_VERSION here"
#endif
#define AS_ENUM(name, v) layout_##name = (v),
enum { EXPECTED(AS_ENUM) };
#define CHECK_ACTUAL(name, v) _Static_assert((v) == layout_##name, "EEPROM layout changed (" #name "): bump BRETT_EEPROM_LAYOUT_VERSION in config.h and record the new values in eeprom_layout.c");
LAYOUT_VALUES(CHECK_ACTUAL)

// Not part of the mark (nothing moved when they were introduced), but pinned:
// the pet's 32 bytes (pet.c) and, since v28, the macros' names (macros.c) sit
// above the macro buffer, taken off its end.
_Static_assert(DYNAMIC_KEYMAP_EEPROM_MAX_ADDR == 3867 && BRETT_MACRO_NAMES_EE_ADDR == 3868 && BRETT_PET_EE_ADDR == 4064,
               "the macro buffer end, the macro names or the pet moved: macros.c, pet.c and BRETT_EEPROM_LAYOUT_VERSION need a look");

#define AS_VALUE(name, v) (uint32_t)(v),
__attribute__((used)) const uint32_t brett_eeprom_layout[] = {BRETT_EEPROM_LAYOUT_VERSION, LAYOUT_VALUES(AS_VALUE)};

uint32_t via_eeprom_magic(void) {
    // FNV-1a over the version and the layout values, folded to 24 bits; never
    // 0xFFFFFF (the "invalid" mark).
    uint32_t h = 2166136261u;
    for (uint8_t i = 0; i < sizeof(brett_eeprom_layout) / sizeof(brett_eeprom_layout[0]); i++) {
        for (uint8_t b = 0; b < 4; b++) {
            h = (h ^ ((brett_eeprom_layout[i] >> (8 * b)) & 0xFF)) * 16777619u;
        }
    }
    h = (h ^ (h >> 24)) & 0xFFFFFF;
    return h == 0xFFFFFF ? 0xB5E770 : h;
}
