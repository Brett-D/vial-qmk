// Copyright 2026 Brett-D
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Crash forensics and stack checks (see diag.c).

#pragma once

#include <stdbool.h>
#include <stdint.h>

// What the keyboard was doing (set by the network code; shown after a crash).
enum {
    CRUMB_LOOP = 0,  // main loop / not in network code
    CRUMB_NET_RX,    // network: parsing a block from the computer (lwIP input)
    CRUMB_NET_TIMER, // network: lwIP timers
    CRUMB_NET_TX,    // network: sending to the computer
    CRUMB_HTTP,      // network: handling an HTTP request
    CRUMB_API,       // network: running a page command (VIA / Vial / 0xF5 / 0xF6)
    CRUMB_NET_START, // network: starting / stopping the stack
    CRUMB_COUNT
};

// How the keyboard last started.
enum { BOOT_POWER = 0, BOOT_RESET_PIN, BOOT_CRASH, BOOT_WATCHDOG, BOOT_OTHER };

typedef struct {
    bool     crashed; // the previous run ended in a HardFault (this boot is the reboot)
    uint8_t  crumb;   // CRUMB_* at the fault
    uint8_t  exc;     // exception number of the faulting code (0 = main loop, 16+ = interrupt)
    uint8_t  boot;    // BOOT_*
    uint32_t pc, lr, sp;
} diag_crash_t;

extern volatile uint8_t diag_crumb;

void                diag_init(void); // early: reads and clears the crash record
const diag_crash_t *diag_crash(void);
void                diag_clear(void);
const char         *diag_crumb_name(uint8_t crumb); // 5 characters, for the OLED

// Least free bytes seen so far (stacks are pre-filled with a pattern).
uint16_t diag_main_stack_free(void); // QMK main loop (process stack, 2 KB)
uint16_t diag_isr_stack_free(void);  // interrupts (1 KB)
uint16_t diag_net_stack_free(void);  // network code (its own stack)

// Runs fn on the network code's own stack (lwIP, HTTP and the API are too
// deep for the 2 KB main loop stack).
void diag_on_net_stack(void (*fn)(void));

// Raw HID 0xF6 0x17: fills the reply (data[2..]); arg data[2] = 1 clears.
void diag_raw_hid(uint8_t *data);
