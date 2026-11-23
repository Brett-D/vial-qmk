// Copyright 2026 Brett-D
// SPDX-License-Identifier: GPL-2.0-or-later

#include QMK_KEYBOARD_H
#include "bridge.h"
#include "usb_driver.h"
#include "usb_endpoints.h"

// One report (key down or key up) at most every BRIDGE_REPORT_INTERVAL_MS,
// and only once the previous one has left the endpoint, so the USB queue never
// fills and send_keyboard_report() never blocks. 2 ms ~= 250 keys/s.
#ifndef BRIDGE_REPORT_INTERVAL_MS
#    define BRIDGE_REPORT_INTERVAL_MS 2
#endif

extern usb_endpoint_in_t usb_endpoints_in[USB_ENDPOINT_IN_COUNT];

static uint8_t  frame[BRIDGE_MAX_PAYLOAD + 3]; // type, seq, payload, crc
static uint8_t  frame_len;                     // 0 = no frame in progress
static uint16_t symbol_count;                  // base32 symbols in the frame
static uint16_t next_char;                     // 0 = ',', 1..n = symbols, n+1 = '.'
static uint8_t  seq;
static uint8_t  held_key;
static uint16_t last_report;

static uint8_t crc8(const uint8_t *data, uint8_t len) {
    uint8_t crc = 0;
    while (len--) {
        crc ^= *data++;
        for (uint8_t i = 0; i < 8; i++) {
            crc = (crc & 0x80) ? (crc << 1) ^ 0x07 : crc << 1;
        }
    }
    return crc;
}

static uint8_t symbol_at(uint16_t index) {
    uint16_t bit   = index * 5;
    uint8_t  value = 0;
    for (uint8_t i = 0; i < 5; i++, bit++) {
        uint8_t byte = bit >> 3;
        value <<= 1;
        if (byte < frame_len && (frame[byte] & (0x80 >> (bit & 7)))) {
            value |= 1;
        }
    }
    return value;
}

static uint8_t symbol_key(uint8_t symbol) {
    return symbol < 26 ? KC_A + symbol : KC_2 + (symbol - 26);
}

static bool endpoint_idle(void) {
#ifdef NKRO_ENABLE
    if (keymap_config.nkro) {
        return usb_endpoint_in_is_inactive(&usb_endpoints_in[USB_ENDPOINT_IN_SHARED]);
    }
#endif
    return usb_endpoint_in_is_inactive(&usb_endpoints_in[USB_ENDPOINT_IN_KEYBOARD]);
}

static bool load_frame(void) {
    uint8_t buf[BRIDGE_MAX_PAYLOAD + 1];
    uint8_t len = bridge_next_frame(buf);
    if (len == 0 || len > BRIDGE_MAX_PAYLOAD + 1) {
        return false;
    }
    frame[0] = buf[0];
    frame[1] = seq++;
    memcpy(&frame[2], &buf[1], len - 1);
    frame[len + 1] = crc8(frame, len + 1);
    frame_len      = len + 2;
    symbol_count   = (frame_len * 8 + 4) / 5;
    next_char      = 0;
    return true;
}

static void send_key(uint8_t key, bool down) {
    if (down) {
        add_key(key);
        held_key = key;
    } else {
        del_key(key);
        held_key = 0;
    }
    send_keyboard_report();
    last_report = timer_read();
}

void bridge_task(void) {
    if (timer_elapsed(last_report) < BRIDGE_REPORT_INTERVAL_MS || !endpoint_idle()) {
        return;
    }
    if (held_key) {
        send_key(held_key, false);
        return;
    }
    if (frame_len == 0 && !load_frame()) {
        return;
    }

    uint8_t key;
    if (next_char == 0) {
        key = KC_COMMA;
    } else if (next_char <= symbol_count) {
        key = symbol_key(symbol_at(next_char - 1));
    } else {
        key = KC_DOT;
    }
    next_char++;
    if (next_char > symbol_count + 1) {
        frame_len = 0;
    }
    send_key(key, true);
}

void bridge_abort(void) {
    // A held key is released by bridge_task() at the normal report pace.
    frame_len = 0;
}

bool bridge_busy(void) {
    return frame_len != 0 || held_key != 0;
}
