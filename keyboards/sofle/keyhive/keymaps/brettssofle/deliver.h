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
    DL_DONE,     // sent; Enter on the page = send again
};

void    deliver_start(void); // after a physical confirm (CONF? prompt, P)
void    deliver_stop(void);
void    deliver_task(void);
// pre_process_record_user (master): true = event swallowed.
bool    deliver_process(keyrecord_t *record);
uint8_t deliver_state(void);
uint8_t deliver_percent(void); // of the current typing step
