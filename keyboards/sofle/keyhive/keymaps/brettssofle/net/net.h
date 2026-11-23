// Copyright 2026 Brett-D
// SPDX-License-Identifier: GPL-2.0-or-later
//
// USB network config mode (see net/ncm.c).

#pragma once

#include <stdbool.h>
#include <stdint.h>

enum net_state {
    NET_OFF,
    NET_USB,   // re-enumerating / waiting for the computer to open the link
    NET_LINK,  // NCM data interface on: waiting for a DHCP request
    NET_READY, // the computer has 192.168.7.2: keyboard at 192.168.7.1
};

void    net_mode_enter(void); // re-enumerate with the network function
void    net_mode_exit(void);  // back to the plain keyboard
bool    net_mode_active(void);
uint8_t net_state(void);
bool    net_failed(void); // no address handed out within NET_FALLBACK_MS
void    net_task(void);   // main loop (time-sliced)
