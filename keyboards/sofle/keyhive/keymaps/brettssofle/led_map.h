// Copyright 2026 Brett-D
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Runtime LED map (see led_map.c).

#pragma once

#include <stdbool.h>
#include <stdint.h>

#define LED_MAP_COUNT 74
#define LED_MAP_UNDERGLOW 0xFE
#define LED_MAP_NONE 0xFF

void led_map_init(void); // keyboard_post_init_user, both halves
void led_map_task(void); // housekeeping, both halves (master syncs the right half)
bool led_map_valid(const uint8_t *map);
void led_map_read(uint8_t offset, uint8_t *out, uint8_t len);
void led_map_write(uint8_t offset, const uint8_t *data, uint8_t len); // staged until commit
bool led_map_commit(void);                                            // validate, save, apply, sync
void led_map_reset(void);                                             // back to rgb_layout.c's built-in map
bool led_map_is_measured(void);
// Master: does the right half run the same map? (checksum it reports back
// after each full sync) 0 = not known yet, 1 = same, 2 = different.
uint8_t led_map_right_state(void);
