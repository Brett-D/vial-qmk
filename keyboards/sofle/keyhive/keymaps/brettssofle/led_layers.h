// Copyright 2026 Brett-D
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Layered lighting (see led_layers.c).

#pragma once

#include <stdbool.h>
#include <stdint.h>

// Pseudo VialRGB id naming the layered effect on the config page.
#define LED_LAYERS_VIALRGB_ID 0xFE

void led_layers_init(void); // keyboard_post_init_user (master loads the saved choice)
// The pair the LAYERED effect draws (rgb_matrix modes, 0 = none), with the
// key-press layer's own hue / saturation.
void led_layers_get(uint8_t *base, uint8_t *react, uint8_t *hue, uint8_t *sat);
void led_layers_set_remote(uint8_t base, uint8_t react, uint8_t hue, uint8_t sat); // right half
// Preview a choice: selects the rgb_matrix mode (layered, plain or none).
// false = not a valid pair (nothing changed).
bool led_layers_apply(uint8_t base, uint8_t react, uint8_t hue, uint8_t sat);
void led_layers_save(void);
void led_layers_revert(void);
bool led_layers_is_base(uint8_t mode);  // whole-keyboard effect
bool led_layers_is_react(uint8_t mode); // key-press effect that can go over a base
bool led_layers_is_solo(uint8_t mode);  // key-press effect that can't (it paints every key)
uint8_t led_layers_mode(void);          // RGB_MATRIX_CUSTOM_LAYERED

void rgb_driver_overlay(bool on); // rgb_driver.c: blend the key-press layer over the base
