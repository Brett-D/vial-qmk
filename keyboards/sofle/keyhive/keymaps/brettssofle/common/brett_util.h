// Copyright 2026 Brett-D
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Small helpers shared by several modules.

#pragma once

#include <stdint.h>
#include "timer.h"

// A nonzero timer_read32() stamp (0 = "not set") that is never in the future:
// "timer_read32() | 1" can be 1 ms ahead, so timer_elapsed32() wraps to ~49
// days and the timeout fires at once.
static inline uint32_t timer_stamp32(void) {
    uint32_t t = timer_read32();
    return t ? t : UINT32_MAX;
}

// CRC-8, polynomial 0x07, initial 0, MSB first: the keystroke bridge's frame
// check (bridge.c, read by the page) and the LED map sync check between the
// halves (led_map.c). Both are protocol: don't change it.
uint8_t util_crc8(const uint8_t *data, uint8_t len);
