// Copyright 2026 Brett-D
// SPDX-License-Identifier: GPL-2.0-or-later
//
// LED calibration and LED map test for the config page (see led_cal.c).
// Include after QMK_KEYBOARD_H (brettssofle.h).

#pragma once

#include <stdbool.h>
#include <stdint.h>

// Calibration state, also synced to the right half (config_mode.c).
#define LED_CAL_OFF 0
#define LED_CAL_DARK 1          // all LEDs off
#define LED_CAL_TEST 2          // light the mapped LED of each pressed key
#define LED_CAL_LED(i) (3 + (i)) // light physical LED i alone

// Capture flags (status byte 20).
#define LED_CAL_CAP_DOWN 0x01 // the key is down (press event); 0 = released
#define LED_CAL_CAP_LONG 0x02 // released after a hold of LED_CAL_HOLD_MS or more

void    led_cal_light(uint8_t index); // physical LED 0-73 alone; anything else = all dark
void    led_cal_test(void);
void    led_cal_end(void);
uint8_t led_cal_code(void); // master's state, for the right half
// pre_process_record_user, master: true = event swallowed (captured).
bool led_cal_process(keyrecord_t *record);
// rgb_matrix_indicators_advanced_user, both halves (code: master's state).
bool led_cal_indicators(uint8_t led_min, uint8_t led_max, uint8_t code);
// Status bytes 15 and 17-21 of the 0xF6 reply.
void led_cal_status(uint8_t *data);
