// Copyright 2026 Brett-D
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Cirque Z (signal strength / contact size, 0-63) for press-to-click, and the
// raw HID interface for its settings (saved by user_config.c), used by the
// config page's Trackpad tab:
//
//   get:  [0xF5, 0x00]
//   set:  [0xF5, 0x01, enabled, threshold, release_z]    (saved to EEPROM)
//   reply to both:
//         [0xF5, z, peak, last_peak, touching, touches_lo, touches_hi,
//          enabled, threshold, release_z, pressed]
//
// Built without the trackpad too (POINTING_DEVICE_ENABLE=no): Z then stays 0.

#include QMK_KEYBOARD_H
#include "trackpad.h"
#include "user_config.h"

#define TP_Z_REG_PACKETBYTE_5 0x17 // Cirque PACKETBYTE_5 (HostReg 0x17): Z in absolute mode
#define TP_Z_RAW_HID_ID 0xF5
#define TP_Z_CMD_SET 0x01

#ifdef POINTING_DEVICE_ENABLE
// Public in the Cirque I2C driver.
void RAP_ReadBytes(uint8_t address, uint8_t *data, uint8_t count);
#endif

static trackpad_z_t tp_z;

const trackpad_z_t *trackpad_z_get(void) {
    return &tp_z;
}

uint8_t trackpad_z_update(bool touch) {
    if (touch) {
        uint8_t packet5 = 0;
#ifdef POINTING_DEVICE_ENABLE
        RAP_ReadBytes(TP_Z_REG_PACKETBYTE_5, &packet5, 1);
#endif
        if (!tp_z.touching) {
            tp_z.peak = 0;
            tp_z.touches++;
        }
        tp_z.z = packet5 & 0x3F; // Z is the low 6 bits
        if (tp_z.z > tp_z.peak) {
            tp_z.peak = tp_z.z;
        }
    } else if (tp_z.touching) {
        tp_z.last_peak = tp_z.peak;
        tp_z.z         = 0;
    }
    tp_z.touching = touch;
    return tp_z.z;
}

void trackpad_z_set_pressed(bool pressed) {
    tp_z.pressed = pressed;
}

void raw_hid_receive_kb(uint8_t *data, uint8_t length) {
    if (data[0] != TP_Z_RAW_HID_ID || length < 11) {
        data[0] = id_unhandled;
        return;
    }
    if (data[1] == TP_Z_CMD_SET) {
        user_config_set_press(data[2], data[3], data[4]); // refused if out of range / release Z not below the threshold
    }
    data[1]  = tp_z.z;
    data[2]  = tp_z.peak;
    data[3]  = tp_z.last_peak;
    data[4]  = tp_z.touching;
    data[5]  = tp_z.touches & 0xFF;
    data[6]  = tp_z.touches >> 8;
    data[7]  = trackpad_press_enabled();
    data[8]  = trackpad_press_threshold();
    data[9]  = trackpad_press_release_z();
    data[10] = tp_z.pressed;
}
