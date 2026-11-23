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
//
// It also keeps a copy of the frame, so the layered effect (led_layers.c) can
// blend its key-press layer over the whole-keyboard layer: in overlay mode a
// colour is mixed over the frame by its brightness instead of replacing it.

#include "quantum.h"
#include "ws2812.h"
#include "led_layers.h"

#define HALF_LEDS (RGB_MATRIX_LED_COUNT / 2)

static uint8_t frame[WS2812_LED_COUNT][3]; // last colours outside overlay mode
static bool    overlay;

int rgb_matrix_led_index(int index) {
    if (is_keyboard_left()) {
        return index < HALF_LEDS ? index : -1;
    }
    return index >= HALF_LEDS ? index - HALF_LEDS : -1;
}

void rgb_driver_overlay(bool on) {
    overlay = on;
}

static void half_set_color(int index, uint8_t red, uint8_t green, uint8_t blue) {
    if (index < 0 || index >= WS2812_LED_COUNT) {
        return;
    }
    if (overlay) {
        // Opacity = brightness relative to the current brightness setting, so
        // a fading key-press light fades back into the frame underneath.
        uint8_t  top   = MAX(red, MAX(green, blue));
        uint8_t  full  = MAX(rgb_matrix_get_val(), 1);
        uint16_t alpha = top >= full ? 255 : (uint16_t)top * 255 / full;
        uint8_t *under = frame[index];
        red            = MIN(255, red + under[0] * (255 - alpha) / 255);
        green          = MIN(255, green + under[1] * (255 - alpha) / 255);
        blue           = MIN(255, blue + under[2] * (255 - alpha) / 255);
    } else {
        frame[index][0] = red;
        frame[index][1] = green;
        frame[index][2] = blue;
    }
    ws2812_set_color(index, red, green, blue);
}

static void half_set_color_all(uint8_t red, uint8_t green, uint8_t blue) {
    for (int i = 0; i < WS2812_LED_COUNT; i++) {
        half_set_color(i, red, green, blue);
    }
}

const rgb_matrix_driver_t rgb_matrix_driver = {
    .init          = ws2812_init,
    .flush         = ws2812_flush,
    .set_color     = half_set_color,
    .set_color_all = half_set_color_all,
};
