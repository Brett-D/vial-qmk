// Copyright 2026 Brett-D
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Layered lighting: background + key-press overlay (settings: led_fx.c,
// drawing: led_fx_draw.c; the
// maths is in led_fx_core.h, the list of effects in led_fx_table.h).

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "led_fx_core.h"

// Pseudo VialRGB id naming the layered effect on the config page.
#define LED_LAYERS_VIALRGB_ID 0xFE
// led_layers_apply: leave the key-press effect as it is.
#define LED_LAYERS_KEEP 0xFF

void led_layers_init(void); // keyboard_post_init_user (master loads the saved choice)

// v20 settings (raw HID 0xF6 0x1C, the config page).
void led_fx_get(fx_settings_t *s);
bool led_fx_set(const fx_settings_t *s); // preview; false = not allowed (led_fx_table.h)
void led_fx_save(void);
void led_fx_fire(uint8_t row, uint8_t col); // a ripple from that key, on both halves
void led_fx_task(void);                     // master: settings / test ripples to the right half
uint8_t led_fx_base(void);

// The v6-v19 interface: background + key-press effect as rgb_matrix modes,
// with the key-press colour (react: 0 = none, LED_LAYERS_KEEP = unchanged).
void led_layers_get(uint8_t *base, uint8_t *react, uint8_t *hue, uint8_t *sat);
void led_layers_set_remote(uint8_t base, uint8_t react, uint8_t hue, uint8_t sat); // right half, older left half
bool led_layers_apply(uint8_t base, uint8_t react, uint8_t hue, uint8_t sat);
void led_layers_save(void);
void led_layers_revert(void);
bool led_layers_is_base(uint8_t mode);  // background (QMK's or ours)
bool led_layers_is_react(uint8_t mode); // QMK key-press effect (mapped to an overlay)
bool led_layers_is_solo(uint8_t mode);  // standalone (no key-press effect with it)
uint8_t led_layers_mode(void);          // RGB_MATRIX_CUSTOM_LAYERED

// led_fx_background(effect_params_t *): declared in rgb_matrix_user.inc (needs rgb_matrix's types).
void led_fx_layered_drawn(void); // LAYERED rendered this frame (else the next flush has no overlay)
// rgb_driver.c, once per frame before flushing: false = no overlay this frame.
bool led_fx_frame_begin(bool calibrating);
void led_fx_pixel(uint8_t led, const uint8_t in[3], uint16_t out[3]); // the key-press layer over one LED: 8-bit in, 8.8 out
