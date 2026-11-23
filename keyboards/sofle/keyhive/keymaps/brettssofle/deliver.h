// Copyright 2026 Brett-D
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Typed page delivery (see deliver.c).

#pragma once

#include <stdbool.h>
#include <stdint.h>

enum deliver_state {
    DL_OFF,
    DL_TYPE,     // typing the bootstrap address
    DL_WAIT_NAV, // waiting for Brett's Enter in the address bar
    DL_WAIT_GO,  // the page is open: waiting for Enter there
    DL_STREAM,   // typing the page
    DL_DONE,     // sent; the right knob = send repair blocks
};

enum deliver_kind { DL_LIGHT, DL_FULL }; // the light page (default) or the full one

void    deliver_start(uint8_t kind); // after a physical confirm (CONF? prompt: P light, F full)
void    deliver_stop(void);
void    deliver_task(void);
// pre_process_record_user (master): true = event swallowed.
bool    deliver_process(keyrecord_t *record);
uint8_t deliver_state(void);
uint8_t deliver_percent(void); // of the current typing step
uint8_t deliver_pass(void);    // 1 = the page, 2+ = repair rounds
// Shared with the host tests / bootstrap.html.
uint32_t deliver_hash(uint32_t r, uint32_t s);
uint32_t deliver_crc32(const uint8_t *b, uint8_t n);
