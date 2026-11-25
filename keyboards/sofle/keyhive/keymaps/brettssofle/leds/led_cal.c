// Copyright 2026 Brett-D
// SPDX-License-Identifier: GPL-2.0-or-later
//
// LED calibration and LED map test (config page, LEDs tab; web session only).
//
// Calibration lights one physical LED by raw chain index (0-36 left chain,
// 37-73 right chain) through the same rgb_matrix_set_color() path effects use,
// but without g_led_config, so it doesn't depend on the current map. Meanwhile
// key presses are captured instead of typed: the page reads the matrix row/col
// of each press and release from the status reply, so you just press the key
// under the lit LED. A release after holding LED_CAL_HOLD_MS or more is flagged
// "long" (the page uses it for "not under a key / underglow").
//
// Test mode checks a saved map end to end: each half lights the LED its own
// g_led_config maps to every pressed key (the lookup reactive effects use,
// with the right half reading the mirrored matrix), key LEDs glow dim white
// and underglow LEDs dim blue. Key presses are captured as above.

#include "brettssofle.h"
#include "led_cal.h"
#include "led_map.h"

#ifdef RGB_MATRIX_ENABLE

#    ifndef LED_CAL_HOLD_MS
#        define LED_CAL_HOLD_MS 800
#    endif

static uint8_t cal_mode = LED_CAL_OFF;
static bool    cal_was_enabled;

static struct {
    uint8_t  seq, row, col, flags;
    uint16_t down_time;
} cal_cap;
static matrix_row_t cal_swallowed[MATRIX_ROWS];

static void cal_begin(void) {
    if (cal_mode == LED_CAL_OFF) {
        cal_was_enabled = rgb_matrix_is_enabled();
        if (!cal_was_enabled) {
            rgb_matrix_enable_noeeprom();
        }
    }
}

void led_cal_light(uint8_t index) {
    cal_begin();
    cal_mode = index < LED_MAP_COUNT ? LED_CAL_LED(index) : LED_CAL_DARK;
}

void led_cal_test(void) {
    cal_begin();
    cal_mode = LED_CAL_TEST;
}

void led_cal_end(void) {
    if (cal_mode != LED_CAL_OFF) {
        cal_mode = LED_CAL_OFF;
        if (!cal_was_enabled) {
            rgb_matrix_disable_noeeprom();
        }
    }
}

uint8_t led_cal_code(void) {
    return cal_mode;
}

static void cal_capture(uint8_t row, uint8_t col, uint8_t flags) {
    cal_cap.row   = row;
    cal_cap.col   = col;
    cal_cap.flags = flags;
    cal_cap.seq++;
}

bool led_cal_process(keyrecord_t *record) {
    keyevent_t  *ev     = &record->event;
    bool         is_key = ev->type == KEY_EVENT && ev->key.row < MATRIX_ROWS && ev->key.col < MATRIX_COLS;
    uint8_t      row = ev->key.row, col = ev->key.col;
    matrix_row_t bit = is_key ? (matrix_row_t)1 << col : 0;

    if (is_key && !ev->pressed && (cal_swallowed[row] & bit)) {
        // Release of a captured press (even if calibration ended meanwhile).
        cal_swallowed[row] &= ~bit;
        if (cal_mode != LED_CAL_OFF) {
            cal_capture(row, col, timer_elapsed(cal_cap.down_time) >= LED_CAL_HOLD_MS ? LED_CAL_CAP_LONG : 0);
        }
        return true;
    }
    if (cal_mode == LED_CAL_OFF) {
        return false;
    }
    if (is_key && ev->pressed) {
        cal_swallowed[row] |= bit;
        cal_cap.down_time = timer_read();
        cal_capture(row, col, LED_CAL_CAP_DOWN);
        return true;
    }
    // Releases of keys pressed before calibration started still reach the
    // host (no stuck keys); knob turns and the rest are dropped.
    return !(is_key && !ev->pressed);
}

bool led_cal_indicators(uint8_t led_min, uint8_t led_max, uint8_t code) {
    if (code == LED_CAL_OFF) {
        return false;
    }
    const uint8_t max = RGB_MATRIX_MAXIMUM_BRIGHTNESS;
    for (uint8_t i = led_min; i < led_max; i++) {
        if (code == LED_CAL_TEST) {
            if (HAS_FLAGS(g_led_config.flags[i], LED_FLAG_UNDERGLOW)) {
                rgb_matrix_set_color(i, 0, 0, max / 3);
            } else if (HAS_FLAGS(g_led_config.flags[i], LED_FLAG_KEYLIGHT)) {
                rgb_matrix_set_color(i, max / 10, max / 10, max / 10);
            } else {
                rgb_matrix_set_color(i, 0, 0, 0);
            }
        } else {
            uint8_t v = code >= LED_CAL_LED(0) && i == code - LED_CAL_LED(0) ? max : 0;
            rgb_matrix_set_color(i, v, v, v);
        }
    }
    if (code == LED_CAL_TEST) {
        for (uint8_t r = 0; r < MATRIX_ROWS; r++) {
            for (uint8_t c = 0; c < MATRIX_COLS; c++) {
                uint8_t led = g_led_config.matrix_co[r][c];
                if (led != NO_LED && led >= led_min && led < led_max && matrix_is_on(r, c)) {
                    rgb_matrix_set_color(led, 0, max, 0);
                }
            }
        }
    }
    return true;
}

void led_cal_status(uint8_t *data) {
    data[15] = cal_mode;
    data[17] = cal_cap.seq;
    data[18] = cal_cap.row;
    data[19] = cal_cap.col;
    data[20] = cal_cap.flags;
    data[21] = led_map_right_state();
}

#endif
