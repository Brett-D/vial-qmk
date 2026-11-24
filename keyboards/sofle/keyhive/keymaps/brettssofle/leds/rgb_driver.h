// Copyright 2026 Brett-D
// SPDX-License-Identifier: GPL-2.0-or-later
//
// The per-half WS2812 driver with the smooth output stage (rgb_driver.c).

#pragma once

void rgb_driver_dither_task(void); // housekeeping, both halves: re-sends the frame with new dither values between frames
void rgb_driver_suspend_task(void); // housekeeping + suspend_power_down_user: LEDs dark once USB is suspended
