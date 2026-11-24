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
// the key-press overlay (led_fx.c) is composited over it once for every
// LED of this half: effects that repaint every LED on each render pass
// (digital rain) can't wipe it, and the frame itself is never overwritten.
//
// v23, smooth output: the composite is 8.8 fixed point, then gamma 2.2 (scaled
// to the brightness cap, so the cap is still the most an LED gives and power is
// unchanged), then temporal dithering: each channel keeps its rounding error
// and adds it to the next send, and between frames (every 16 ms) the frame is
// sent again every ~4 ms with new dither values (the WS2812 transfer is DMA,
// ~1.1 ms for 37 LEDs). So a fade has ~256 steps per level instead of the
// cap's ~100 levels, and dim levels average out instead of stepping. LED
// calibration stays linear and undithered (its dim test colours must show).

#include "quantum.h"
#include "ws2812.h"
#include "led_fx.h"
#include "config_mode.h"
#include "rgb_driver.h"

#define HALF_LEDS (RGB_MATRIX_LED_COUNT / 2)
#ifndef FX_DITHER_MS
#    define FX_DITHER_MS 4 // re-send period between frames (~250 Hz)
#endif
#define RGB_DRIVER_REFRESH_MS 250 // without flushes (effect off): the output again, so a lost transfer can't stick
#define FX_DITHER_MIN 48 // below ~0.2 of the lowest level: off (a slow blink of the lowest level would show)

static uint8_t  frame[WS2812_LED_COUNT][3];  // this frame's colours before the overlay
static uint16_t target[WS2812_LED_COUNT][3]; // what goes out, 8.8 (after the overlay, cap and gamma)
static uint8_t  err[WS2812_LED_COUNT][3];    // dither: rounding error carried to the next send
static bool     dithering;                   // some target has a fraction: keep re-sending
static uint16_t last_emit;
static bool     blanked; // the output is dark because the LEDs are off

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
}

static void half_set_color_all(uint8_t red, uint8_t green, uint8_t blue) {
    for (int i = 0; i < WS2812_LED_COUNT; i++) {
        half_set_color(i, red, green, blue);
    }
}

// Sends target[] with the carried dither error.
static void emit(void) {
    static bool seeded;
    if (!seeded) { // neighbours start out of step, so dim areas don't pulse together
        for (int i = 0; i < WS2812_LED_COUNT; i++) {
            for (uint8_t ch = 0; ch < 3; ch++) err[i][ch] = (uint8_t)((i * 3 + ch) * 97);
        }
        seeded = true;
    }
    bool frac = false;
    for (int i = 0; i < WS2812_LED_COUNT; i++) {
        uint8_t o[3];
        for (uint8_t ch = 0; ch < 3; ch++) {
            uint16_t t = target[i][ch];
            if (t < FX_DITHER_MIN) {
                o[ch]       = 0;
                err[i][ch]  = 0;
                continue;
            }
            uint16_t v = t + err[i][ch]; // t <= cap * 256, so v >> 8 <= cap
            o[ch]      = v >> 8;
            err[i][ch] = v & 0xFF;
            frac |= (t & 0xFF) != 0;
        }
        ws2812_set_color(i, o[0], o[1], o[2]);
    }
    ws2812_flush();
    last_emit = timer_read();
    dithering = frac;
}

// The LEDs are off on this half (idle timeout, turned off, USB suspend). QMK
// then sends one black frame and stops flushing: that frame could still carry
// the key-press overlay (a suspend between a LAYERED render pass and its
// flush), and if it got lost on the data line nothing would ever correct it.
// Brett, v22: after idle the left half was dark, the right one stayed lit.
static bool output_off(void) {
    return !config_mode_led_cal_busy() && (!rgb_matrix_is_enabled() || rgb_matrix_get_suspend_state());
}

static void blank(void) {
    memset(target, 0, sizeof target);
    blanked = true;
    emit();
}

static void half_flush(void) {
    if (output_off()) {
        blank();
        return;
    }
    bool     cal   = config_mode_led_cal_busy();
    uint8_t  first = is_keyboard_left() ? 0 : HALF_LEDS;
    uint32_t gk    = fx_gamma_k(RGB_MATRIX_MAXIMUM_BRIGHTNESS);
    if (!cal) {
        // LEDs mapped to nothing (led_map.c) are skipped by every effect: keep them dark,
        // or one that was lit when the map changed would stay lit.
        for (int i = 0; i < WS2812_LED_COUNT; i++) {
            if (g_led_config.flags[first + i] == LED_FLAG_NONE) {
                frame[i][0] = frame[i][1] = frame[i][2] = 0;
            }
        }
    }
    bool fx = led_fx_frame_begin(cal);
    for (int i = 0; i < WS2812_LED_COUNT; i++) {
        uint16_t o[3];
        if (fx) {
            led_fx_pixel(first + i, frame[i], o);
        } else {
            for (uint8_t ch = 0; ch < 3; ch++) o[ch] = (uint16_t)frame[i][ch] << 8;
        }
        fx_cap16(o, RGB_MATRIX_MAXIMUM_BRIGHTNESS);
        for (uint8_t ch = 0; ch < 3; ch++) {
            target[i][ch] = cal ? o[ch] & 0xFF00 : fx_gamma16(o[ch], RGB_MATRIX_MAXIMUM_BRIGHTNESS, gk);
        }
    }
    blanked = false;
    emit();
}

// keymap.c: housekeeping (both halves) and suspend_power_down_user (the
// master's suspend loop, which has no housekeeping). Between frames the same
// frame again with new dither values; dark as soon as the LEDs are off; and
// without flushes the output again every RGB_DRIVER_REFRESH_MS.
void rgb_driver_task(void) {
    if (output_off() && !blanked) {
        blank();
        return;
    }
    uint16_t since = timer_elapsed(last_emit);
    if ((dithering && since >= FX_DITHER_MS) || since >= RGB_DRIVER_REFRESH_MS) {
        emit();
    }
}

const rgb_matrix_driver_t rgb_matrix_driver = {
    .init          = ws2812_init,
    .flush         = half_flush,
    .set_color     = half_set_color,
    .set_color_all = half_set_color_all,
};
