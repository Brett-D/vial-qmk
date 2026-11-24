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
// It also keeps a copy of the frame (background + indicators), and at flush
// the key-press overlay (led_layers.c) is composited over it once for every
// LED of this half: effects that repaint every LED on each render pass
// (digital rain) can't wipe it, and the frame itself is never overwritten.

#include "quantum.h"
#include "ws2812.h"
#include "led_layers.h"

#define HALF_LEDS (RGB_MATRIX_LED_COUNT / 2)

static uint8_t frame[WS2812_LED_COUNT][3]; // this frame's colours before the overlay

int rgb_matrix_led_index(int index) {
    if (is_keyboard_left()) {
        return index < HALF_LEDS ? index : -1;
    }
    return index >= HALF_LEDS ? index - HALF_LEDS : -1;
}

static void half_set_color(int index, uint8_t red, uint8_t green, uint8_t blue) {
    if (index < 0 || index >= WS2812_LED_COUNT) {
        return;
    }
    frame[index][0] = red;
    frame[index][1] = green;
    frame[index][2] = blue;
    ws2812_set_color(index, red, green, blue);
}

static void half_set_color_all(uint8_t red, uint8_t green, uint8_t blue) {
    for (int i = 0; i < WS2812_LED_COUNT; i++) {
        half_set_color(i, red, green, blue);
    }
}

static void half_flush(void) {
    if (led_fx_frame_begin(config_mode_led_cal_busy())) {
        uint8_t first = is_keyboard_left() ? 0 : HALF_LEDS;
        for (int i = 0; i < WS2812_LED_COUNT; i++) {
            uint8_t rgb[3] = {frame[i][0], frame[i][1], frame[i][2]};
            led_fx_pixel(first + i, rgb);
            ws2812_set_color(i, rgb[0], rgb[1], rgb[2]);
        }
    }
    ws2812_flush();
}

const rgb_matrix_driver_t rgb_matrix_driver = {
    .init          = ws2812_init,
    .flush         = half_flush,
    .set_color     = half_set_color,
    .set_color_all = half_set_color_all,
};
