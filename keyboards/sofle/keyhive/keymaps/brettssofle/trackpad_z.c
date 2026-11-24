// Copyright 2026 Brett-D
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Cirque Z (signal strength / contact size, 0-63) for press-to-click, plus the
// press-to-click settings (saved in EECONFIG_USER) and a raw HID interface
// used by trackpad_z.html over WebHID:
//
//   get:  [0xF5, 0x00]
//   set:  [0xF5, 0x01, enabled, threshold, release_z]    (saved to EEPROM)
//   reply to both:
//         [0xF5, z, peak, last_peak, touching, touches_lo, touches_hi,
//          enabled, threshold, release_z, pressed]
//
// With TRACKPAD_Z_DEBUG the values are also synced to the right half for its
// OLED.

#include "brettssofle.h"

// The settings (EECONFIG_USER) and raw HID 0xF5 are built without the
// trackpad too; only reading Z needs the Cirque driver.
#include "transactions.h"

// Cirque PACKETBYTE_5 (HostReg 0x17): Z in absolute mode.
#    define TP_Z_REG_PACKETBYTE_5 0x17
#    define TP_Z_RAW_HID_ID 0xF5
#    define TP_Z_CMD_GET 0x00
#    define TP_Z_CMD_SET 0x01
#    define TP_CONFIG_VERSION 3 // 2: hysteresis replaced by an absolute release Z; 3: LED zones in bits 20-31

#    ifndef TP_Z_SYNC_INTERVAL_MS
#        define TP_Z_SYNC_INTERVAL_MS 30
#    endif
#    ifndef TRACKPAD_PRESS_DEFAULT_ENABLED
#        define TRACKPAD_PRESS_DEFAULT_ENABLED 1
#    endif
#    ifndef TRACKPAD_PRESS_DEFAULT_THRESHOLD
#        define TRACKPAD_PRESS_DEFAULT_THRESHOLD 51
#    endif
#    ifndef TRACKPAD_PRESS_DEFAULT_RELEASE_Z
#        define TRACKPAD_PRESS_DEFAULT_RELEASE_Z 24
#    endif

#    ifdef POINTING_DEVICE_ENABLE
// Public in the Cirque I2C driver.
void RAP_ReadBytes(uint8_t address, uint8_t *data, uint8_t count);
#    endif

// EECONFIG_USER layout (shared with the config page settings):
//   bits 0-3   version
//   bit  4     press-to-click enabled
//   bits 5-10  press-to-click Z threshold (default 51)
//   bits 11-13 RGB idle timeout index (1 = 1 min)
//   bits 14-19 press-to-click release Z
//   bits 20-21 LED zones (0 effect on keys + underglow, 1 keys + solid
//              underglow, 2 keys only)
//   bits 22-27 solid underglow hue / 4
//   bits 28-31 solid underglow saturation / 17
typedef union {
    uint32_t raw;
    struct {
        uint32_t version : 4;
        uint32_t press_enabled : 1;
        uint32_t press_threshold : 6;
        uint32_t rgb_idle_timeout_idx : 3;
        uint32_t press_release_z : 6;
        uint32_t led_zone : 2;
        uint32_t ug_hue : 6;
        uint32_t ug_sat : 4;
    };
} tp_config_t;
_Static_assert(sizeof(tp_config_t) == sizeof(uint32_t), "tp_config_t must fit EECONFIG_USER");

static trackpad_z_t tp_z;
static tp_config_t  tp_config;

const trackpad_z_t *trackpad_z_get(void) {
    return &tp_z;
}

uint8_t trackpad_z_update(bool touch) {
    if (touch) {
        uint8_t packet5 = 0;
#    ifdef POINTING_DEVICE_ENABLE
        RAP_ReadBytes(TP_Z_REG_PACKETBYTE_5, &packet5, 1);
#    endif
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

/* ---------------------------------------------------------------------------
 * Press-to-click settings
 * ------------------------------------------------------------------------ */

#    ifndef LED_ZONE_DEFAULT_UG_HUE
#        define LED_ZONE_DEFAULT_UG_HUE 42 // x4 = 168, blue
#    endif

static void led_zone_set_defaults(void) {
    tp_config.led_zone = 0;
    tp_config.ug_hue   = LED_ZONE_DEFAULT_UG_HUE;
    tp_config.ug_sat   = 15;
}

static void tp_config_set_defaults(void) {
    tp_config.raw              = 0;
    tp_config.version          = TP_CONFIG_VERSION;
    tp_config.press_enabled    = TRACKPAD_PRESS_DEFAULT_ENABLED;
    tp_config.press_threshold  = TRACKPAD_PRESS_DEFAULT_THRESHOLD;
    tp_config.press_release_z  = TRACKPAD_PRESS_DEFAULT_RELEASE_Z;
    tp_config.rgb_idle_timeout_idx = 1;
    led_zone_set_defaults();
}

static bool tp_config_valid(void) {
    return tp_config.version == TP_CONFIG_VERSION && tp_config.press_threshold >= 1 && tp_config.press_release_z < tp_config.press_threshold;
}

void eeconfig_init_user(void) {
    tp_config_set_defaults();
    eeconfig_update_user(tp_config.raw);
}

bool trackpad_press_enabled(void) {
    return tp_config.press_enabled;
}
uint8_t trackpad_press_threshold(void) {
    return tp_config.press_threshold;
}
uint8_t trackpad_press_release_z(void) {
    return tp_config.press_release_z;
}

#    define RGB_IDLE_CHOICES 6

uint8_t user_config_rgb_idle_idx(void) {
    return tp_config.rgb_idle_timeout_idx < RGB_IDLE_CHOICES ? tp_config.rgb_idle_timeout_idx : 1;
}

void user_config_set_rgb_idle_idx(uint8_t idx) {
    if (idx < RGB_IDLE_CHOICES && idx != tp_config.rgb_idle_timeout_idx) {
        tp_config.rgb_idle_timeout_idx = idx;
        eeconfig_update_user(tp_config.raw);
    }
}

uint32_t user_config_rgb_idle_ms(void) {
    static const uint8_t minutes[RGB_IDLE_CHOICES] = {0, 1, 2, 5, 10, 30};
    return minutes[user_config_rgb_idle_idx()] * 60000UL;
}

void user_config_get_led_zone(uint8_t *zone, uint8_t *hue, uint8_t *sat) {
    *zone = tp_config.led_zone < 3 ? tp_config.led_zone : 0;
    *hue  = tp_config.ug_hue * 4;
    *sat  = tp_config.ug_sat * 17;
}

void user_config_set_led_zone(uint8_t zone, uint8_t hue, uint8_t sat) {
    tp_config_t old    = tp_config;
    tp_config.led_zone = zone < 3 ? zone : 0;
    tp_config.ug_hue   = (hue + 2) / 4 > 63 ? 63 : (hue + 2) / 4;
    tp_config.ug_sat   = (sat + 8) / 17;
    if (tp_config.raw != old.raw) {
        eeconfig_update_user(tp_config.raw);
    }
}

/* ---------------------------------------------------------------------------
 * Split sync for the right OLED (debug only)
 * ------------------------------------------------------------------------ */

#    ifdef TRACKPAD_Z_DEBUG
static void tp_z_slave_handler(uint8_t in_len, const void *in_data, uint8_t out_len, void *out_data) {
    if (in_len == sizeof(tp_z)) {
        memcpy(&tp_z, in_data, sizeof(tp_z));
    }
}
#    endif

void trackpad_z_post_init(void) {
    tp_config.raw = eeconfig_read_user();
    if (tp_config.version == 2) {
        // v2 -> v3: bits 20-31 were reserved (zero); keep everything else.
        tp_config.version = TP_CONFIG_VERSION;
        led_zone_set_defaults();
        if (tp_config_valid()) {
            eeconfig_update_user(tp_config.raw);
        }
    }
    if (!tp_config_valid()) {
        eeconfig_init_user();
    }
#    ifdef TRACKPAD_Z_DEBUG
    transaction_register_rpc(RPC_ID_USER_TP_Z, tp_z_slave_handler);
#    endif
}

void trackpad_z_sync_task(void) {
#    ifdef TRACKPAD_Z_DEBUG
    static trackpad_z_t last_sent;
    static uint16_t     last_time;
    if (!is_keyboard_master() || timer_elapsed(last_time) < TP_Z_SYNC_INTERVAL_MS || memcmp(&last_sent, &tp_z, sizeof(tp_z)) == 0) {
        return;
    }
    if (transaction_rpc_send(RPC_ID_USER_TP_Z, sizeof(tp_z), &tp_z)) {
        last_sent = tp_z;
    }
    last_time = timer_read();
#    endif
}

/* ---------------------------------------------------------------------------
 * Raw HID
 * ------------------------------------------------------------------------ */

void raw_hid_receive_kb(uint8_t *data, uint8_t length) {
    if (data[0] != TP_Z_RAW_HID_ID || length < 11) {
        data[0] = id_unhandled;
        return;
    }

    if (data[1] == TP_Z_CMD_SET && data[3] <= 63 && data[4] <= 63) {
        tp_config_t new_config = tp_config;
        new_config.press_enabled    = data[2] ? 1 : 0;
        new_config.press_threshold  = data[3];
        new_config.press_release_z  = data[4];
        tp_config_t old_config      = tp_config;
        tp_config                   = new_config;
        if (tp_config_valid()) {
            if (tp_config.raw != old_config.raw) {
                eeconfig_update_user(tp_config.raw);
            }
        } else {
            tp_config = old_config;
        }
    }

    data[1]  = tp_z.z;
    data[2]  = tp_z.peak;
    data[3]  = tp_z.last_peak;
    data[4]  = tp_z.touching;
    data[5]  = tp_z.touches & 0xFF;
    data[6]  = tp_z.touches >> 8;
    data[7]  = tp_config.press_enabled;
    data[8]  = tp_config.press_threshold;
    data[9]  = tp_config.press_release_z;
    data[10] = tp_z.pressed;
}
