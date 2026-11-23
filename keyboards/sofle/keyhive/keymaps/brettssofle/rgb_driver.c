// Copyright 2026 Brett-D
// SPDX-License-Identifier: GPL-2.0-or-later
//
// rgblight driver that only drives this half's LEDs.
//
// With RGBLIGHT_SPLIT, QMK runs effects over all RGBLIGHT_LED_COUNT (74) LEDs
// on both halves and maps them through led_map minus the half's clipping start.
// The stock WS2812 driver then has a 74-entry buffer with no bounds check:
// the left half sends 74 LEDs down its 37-LED chain on every update, and on the
// right half the left LEDs' indices underflow (uint8_t, e.g. 28 - 37 = 247) and
// get written past the end of the buffer, corrupting RAM.
//
// WS2812_LED_COUNT is set to one half (config.h) and other-half indices are
// dropped here.

#include "quantum.h"
#include "ws2812.h"
#include "rgblight_drivers.h"

static void half_set_color(int index, uint8_t red, uint8_t green, uint8_t blue) {
    if (index >= 0 && index < WS2812_LED_COUNT) {
        ws2812_set_color(index, red, green, blue);
    }
}

const rgblight_driver_t rgblight_driver = {
    .init          = ws2812_init,
    .set_color     = half_set_color,
    .set_color_all = ws2812_set_color_all,
    .flush         = ws2812_flush,
};
