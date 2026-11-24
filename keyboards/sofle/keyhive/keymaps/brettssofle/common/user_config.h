// Copyright 2026 Brett-D
// SPDX-License-Identifier: GPL-2.0-or-later
//
// The settings kept in EECONFIG_USER (32 bits): press-to-click, the LED idle
// timeout and the LED zones. Built with or without the trackpad.

#pragma once

#include <stdbool.h>
#include <stdint.h>

void user_config_init(void); // keyboard_post_init_user: read, migrate, check

// Press-to-click (trackpad.c); set: false = refused (release Z must be below the threshold).
bool    trackpad_press_enabled(void);
uint8_t trackpad_press_threshold(void);
uint8_t trackpad_press_release_z(void);
bool    user_config_set_press(bool enabled, uint8_t threshold, uint8_t release_z);

// LED idle timeout choice (0 = never, 1/2/5/10/30 min).
uint8_t  user_config_rgb_idle_idx(void);
void     user_config_set_rgb_idle_idx(uint8_t idx);
uint32_t user_config_rgb_idle_ms(void);

// LED zones (0 effect everywhere, 1 keys + solid underglow, 2 keys only) and the solid underglow colour.
void user_config_get_led_zone(uint8_t *zone, uint8_t *hue, uint8_t *sat);
void user_config_set_led_zone(uint8_t zone, uint8_t hue, uint8_t sat);
