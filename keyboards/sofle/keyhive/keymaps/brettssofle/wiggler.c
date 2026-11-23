// Copyright 2026 Brett-D
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Mouse wiggler: keeps the computer awake by nudging the pointer 1 px right
// and straight back after a chosen time without any key, knob or trackpad
// input (and again every interval while idle). The nudge isn't counted as
// input, so the LED and OLED idle timeouts still work. Turned on from the
// config page (Trackpad tab) or the WIGGLE key; always off at power-up.

#include "brettssofle.h"

#define WIGGLE_BACK_MS 30

static const uint16_t interval_s[WIGGLE_INTERVALS] = {30, 60, 120, 240};

static bool     on;
static uint8_t  idx = 1; // 1 min
static bool     back_pending;
static uint32_t last_nudge;

bool wiggler_on(void) {
    return on;
}

uint8_t wiggler_interval(void) {
    return idx;
}

void wiggler_set(bool enable, uint8_t interval) {
    on         = enable;
    idx        = interval < WIGGLE_INTERVALS ? interval : idx;
    last_nudge = timer_read32();
}

void wiggler_toggle(void) {
    wiggler_set(!on, idx);
}

#ifdef POINTING_DEVICE_ENABLE
static void nudge(int8_t dx) {
    report_mouse_t r = pointing_device_get_report();
    r.x              = dx;
    r.y              = 0;
    pointing_device_set_report(r);
    pointing_device_send();
}
#endif

void wiggler_task(void) {
#ifdef POINTING_DEVICE_ENABLE
    if (!is_keyboard_master()) {
        return;
    }
    if (back_pending) {
        if (timer_elapsed32(last_nudge) >= WIGGLE_BACK_MS) {
            nudge(-1);
            back_pending = false;
        }
        return;
    }
    uint32_t every = interval_s[idx] * 1000UL;
    if (on && last_input_activity_elapsed() >= every && timer_elapsed32(last_nudge) >= every) {
        nudge(1);
        back_pending = true;
        last_nudge   = timer_read32();
    }
#endif
}
