// Copyright 2026 Brett-D
// SPDX-License-Identifier: GPL-2.0-or-later
//
// The per-half WS2812 driver with the smooth output stage (rgb_driver.c).

#pragma once

// Housekeeping (both halves) and suspend_power_down_user: dither re-sends
// between frames, dark as soon as the LEDs are off, the output re-sent while
// no frames come (a lost transfer can't stick).
void rgb_driver_task(void);
