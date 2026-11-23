// Copyright 2026 Brett-D
// SPDX-License-Identifier: GPL-2.0-or-later
//
// TRACKPAD_Z_DEBUG: live Cirque Z (signal strength / contact size, 0-63) for
// testing whether a light touch and a firm press can be told apart.
//
//   - The master reads Z while a finger is down and keeps the peak per touch.
//   - The values are synced to the right half and shown on its OLED.
//   - Raw HID command 0xF5 returns them (used by trackpad_z.html over WebHID):
//       request:  [0xF5]
//       response: [0xF5, z, peak, last_peak, touching, touches_lo, touches_hi]

#include "brettssofle.h"

#if defined(TRACKPAD_Z_DEBUG) && defined(POINTING_DEVICE_ENABLE)

#    include "transactions.h"

// Cirque PACKETBYTE_5 (HostReg 0x17): Z in absolute mode.
#    define TP_Z_REG_PACKETBYTE_5 0x17

#    define TP_Z_RAW_HID_ID 0xF5
#    ifndef TP_Z_SYNC_INTERVAL_MS
#        define TP_Z_SYNC_INTERVAL_MS 30
#    endif

// Public in the Cirque I2C driver.
void RAP_ReadBytes(uint8_t address, uint8_t *data, uint8_t count);

static trackpad_z_t tp_z;

const trackpad_z_t *trackpad_z_get(void) {
    return &tp_z;
}

void trackpad_z_update(bool touch) {
    if (touch) {
        uint8_t packet5 = 0;
        RAP_ReadBytes(TP_Z_REG_PACKETBYTE_5, &packet5, 1);
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
}

static void tp_z_slave_handler(uint8_t in_len, const void *in_data, uint8_t out_len, void *out_data) {
    if (in_len == sizeof(tp_z)) {
        memcpy(&tp_z, in_data, sizeof(tp_z));
    }
}

void trackpad_z_post_init(void) {
    transaction_register_rpc(RPC_ID_USER_TP_Z, tp_z_slave_handler);
}

void trackpad_z_sync_task(void) {
    static trackpad_z_t last_sent;
    static uint16_t     last_time;
    if (!is_keyboard_master() || timer_elapsed(last_time) < TP_Z_SYNC_INTERVAL_MS || memcmp(&last_sent, &tp_z, sizeof(tp_z)) == 0) {
        return;
    }
    if (transaction_rpc_send(RPC_ID_USER_TP_Z, sizeof(tp_z), &tp_z)) {
        last_sent = tp_z;
    }
    last_time = timer_read();
}

void raw_hid_receive_kb(uint8_t *data, uint8_t length) {
    if (data[0] == TP_Z_RAW_HID_ID && length >= 7) {
        data[1] = tp_z.z;
        data[2] = tp_z.peak;
        data[3] = tp_z.last_peak;
        data[4] = tp_z.touching;
        data[5] = tp_z.touches & 0xFF;
        data[6] = tp_z.touches >> 8;
        return;
    }
    data[0] = id_unhandled;
}

#endif
