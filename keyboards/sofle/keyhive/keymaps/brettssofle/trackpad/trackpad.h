// Copyright 2026 Brett-D
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Trackpad (trackpad.c: gestures, press-to-click; trackpad_z.c: Cirque Z and
// raw HID 0xF5) and the mouse wiggler (wiggler.c).

#pragma once

#include <stdbool.h>
#include <stdint.h>

void trackpad_set_drag_scroll(bool enable); // DRAG_SCROLL key

typedef struct {
    uint8_t  z;         // live Z while touching (0-63)
    uint8_t  peak;      // highest Z of the current/last touch
    uint8_t  last_peak; // peak of the previous completed touch
    bool     touching;
    bool     pressed;   // press-to-click is holding the left button
    uint16_t touches;   // touch counter, so a new touch is visible
} trackpad_z_t;

const trackpad_z_t *trackpad_z_get(void);
uint8_t             trackpad_z_update(bool touch);
void                trackpad_z_set_pressed(bool pressed);

// Mouse wiggler: intervals 30 s / 1 / 2 / 4 min without input.
#define WIGGLE_INTERVALS 4
bool    wiggler_on(void);
uint8_t wiggler_interval(void);
void    wiggler_set(bool enable, uint8_t interval);
void    wiggler_toggle(void);
void    wiggler_task(void); // housekeeping, master
