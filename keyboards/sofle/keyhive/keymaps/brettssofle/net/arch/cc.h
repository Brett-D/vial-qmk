// Copyright 2026 Brett-D
// SPDX-License-Identifier: GPL-2.0-or-later
//
// lwIP port for the keyboard: bare metal, little endian (RP2040), no OS.

#pragma once

#include <stdint.h>
#include <stdlib.h>

#define LWIP_NO_INTTYPES_H 1
#define X8_F "02x"
#define U16_F "u"
#define S16_F "d"
#define X16_F "x"
#define U32_F "lu"
#define S32_F "ld"
#define X32_F "lx"
#define SZT_F "u"

#ifndef BYTE_ORDER
#    define BYTE_ORDER LITTLE_ENDIAN
#endif

#define LWIP_RAND() ((u32_t)rand())
#define LWIP_PLATFORM_DIAG(x) \
    do {                      \
    } while (0)
#define LWIP_PLATFORM_ASSERT(x) \
    do {                        \
    } while (0)
