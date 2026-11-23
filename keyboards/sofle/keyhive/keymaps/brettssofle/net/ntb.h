// Copyright 2026 Brett-D
// SPDX-License-Identifier: GPL-2.0-or-later
//
// CDC-NCM transfer blocks (NTB16, NCM 1.0 spec 3.2-3.3): parse the blocks the
// host sends and build the ones the keyboard sends. Pure functions (no USB),
// so the host tests can use them too.

#pragma once

#include <stdbool.h>
#include <stdint.h>

#define NTB_MAX_SIZE 2048
#define NTB_DATAGRAM_OFFSET 32 // NTH16 (12) + NDP16 with one datagram (16), aligned to 4

typedef void (*ntb_datagram_fn)(const uint8_t *frame, uint16_t len);

// Calls fn for every datagram (Ethernet frame) in the block; returns how many.
uint8_t ntb_parse(const uint8_t *block, uint16_t size, ntb_datagram_fn fn);
// One frame per block; returns the block size (never a multiple of 64, so no
// zero-length packet is needed to end the transfer).
uint16_t ntb_build(uint8_t *block, const uint8_t *frame, uint16_t len, uint16_t seq);
