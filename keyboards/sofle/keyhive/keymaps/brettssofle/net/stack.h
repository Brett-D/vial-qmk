// Copyright 2026 Brett-D
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Network stack for the USB network config mode (lwIP glue, see stack.c).
// Platform independent: the firmware feeds it NCM frames (ncm.c), the host
// test a Linux TAP device.

#pragma once

#include <stdbool.h>
#include <stdint.h>

#define NET_IP_DEVICE 1 // 192.168.7.1: the keyboard
#define NET_IP_HOST 2   // 192.168.7.2: the computer (DHCP)

typedef void (*net_tx_fn)(const uint8_t *frame, uint16_t len);

void     netstack_start(net_tx_fn tx); // link up (lwIP is set up once)
void     netstack_stop(void);
void     netstack_input(const uint8_t *frame, uint16_t len);
void     netstack_poll(void); // lwIP timers; call from the main loop
uint8_t  netstack_leases(void);        // DHCP addresses handed out since start
uint32_t netstack_idle_ms(void);       // since the last frame from the computer (0 = none yet)
uint32_t net_millis(void);             // platform clock
extern const uint8_t net_host_mac[6];  // the computer's side (NCM iMACAddress)
extern const uint8_t net_device_mac[6];

// What the network code is doing, for crash reports (diag.c; no-op in tests).
#ifdef NET_DIAG
#    include "diag.h"
#    define NET_CRUMB(x) (diag_crumb = (x))
#else
#    define NET_CRUMB(x) ((void)0)
#endif
