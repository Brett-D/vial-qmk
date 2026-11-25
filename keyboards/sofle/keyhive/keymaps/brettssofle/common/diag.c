// Copyright 2026 Brett-D
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Crash forensics. A HardFault (the RP2040's only fault) stores where it
// happened (PC, LR, SP, interrupt number and what the network code was doing)
// in the watchdog scratch registers, which survive a watchdog reset, and
// reboots. After the reboot the right OLED shows it and the config page
// reads it (raw HID / network API 0xF6 0x17), so one failed attempt tells
// where it died. Without this, a fault just hangs the keyboard.
//
// Also: stack high-water marks (ChibiOS fills the stacks with 0x55555555 at
// startup) and a separate stack for the network code.

#include "quantum.h"
#include "diag.h"
#include "hardware/structs/watchdog.h"
#include "hardware/structs/psm.h"
#include "hardware/structs/vreg_and_chip_reset.h"
#ifdef NETWORK_ENABLE
#    include "net/net.h"
#endif

#define DIAG_MAGIC 0x5F1E0000u // scratch0 high half: a fault record is present
#define STACK_FILL 0x55555555u
#ifndef DIAG_NET_STACK_SIZE
#    define DIAG_NET_STACK_SIZE 6144
#endif

volatile uint8_t    diag_crumb;
static diag_crash_t crash;

extern uint32_t __process_stack_base__[], __process_stack_end__[];
extern uint32_t __main_stack_base__[], __main_stack_end__[];

static uint32_t net_stack[DIAG_NET_STACK_SIZE / 4] __attribute__((aligned(8)));
static bool     net_stack_painted;

/* ---- fault handler ---- */

static __attribute__((noreturn)) void reboot_now(void) {
    watchdog_hw->scratch[4] = 0; // a normal boot, not a jump (bootrom)
    hw_clear_bits(&watchdog_hw->ctrl, WATCHDOG_CTRL_ENABLE_BITS);
    psm_hw->wdsel = PSM_WDSEL_BITS & ~(PSM_WDSEL_ROSC_BITS | PSM_WDSEL_XOSC_BITS);
    hw_set_bits(&watchdog_hw->ctrl, WATCHDOG_CTRL_TRIGGER_BITS);
    while (true) {
    }
}

// Called by HardFault_Handler with the exception frame (on the stack that was
// in use) and EXC_RETURN.
__attribute__((used, noreturn)) void diag_fault(uint32_t *frame, uint32_t exc_return) {
    uintptr_t f  = (uintptr_t)frame;
    uint32_t  pc = 0, lr = 0, psr = 0;
    if (!(f & 3) && f >= 0x20000000u && f <= 0x20042000u - 32) { // whole frame in SRAM (no wrap-around)
        lr  = frame[5];
        pc  = frame[6];
        psr = frame[7];
    }
    watchdog_hw->scratch[0] = DIAG_MAGIC | (psr & 0x3F) << 8 | diag_crumb;
    watchdog_hw->scratch[1] = pc;
    watchdog_hw->scratch[2] = lr;
    watchdog_hw->scratch[3] = (uint32_t)f;
    reboot_now();
}

// Overrides ChibiOS's weak handler (which just spins). diag_fault runs on a
// fresh stack (the top of the interrupt stack): the fault may have come from
// a bad stack pointer, and the faulting context is not resumed anyway.
__attribute__((naked)) void HardFault_Handler(void) {
    __asm volatile(
        "movs r0, #4      \n"
        "mov  r1, lr      \n"
        "tst  r0, r1      \n"
        "beq  1f          \n"
        "mrs  r0, psp     \n" // the fault happened in thread mode (main loop)
        "b    2f          \n"
        "1:               \n"
        "mrs  r0, msp     \n" // in an interrupt
        "2:               \n"
        "ldr  r2, =__main_stack_end__ \n"
        "mov  sp, r2      \n"
        "ldr  r2, =diag_fault \n"
        "bx   r2          \n"
        ".ltorg           \n");
}

/* ---- right half's watchdog (v29) ---- */

#ifndef DIAG_WATCHDOG_MS
#    define DIAG_WATCHDOG_MS 3000
#endif
// RP2040-E1: the watchdog counts down twice per 1 us tick.
#define WDT_LOAD ((uint32_t)DIAG_WATCHDOG_MS * 2000u)
_Static_assert(WDT_LOAD <= 0xFFFFFFu, "watchdog time too long");
static bool wdt_on;

void diag_watchdog_start(void) {
    watchdog_hw->load = WDT_LOAD;
    psm_hw->wdsel     = PSM_WDSEL_BITS & ~(PSM_WDSEL_ROSC_BITS | PSM_WDSEL_XOSC_BITS);
    hw_set_bits(&watchdog_hw->ctrl, WATCHDOG_CTRL_ENABLE_BITS | WATCHDOG_CTRL_PAUSE_DBG0_BITS | WATCHDOG_CTRL_PAUSE_DBG1_BITS | WATCHDOG_CTRL_PAUSE_JTAG_BITS);
    wdt_on = true;
}

void diag_watchdog_feed(void) {
    if (wdt_on) {
        watchdog_hw->load = WDT_LOAD;
    }
}

// Before QMK jumps to the bootloader or reboots (QK_BOOT / QK_REBOOT on the right half).
bool shutdown_user(bool jump_to_bootloader) {
    hw_clear_bits(&watchdog_hw->ctrl, WATCHDOG_CTRL_ENABLE_BITS);
    wdt_on = false;
    return true;
}

/* ---- boot ---- */

void diag_init(void) {
    uint32_t s0 = watchdog_hw->scratch[0];
    if ((s0 & 0xFFFF0000u) == DIAG_MAGIC) {
        crash.crashed = true;
        crash.crumb   = s0 & 0xFF;
        crash.exc     = (s0 >> 8) & 0x3F;
        crash.pc      = watchdog_hw->scratch[1];
        crash.lr      = watchdog_hw->scratch[2];
        crash.sp      = watchdog_hw->scratch[3];
        watchdog_hw->scratch[0] = 0;
    }
    uint32_t chip = vreg_and_chip_reset_hw->chip_reset;
    if (crash.crashed) {
        crash.boot = BOOT_CRASH;
    } else if (chip & VREG_AND_CHIP_RESET_CHIP_RESET_HAD_PSM_RESTART_BITS) {
        crash.boot = BOOT_OTHER; // debugger
    } else if (watchdog_hw->reason & WATCHDOG_REASON_TIMER_BITS) {
        crash.boot = BOOT_WATCHDOG;
    } else if (watchdog_hw->reason & WATCHDOG_REASON_FORCE_BITS) {
        crash.boot = BOOT_OTHER; // a reboot on purpose (QK_REBOOT, ...)
    } else if (chip & VREG_AND_CHIP_RESET_CHIP_RESET_HAD_RUN_BITS) {
        crash.boot = BOOT_RESET_PIN;
    } else if (chip & VREG_AND_CHIP_RESET_CHIP_RESET_HAD_POR_BITS) {
        crash.boot = BOOT_POWER;
    } else {
        crash.boot = BOOT_OTHER; // SYSRESETREQ (soft reset)
    }
}

const diag_crash_t *diag_crash(void) {
    return &crash;
}

void diag_clear(void) {
    crash.crashed = false;
}

const char *diag_crumb_name(uint8_t crumb) {
    static const char names[CRUMB_COUNT][6] = {"loop ", "netRx", "netTm", "netTx", "http ", "api  ", "netOn"};
    return crumb < CRUMB_COUNT ? names[crumb] : "?    ";
}

/* ---- stacks ---- */

static uint16_t unused_bytes(const uint32_t *base, const uint32_t *end) {
    const uint32_t *p = base;
    while (p < end && *p == STACK_FILL) {
        p++;
    }
    return (uint16_t)((p - base) * 4);
}

uint16_t diag_main_stack_free(void) {
    return unused_bytes(__process_stack_base__, __process_stack_end__);
}

uint16_t diag_isr_stack_free(void) {
    return unused_bytes(__main_stack_base__, __main_stack_end__);
}

uint16_t diag_net_stack_free(void) {
    return net_stack_painted ? unused_bytes(net_stack, net_stack + sizeof(net_stack) / 4) : sizeof(net_stack);
}

// Switches SP (the process stack in thread mode) to `top`, calls fn, and
// switches back. Interrupts keep using their own stack.
__attribute__((naked, noinline)) static void call_on_stack(void (*fn)(void), void *top) {
    __asm volatile(
        "push {r4, lr} \n"
        "mov  r4, sp   \n"
        "mov  sp, r1   \n"
        "blx  r0       \n"
        "mov  sp, r4   \n"
        "pop  {r4, pc} \n");
}

void diag_on_net_stack(void (*fn)(void)) {
    if (!net_stack_painted) {
        for (uint16_t i = 0; i < sizeof(net_stack) / 4; i++) {
            net_stack[i] = STACK_FILL;
        }
        net_stack_painted = true;
    }
    call_on_stack(fn, net_stack + sizeof(net_stack) / 4);
}

/* ---- raw HID 0xF6 0x17 ---- */

static void put32(uint8_t *p, uint32_t v) {
    p[0] = v;
    p[1] = v >> 8;
    p[2] = v >> 16;
    p[3] = v >> 24;
}

static void put16(uint8_t *p, uint16_t v) {
    p[0] = v;
    p[1] = v >> 8;
}

void diag_raw_hid(uint8_t *d) {
    if (d[2] == 1) {
        diag_clear();
    }
    memset(d + 2, 0, 30);
    d[2] = 0xD1; // this firmware has the diagnostics command
    d[3] = crash.crashed;
    d[4] = crash.crumb;
    d[5] = crash.exc;
    put32(d + 6, crash.pc);
    put32(d + 10, crash.lr);
    put32(d + 14, crash.sp);
    put16(d + 18, diag_main_stack_free());
    put16(d + 20, diag_net_stack_free());
    put16(d + 22, diag_isr_stack_free());
    d[24] = crash.boot;
#ifdef NETWORK_ENABLE
    uint32_t ago;
    d[25] = net_last_exit(&ago);
    put16(d + 26, ago / 1000 > 0xFFFF ? 0xFFFF : ago / 1000);
    d[28] = net_mode_active() ? net_state() : 0;
#endif
}
