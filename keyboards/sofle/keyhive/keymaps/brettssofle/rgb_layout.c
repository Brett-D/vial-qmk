// Copyright 2026 Brett-D
// SPDX-License-Identifier: GPL-2.0-or-later
//
// RGB Matrix layout for the Keyhive Sofle v2 (per-key RGB + underglow), in
// physical chain order: per half 37 LEDs = 30 per-key (29 keys + the
// encoder/key slot) and 7 underglow. Derived from keyhive's rgblight led_map
// applied to the Sofle RGB chain order in keyboards/sofle/info.json (the
// right half's chain mirrors the left). Positions are key centres in QMK's
// 224 x 64 LED space; underglow LEDs sit between their chain neighbours.
//
// Overrides the weak, sofle/info.json-generated 72-LED layout (that file wins
// over keyhive/keyboard.json in this QMK's info merge). Count and split are
// set in config.h.

#include "quantum.h"

#ifdef RGB_MATRIX_ENABLE
_Static_assert(RGB_MATRIX_LED_COUNT == 74, "rgb_layout.c has 74 LEDs");

// clang-format off
led_config_t g_led_config = {
    {
        {     32,     31,     20,     19,      7,      6, NO_LED },
        {     33,     30,     21,     18,      8,      5, NO_LED },
        {     34,     29,     22,     17,      9,      3, NO_LED },
        {     35,     27,     23,     15,     11,      2, NO_LED },
        {      0,     36,     26,     24,     14,     12, NO_LED },
        {     41,     42,     53,     54,     66,     67, NO_LED },
        {     40,     43,     52,     55,     65,     68, NO_LED },
        {     39,     44,     51,     56,     64,     70, NO_LED },
        {     38,     46,     50,     58,     62,     71, NO_LED },
        {     37,     47,     49,     59,     61,     73, NO_LED },
    }, {
        { 88, 36}, { 81, 39}, { 75, 42}, { 75, 31}, { 75, 25}, { 75, 19}, { 75,  8}, { 61,  7},
        { 61, 18}, { 61, 29}, { 61, 35}, { 61, 40}, { 88, 56}, { 78, 54}, { 68, 53}, { 48, 39},
        { 48, 33}, { 48, 28}, { 48, 17}, { 48,  6}, { 34,  7}, { 34, 18}, { 34, 29}, { 34, 40},
        { 54, 52}, { 48, 52}, { 41, 51}, { 20, 43}, { 20, 38}, { 20, 32}, { 20, 21}, { 20, 10},
        {  7, 11}, {  7, 22}, {  7, 33}, {  7, 45}, { 27, 54}, {197, 54}, {217, 45}, {217, 33},
        {217, 22}, {217, 11}, {204, 10}, {204, 21}, {204, 32}, {204, 38}, {204, 43}, {183, 51},
        {176, 52}, {170, 52}, {190, 40}, {190, 29}, {190, 18}, {190,  7}, {176,  6}, {176, 17},
        {176, 28}, {176, 33}, {176, 39}, {156, 53}, {146, 54}, {136, 56}, {163, 40}, {163, 35},
        {163, 29}, {163, 18}, {163,  7}, {149,  8}, {149, 19}, {149, 25}, {149, 31}, {149, 42},
        {143, 39}, {136, 36},
    }, {
        LED_FLAG_KEYLIGHT, LED_FLAG_UNDERGLOW, LED_FLAG_KEYLIGHT, LED_FLAG_KEYLIGHT, LED_FLAG_UNDERGLOW, LED_FLAG_KEYLIGHT,
        LED_FLAG_KEYLIGHT, LED_FLAG_KEYLIGHT, LED_FLAG_KEYLIGHT, LED_FLAG_KEYLIGHT, LED_FLAG_UNDERGLOW, LED_FLAG_KEYLIGHT,
        LED_FLAG_KEYLIGHT, LED_FLAG_UNDERGLOW, LED_FLAG_KEYLIGHT, LED_FLAG_KEYLIGHT, LED_FLAG_UNDERGLOW, LED_FLAG_KEYLIGHT,
        LED_FLAG_KEYLIGHT, LED_FLAG_KEYLIGHT, LED_FLAG_KEYLIGHT, LED_FLAG_KEYLIGHT, LED_FLAG_KEYLIGHT, LED_FLAG_KEYLIGHT,
        LED_FLAG_KEYLIGHT, LED_FLAG_UNDERGLOW, LED_FLAG_KEYLIGHT, LED_FLAG_KEYLIGHT, LED_FLAG_UNDERGLOW, LED_FLAG_KEYLIGHT,
        LED_FLAG_KEYLIGHT, LED_FLAG_KEYLIGHT, LED_FLAG_KEYLIGHT, LED_FLAG_KEYLIGHT, LED_FLAG_KEYLIGHT, LED_FLAG_KEYLIGHT,
        LED_FLAG_KEYLIGHT, LED_FLAG_KEYLIGHT, LED_FLAG_KEYLIGHT, LED_FLAG_KEYLIGHT, LED_FLAG_KEYLIGHT, LED_FLAG_KEYLIGHT,
        LED_FLAG_KEYLIGHT, LED_FLAG_KEYLIGHT, LED_FLAG_KEYLIGHT, LED_FLAG_UNDERGLOW, LED_FLAG_KEYLIGHT, LED_FLAG_KEYLIGHT,
        LED_FLAG_UNDERGLOW, LED_FLAG_KEYLIGHT, LED_FLAG_KEYLIGHT, LED_FLAG_KEYLIGHT, LED_FLAG_KEYLIGHT, LED_FLAG_KEYLIGHT,
        LED_FLAG_KEYLIGHT, LED_FLAG_KEYLIGHT, LED_FLAG_KEYLIGHT, LED_FLAG_UNDERGLOW, LED_FLAG_KEYLIGHT, LED_FLAG_KEYLIGHT,
        LED_FLAG_UNDERGLOW, LED_FLAG_KEYLIGHT, LED_FLAG_KEYLIGHT, LED_FLAG_UNDERGLOW, LED_FLAG_KEYLIGHT, LED_FLAG_KEYLIGHT,
        LED_FLAG_KEYLIGHT, LED_FLAG_KEYLIGHT, LED_FLAG_KEYLIGHT, LED_FLAG_UNDERGLOW, LED_FLAG_KEYLIGHT, LED_FLAG_KEYLIGHT,
        LED_FLAG_UNDERGLOW, LED_FLAG_KEYLIGHT,
    }
};
// clang-format on
#endif
