// Copyright 2026 Brett-D
// SPDX-License-Identifier: GPL-2.0-or-later
//
// RGB Matrix driver that only drives this half's LEDs.
//
// Each half has its own 37-LED chain (WS2812_LED_COUNT, config.h), but RGB
// Matrix indexes all 74 LEDs, and rgb_matrix_set_color_all() / indicator code
// touch the other half's indices too. The stock path would write those into
// this half's buffer (left: past its end; right: over its own LEDs), so other
// half indices are dropped here.

#include "quantum.h"
#include "ws2812.h"

#define HALF_LEDS (RGB_MATRIX_LED_COUNT / 2)

int rgb_matrix_led_index(int index) {
    if (is_keyboard_left()) {
        return index < HALF_LEDS ? index : -1;
    }
    return index >= HALF_LEDS ? index - HALF_LEDS : -1;
}

static void half_set_color(int index, uint8_t red, uint8_t green, uint8_t blue) {
    if (index >= 0 && index < WS2812_LED_COUNT) {
        ws2812_set_color(index, red, green, blue);
    }
}

const rgb_matrix_driver_t rgb_matrix_driver = {
    .init          = ws2812_init,
    .flush         = ws2812_flush,
    .set_color     = half_set_color,
    .set_color_all = ws2812_set_color_all,
};
