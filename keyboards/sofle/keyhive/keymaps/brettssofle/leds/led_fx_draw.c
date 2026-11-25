// Copyright 2026 Brett-D
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Drawing the lighting (led_fx.c has the settings): our own backgrounds
// (LAYERED's background pass, rgb_matrix_user.inc) and the key-press layer,
// composited once per frame over every LED of this half when rgb_driver.c
// flushes. The maths is led_fx_core.h (fx_core.js on the config page).

#include QMK_KEYBOARD_H
#include "led_fx.h"
#include "led_map.h"
#include "config_mode.h"

#ifdef RGB_MATRIX_ENABLE
#    define FX_DIM_STEP 24 // per frame, background dim in / out

static fx_settings_t fx; // this frame's settings (led_fx_get at frame start)
static uint8_t       dim_now = 255;

// LAYERED's background pass for our own backgrounds (rgb_matrix_user.inc).
bool led_fx_background(effect_params_t *params) {
    RGB_MATRIX_USE_LIMITS(led_min, led_max);
    if (led_fx_base() < FX_OWN_BG) {
        for (uint8_t i = led_min; i < led_max; i++) {
            rgb_matrix_set_color(i, 0, 0, 0); // no background: dark
        }
        return rgb_matrix_check_finished_leds(led_max);
    }
    // The hit tracker is a packed struct: copy its ticks (no unaligned 16-bit reads on the M0+).
    uint16_t ht[LED_HITS_TO_REMEMBER];
    uint8_t  hx[LED_HITS_TO_REMEMBER];
    for (uint8_t j = 0; j < g_last_hit_tracker.count; j++) {
        hx[j] = g_last_hit_tracker.x[j];
        ht[j] = g_last_hit_tracker.tick[j];
    }
    fx_bg_ctx_t c = {.t = g_rgb_timer, .hue = rgb_matrix_get_hue(), .sat = rgb_matrix_get_sat(), .val = rgb_matrix_get_val(), .spd = rgb_matrix_get_speed(),
                     .n = g_last_hit_tracker.count, .hx = hx, .ht = ht};
    c.wpm = fx_typing_wpm(c.n, ht); // from the presses both halves see (QMK's WPM counts on the left only)
    for (uint8_t i = led_min; i < led_max; i++) {
        RGB_MATRIX_TEST_LED_FLAGS();
        fx_hsv_t o   = fx_background(led_fx_base(), &c, g_led_config.point[i].x, g_led_config.point[i].y);
        rgb_t    rgb = hsv_to_rgb((hsv_t){o.h, o.s, o.v});
        rgb_matrix_set_color(i, rgb.r, rgb.g, rgb.b);
    }
    return rgb_matrix_check_finished_leds(led_max);
}

// The pet gets excited when you type fast (pet_draw.c): the same typing speed as the meter.
uint8_t pet_typing_wpm(void) {
    uint16_t ht[LED_HITS_TO_REMEMBER];
    for (uint8_t j = 0; j < g_last_hit_tracker.count; j++) ht[j] = g_last_hit_tracker.tick[j];
    return fx_typing_wpm(g_last_hit_tracker.count, ht);
}

// Compositing: rgb_driver.c, once per frame at flush, over every LED of this half.
static bool       frame_on;
static uint8_t    frame_v, frame_hue, frame_n;
static fx_prep_t  prep;
static fx_event_t ev[LED_HITS_TO_REMEMBER];
// Press-to-LED distances per event slot (this half's LEDs), kept while the slot holds the same press.
#    define FX_HALF_LEDS (RGB_MATRIX_LED_COUNT / 2)
static uint8_t dcache[LED_HITS_TO_REMEMBER][FX_HALF_LEDS];
static uint8_t dkey[LED_HITS_TO_REMEMBER][2];
static bool    layered_drawn;

// LAYERED rendered (part of) this frame. A suspend or RGB_MATRIX_TIMEOUT
// renders effect NONE instead and flushes that black frame once: it must go
// out without the overlay, or a running ripple stays lit while the computer sleeps.
void led_fx_layered_drawn(void) {
    layered_drawn = true;
}

bool led_fx_frame_begin(bool calibrating) {
    bool drawn    = layered_drawn;
    layered_drawn = false;
    static uint8_t map_gen;
    if (map_gen != led_map_generation()) { // LED positions moved: the cached distances are stale
        map_gen = led_map_generation();
        memset(dcache, 0xFF, sizeof dcache);
    }
    led_fx_get(&fx); // this frame's settings
    frame_on = drawn && rgb_matrix_is_enabled() && rgb_matrix_get_mode() == led_layers_mode() && fx.effect && !calibrating;
    if (!frame_on) {
        dim_now = 255;
        return false;
    }
    frame_v   = rgb_matrix_get_val();
    frame_hue = rgb_matrix_get_hue();
    fx_prepare(&prep, &fx);
    bool any = false;
    frame_n  = 0;
    for (uint8_t j = 0; j < g_last_hit_tracker.count; j++) {
        fx_event_t *e = &ev[frame_n];
        fx_event_prepare(&prep, e, g_last_hit_tracker.x[j], g_last_hit_tracker.y[j], g_last_hit_tracker.tick[j], g_rgb_timer - g_last_hit_tracker.tick[j]);
        if (fx_alive(&prep, e)) {
            if (dkey[frame_n][0] != e->x || dkey[frame_n][1] != e->y5) {
                dkey[frame_n][0] = e->x, dkey[frame_n][1] = e->y5;
                memset(dcache[frame_n], 0xFF, FX_HALF_LEDS);
            }
            frame_n++; // only the presses still lighting something
            any = true;
        }
    }
    // The background dims while a ripple runs (dim 0 = never), fading in and out.
    uint8_t want = any ? 255 - fx.dim * 28 : 255;
    dim_now      = dim_now < want ? (want - dim_now < FX_DIM_STEP ? want : dim_now + FX_DIM_STEP) : (dim_now - want < FX_DIM_STEP ? want : dim_now - FX_DIM_STEP);
    return true;
}

// v26: the pet's LED reaction, over everything (and the key-press layer):
// in the key-press brightness, its strength scaled by the pet's LED setting.
static fx_pet_t pet_g;
static uint16_t pet_k;
static uint8_t  pet_v;
static bool     pet_ug_dark;

bool led_pet_frame_begin(bool calibrating) {
    uint8_t  type, level;
    uint16_t t;
    if (calibrating || !led_fx_pet_now(&type, &t, &level)) {
        return false;
    }
    fx_pet_prepare(&pet_g, type, t);
    if (!pet_g.type) {
        return false;
    }
    fx_settings_t s;
    led_fx_get(&s);
    pet_k       = fx_pet_k(level);
    pet_v       = fx_overlay_v(s.bright, rgb_matrix_get_val(), RGB_MATRIX_MAXIMUM_BRIGHTNESS);
    pet_ug_dark = config_mode_underglow_dark(); // "keys only": the underglow stays dark
    return true;
}

void led_pet_pixel(uint8_t led, uint16_t rgb[3]) {
    led_flags_t f     = g_led_config.flags[led];
    bool        under = HAS_FLAGS(f, LED_FLAG_UNDERGLOW);
    if (f == LED_FLAG_NONE || (under && pet_ug_dark)) {
        return;
    }
    fx_pet_px_t o = fx_pet_px(&pet_g, g_led_config.point[led].x, g_led_config.point[led].y, under);
    uint8_t     a = fx_pet_alpha(&pet_g, o.a, pet_k);
    if (!a) {
        return;
    }
    uint8_t v      = fx_div255((uint32_t)pet_v * o.v);
    rgb_t   c      = hsv_to_rgb((hsv_t){o.h, o.s, v});
    uint8_t top[3] = {c.r, c.g, c.b};
    fx_blend16(FX_REPLACE, rgb, top, a, v);
}

// The overlay over one LED's background colour: in = 8-bit, out = 8.8 (v23).
void led_fx_pixel(uint8_t led, const uint8_t in[3], uint16_t rgb[3]) {
    for (uint8_t i = 0; i < 3; i++) rgb[i] = (uint16_t)in[i] << 8;
    if (!frame_on || !HAS_ANY_FLAGS(g_led_config.flags[led], rgb_matrix_get_flags())) {
        return;
    }
    if (dim_now != 255) {
        for (uint8_t i = 0; i < 3; i++) rgb[i] = (uint32_t)rgb[i] * dim_now >> 8;
    }
    uint8_t  lx = g_led_config.point[led].x, ly = g_led_config.point[led].y;
    uint16_t best = 0;
    uint8_t  bj   = 0;
    for (uint8_t j = 0; j < frame_n; j++) {
        uint8_t a = fx_intensity_c(&prep, &ev[j], lx, ly, &dcache[j][led % FX_HALF_LEDS]);
        if (fx.effect == FX_HEAT) {
            best += a; // heat adds up
        } else if (a > best) {
            best = a, bj = j;
        }
    }
    if (!best) {
        return;
    }
    uint8_t v      = fx_overlay_v(fx.bright, frame_v, RGB_MATRIX_MAXIMUM_BRIGHTNESS);
    rgb_t   c      = hsv_to_rgb((hsv_t){fx_hue(&prep, &ev[bj], frame_hue, lx, ly), fx_sat(&prep), v});
    uint8_t top[3] = {c.r, c.g, c.b};
    fx_blend16(fx.blend, rgb, top, best > 255 ? 255 : (uint8_t)best, v);
}

#endif
