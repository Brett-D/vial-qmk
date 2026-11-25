// Copyright 2026 Brett-D
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Keystroke bridge: sends small binary frames to the configurator page by
// typing them, one key per report, without blocking the main loop.
//
// Wire format: ',' + base32(type, seq, payload..., crc8) + '.'
//   base32 symbols 0-25 = keys A-Z, 26-31 = keys 2-7 (physical keys; the page
//   reads KeyboardEvent.code, so the host keyboard layout doesn't matter).
// Only letters, digits, comma and period are ever typed: no Enter, Tab,
// modifiers or function keys.

#pragma once

#include <stdbool.h>
#include <stdint.h>

#define BRIDGE_MAX_PAYLOAD 40

// Provided by config_mode.c: fill buf with the next frame (type byte first,
// then payload). Returns its length, or 0 if there is nothing to send.
uint8_t bridge_next_frame(uint8_t *buf);

void bridge_task(void);
// Drops the frame being typed (the page discards partial frames). A key the
// bridge is holding is still released by bridge_task().
void bridge_abort(void);
bool bridge_busy(void);
