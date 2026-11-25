// Copyright 2026 Brett-D
// SPDX-License-Identifier: GPL-2.0-or-later

#include "brett_util.h"

uint8_t util_crc8(const uint8_t *data, uint8_t len) {
    uint8_t crc = 0;
    while (len--) {
        crc ^= *data++;
        for (uint8_t i = 0; i < 8; i++) {
            crc = crc & 0x80 ? (uint8_t)(crc << 1) ^ 0x07 : (uint8_t)(crc << 1);
        }
    }
    return crc;
}
