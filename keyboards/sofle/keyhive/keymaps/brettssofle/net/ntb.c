// Copyright 2026 Brett-D
// SPDX-License-Identifier: GPL-2.0-or-later

#include <string.h>
#include "ntb.h"

#define NTH16_SIG 0x484D434EUL // "NCMH"
#define NDP16_SIG 0x304D434EUL // "NCM0" (no CRC)
#define NDP16_SIG_CRC 0x314D434EUL // "NCM1"

static uint16_t get16(const uint8_t *p) {
    return p[0] | p[1] << 8;
}

static uint32_t get32(const uint8_t *p) {
    return get16(p) | (uint32_t)get16(p + 2) << 16;
}

static void put16(uint8_t *p, uint16_t v) {
    p[0] = v;
    p[1] = v >> 8;
}

static void put32(uint8_t *p, uint32_t v) {
    put16(p, v);
    put16(p + 2, v >> 16);
}

uint8_t ntb_parse(const uint8_t *b, uint16_t size, ntb_datagram_fn fn) {
    if (size < 12 || get32(b) != NTH16_SIG || get16(b + 4) != 12) {
        return 0;
    }
    uint16_t block = get16(b + 8);
    if (block == 0 || block > size) {
        block = size;
    }
    uint16_t ndp   = get16(b + 10);
    uint8_t  count = 0;
    for (uint8_t guard = 0; ndp && guard < 8; guard++) {
        if (ndp & 3 || ndp + 8 > block) {
            break;
        }
        uint32_t sig = get32(b + ndp);
        uint16_t len = get16(b + ndp + 4);
        if ((sig != NDP16_SIG && sig != NDP16_SIG_CRC) || len < 16 || ndp + len > block) {
            break;
        }
        for (uint16_t i = ndp + 8; i + 4 <= ndp + len; i += 4) {
            uint16_t at = get16(b + i), n = get16(b + i + 2);
            if (at == 0 || n == 0) {
                break;
            }
            if (at + n <= block && n >= 14) {
                fn(b + at, n);
                count++;
            }
        }
        ndp = get16(b + ndp + 6);
    }
    return count;
}

uint16_t ntb_build(uint8_t *b, const uint8_t *frame, uint16_t len, uint16_t seq) {
    put32(b, NTH16_SIG);
    put16(b + 4, 12);
    put16(b + 6, seq);
    put16(b + 10, 12); // NDP right after the header
    put32(b + 12, NDP16_SIG);
    put16(b + 16, 16);
    put16(b + 18, 0); // no next NDP
    put16(b + 20, NTB_DATAGRAM_OFFSET);
    put16(b + 22, len);
    put32(b + 24, 0); // terminator
    put32(b + 28, 0); // padding up to the datagram
    memcpy(b + NTB_DATAGRAM_OFFSET, frame, len);
    uint16_t size = NTB_DATAGRAM_OFFSET + len;
    if (size % 64 == 0) {
        b[size++] = 0; // a short last packet ends the transfer
    }
    put16(b + 8, size);
    return size;
}
