// Copyright 2026 Brett-D
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Config mode internals, shared by config_mode.c (keyboard-only mode: keys,
// knobs, the editor, the frames for the page), config_hid.c (the web session
// and the 0xF6 raw HID commands) and config_oled.c (the right half's OLED:
// the sync and the config screens). Not for other modules: config_mode.h is
// the public interface.

#pragma once

#include <stddef.h>
#include "brettssofle.h"
#include "bridge.h"
#include "deliver.h"
#include "diag.h"
#ifdef PET_ENABLE
#    include "pet.h"
#endif
#ifdef NETWORK_ENABLE
#    include "net.h"
#else
#    define NET_READY 3 // net.h (the OLED code reads the synced network state)
#endif
#include "config_picker.h"
#include "led_cal.h"
#include "led_fx.h"
#include "led_map.h"
#include "dynamic_keymap.h"
#include "keymap_introspection.h"
#include "raw_hid.h"
#include "transactions.h"
#include "via.h"
#include "config_mode.h"
#include "brett_util.h"
#include "user_config.h"
#include "trackpad.h"
#include "oled_pages.h"

#ifndef CONFIG_NET_BYE_MS
#    define CONFIG_NET_BYE_MS 6000 // "NET off" + why, after network mode ends
#endif
#ifndef CONFIG_MODE_IDLE_EXIT_MS
#    define CONFIG_MODE_IDLE_EXIT_MS 300000
#endif
#ifndef CONFIG_HID_SESSION_IDLE_MS
#    define CONFIG_HID_SESSION_IDLE_MS 90000
#endif
#define CONFIG_MODE_ASK_MS 30000
#define CONFIG_HID_DENY_COOLDOWN_MS 5000
#define CONFIG_MODE_COMBO_MS 2000
#define CONFIG_MODE_ESC_HOLD_MS 2000
#define CONFIG_MODE_RESYNC_HOLD_MS 800
#define CONFIG_MODE_FAST_TURN_MS 80
#define CONFIG_MODE_TPZ_INTERVAL_MS 100
#define CONFIG_MODE_OLED_SYNC_MS 1000
#define CONFIG_KNOB_LONG_MS 600     // right knob long press: next tab (page and keyboard-only mode)
#define CONFIG_NAV_IDLE_MS 1500     // page stopped polling: the right knob works normally again
#define CONFIG_MODE_PROTOCOL 2

// Raw HID command for the page (keep in sync with page.html):
//   [0xF6, sub, args...] -> [0xF6, sub, proto, hid state, keyboard-only state,
//                            led idle idx, led max val, led mode count,
//                            led enabled, vial unlocked, ask seconds left,
//                            effect (VialRGB id), zone, underglow hue, underglow sat,
//                            calibration state (led_cal.h), map flags (1 measured, 2 last commit ok),
//                            capture seq, row, col, flags, right half map (0 ?, 1 same, 2 differs),
//                            knob nav (CMD_NAV only): turns (int8), presses, long presses, nav on,
//                            layers: base effect, key-press effect (VialRGB ids), key-press hue, sat,
//                            wiggler (bit 0 on, bits 1-2 interval)]
#define CFG_RAW_HID_ID 0xF6
enum cfg_hid_cmd {
    CMD_STATUS      = 0x00, // arg: 1 = keep the underglow awake (LED tab open); refreshes the session
    CMD_SET_IDLE    = 0x01, // arg: idle timeout index (saved)
    CMD_MODE_TABLE  = 0x02, // arg: first mode -> [.., .., first, mode count, effect id x 28]
    CMD_EXIT_BRIDGE = 0x03, // end keyboard-only mode
    CMD_LED_REVERT  = 0x04, // drop unsaved lighting changes
    CMD_LED_ENABLE  = 0x05, // arg: 0/1 = lights off/on (preview; saved by VIA lighting save)
    CMD_SET_ZONE    = 0x06, // args: zone, underglow hue, underglow sat (preview)
    CMD_SAVE_ZONE   = 0x07, // save the previewed zone settings
    CMD_CAL_LIGHT   = 0x08, // arg: physical LED 0-73 to light alone (0xFF = all off); starts calibration + key capture
    CMD_CAL_END     = 0x09, // end calibration, effects resume
    CMD_MAP_READ    = 0x0A, // arg: offset -> [.., .., offset, LED count, 26 map entries]
    CMD_MAP_WRITE   = 0x0B, // args: offset, len (<= 26), entries (staged)
    CMD_MAP_COMMIT  = 0x0C, // validate + save + apply the staged map (status byte 16 bit 1 = ok)
    CMD_MAP_RESET   = 0x0D, // back to the built-in map
    CMD_CAL_TEST    = 0x0E, // light the mapped LED of each pressed key (ends with CMD_CAL_END)
    CMD_NAV         = 0x12, // arg 1: page focused, the right knob drives it (events returned + cleared); 0: release it
    CMD_LAYERS      = 0x14, // whole-keyboard effect, key-press effect (VialRGB ids, 0 = none, 0xFF = keep), key-press hue, sat, save
    CMD_LEDFX       = 0x1C, // v20 lighting (led_fx.c): 0 get, 1 set (preview), 2 save, 3 revert, 4 test ripple at row, col
    CMD_LOOK        = 0x15, // speed, hue, sat, val (preview; saved by VIA lighting save)
    CMD_WIGGLE      = 0x16, // mouse wiggler: on, interval index (not saved)
    CMD_DIAG        = 0x17, // crash record, stack use, why network mode ended (diag.c); arg 1 = clear
    CMD_PET_GET     = 0x18, // the OLED pet (pet.c): stats, settings, name
    CMD_PET_ACT     = 0x19, // care: 1 feed, 2 play, 3 sleep/wake, 4 clean, 5 new pet
    CMD_PET_SET     = 0x1A, // flags (1 on, 2 can die), decay speed, name
    CMD_PET_RESTORE = 0x1B, // species, stage (restoring a backup)
    CMD_REQUEST     = 0x10, // ask for a web session (confirmed on the keyboard)
    CMD_END         = 0x11, // end the web session
};
enum hid_state { HS_NONE, HS_ASK, HS_ACTIVE, HS_DENIED, HS_TIMEOUT };

// Vial subcommands (quantum/vial.h) that change the keyboard.
#define VIAL_SET_ENCODER 0x04
#define VIAL_QMK_SETTINGS_SET 0x0B
#define VIAL_QMK_SETTINGS_RESET 0x0C
#define VIAL_DYNAMIC_ENTRY_OP 0x0D

// Knob push switches in the matrix (keyhive keyboard.json).
#define KNOB_L_ROW 4
#define KNOB_L_COL 0
#define KNOB_R_ROW 9
#define KNOB_R_COL 5

#define NO_KEY 0xFF
#define LED_IDLE_CHOICES 6
#define LAYERS DYNAMIC_KEYMAP_LAYER_COUNT
#define ALL_ROWS ((1u << MATRIX_ROWS) - 1)

// Frame types; keep in sync with page.html.
enum frame_type {
    F_HELLO = 0x01, // proto, nonce lo, nonce hi
    F_READY = 0x02, // proto, layers, rows, cols, encoders, default layer, picker hash lo/hi, picker count lo/hi
    F_BYE   = 0x03, // -
    F_UI    = 0x04, // tab, layer, sel row, sel col, pick lo/hi, pending lo/hi, knob slot, led field, tp field, flags
    F_ROW   = 0x10, // layer, row, keycode x MATRIX_COLS (LE)
    F_ENC   = 0x11, // layer, left ccw, left cw, right ccw, right cw (LE)
    F_LED   = 0x20, // enabled, mode, effect (VialRGB id), 0, hue, sat, val, speed, idle, max val, unsaved, zone, underglow hue, underglow sat
    F_TP    = 0x21, // enabled, threshold, release z, unsaved
    F_TPZ   = 0x22, // z, peak, last peak, touching, pressed
};

enum cfg_state { ST_OFF, ST_ASK, ST_CHALLENGE, ST_READY, ST_CLOSING };
enum cfg_tab { TAB_LAYOUT, TAB_LEDS, TAB_TRACKPAD, TAB_COUNT };
enum led_field { LF_ENABLE, LF_EFFECT, LF_HUE, LF_SAT, LF_VAL, LF_SPEED, LF_ZONE, LF_UG_HUE, LF_UG_SAT, LF_IDLE, LF_COUNT };
enum led_zone { ZONE_ALL, ZONE_SOLID_UNDERGLOW, ZONE_KEYS_ONLY, ZONE_COUNT };
enum tp_field { TF_ENABLE, TF_THRESHOLD, TF_RELEASE, TF_COUNT };
// Knob actions (Layout tab): left ccw/cw/press, right ccw/cw/press. The
// selection is either a key (sel_row/sel_col) or a knob action (knob_slot).
#define KNOB_SLOTS 6
#define NO_SLOT 0xFF


typedef struct {
    bool    enable;
    uint8_t mode, hue, sat, val, speed, idle;
    uint8_t zone, ug_hue, ug_sat;
    bool    dirty;
} led_edit_t;

typedef struct {
    bool    enable;
    uint8_t threshold, release;
    bool    dirty;
} tp_edit_t;

typedef struct {
    uint8_t  state;
    uint8_t  tab;
    uint8_t  layer;
    uint8_t  sel_row, sel_col;
    bool     sel_held;
    uint16_t pending; // keycode the right knob press applies
    int16_t  pick;    // picker index of pending, -1 if not in the picker
    uint8_t  knob_slot;
    uint8_t  led_field, tp_field;
    uint16_t nonce;
    uint8_t  challenge_pos;
    uint32_t ask_time;

    bool     knob_down[2], knob_used[2];
    uint32_t knob_time[2];
    uint16_t last_value_turn;
    bool     esc_down;
    uint32_t esc_time;
    layer_state_t held_layers; // layers of the held layer keys (they're swallowed too)

    led_edit_t led;
    bool       led_load_pending;
    tp_edit_t  tp;

    bool         tx_hello, tx_ready, tx_bye, tx_ui, tx_led, tx_tp, tx_tpz;
    uint16_t     tx_rows[LAYERS];
    uint8_t      tx_enc;
    uint16_t     last_tpz;
    trackpad_z_t tpz_sent;
} cfg_t;
extern cfg_t cfg;

// Keys pressed while config mode was on (or swallowed knob presses): their
// release is swallowed too, even after config mode ends.
extern matrix_row_t held_swallowed[MATRIX_ROWS];

// Web (raw HID) session.
typedef struct {
    uint8_t  state;
    uint32_t time; // ask start / last poll / deny time
} hid_session_t;
extern hid_session_t hid;

// Right knob driving the focused config page (web session): its turns and
// presses are reported to the page instead of acting normally.
typedef struct {
    bool     on, down, long_sent;
    int8_t   turns;
    uint8_t  presses, longs;
    uint32_t time, down_time; // last page poll / knob press
} nav_t;
extern nav_t nav;

// State mirrored to the right half for its OLED.
typedef struct {
    uint8_t state, tab, layer, hid;
    uint8_t zone, ug_hue, ug_sat; // LED zones, for the right half's LEDs
    uint8_t cal;                  // LED calibration / test state (led_cal.h)
    uint8_t nav;                  // right knob drives the page
    uint8_t lay_base, lay_react, lay_hue, lay_sat; // layered lighting (led_fx.c)
    uint8_t dl, dl_pct, dl_pass;                   // typed page delivery (deliver.c)
    uint8_t net;                                   // network mode: state | 0x80 = no address yet (fallbacks)
    uint8_t net_bye;                               // network mode just ended: why (NET_EXIT_*)
    uint8_t net_secs;                              // seconds since network mode started (max 255)
    uint8_t fw;                                    // the left half's BRETT_FW_VERSION (v19+)
    // New fields go at the end: the right half takes any length >= 18 as a
    // prefix (v19+), and the left half also sends the older layouts.
} cfg_oled_t;
#define CFG_SYNC_V16_LEN 18 // v16-v17: up to net_bye
#define CFG_SYNC_V18_LEN 19 // v18: + net_secs
#define CFG_SYNC_V13_LEN 17 // v13-v15: no dl_pass
_Static_assert(sizeof(cfg_oled_t) == 20 && offsetof(cfg_oled_t, net_bye) == CFG_SYNC_V16_LEN - 1 && offsetof(cfg_oled_t, net_secs) == CFG_SYNC_V18_LEN - 1,
               "cfg_oled_t: append new fields at the end only (older right halves read a prefix)");
extern cfg_oled_t oled_state;

typedef struct {
    uint8_t zone, hue, sat;
} zones_t;
extern zones_t zones;

#ifdef RGB_MATRIX_ENABLE
// Enabled RGB Matrix modes are numbered 1..LED_MODE_COUNT.
#    define LED_MODE_COUNT (RGB_MATRIX_EFFECT_MAX - 1)
#endif

// Shared between the three files (defined where noted).
uint16_t default_layer_keycode(uint8_t row, uint8_t col);  // mode.c
uint16_t basic_of(uint16_t kc);  // mode.c
uint8_t led_effect_of(uint8_t mode);  // mode.c
uint8_t led_mode_of(uint8_t id);  // mode.c
void zones_load(void);  // mode.c
void zones_apply_flags(uint8_t zone);  // mode.c
void block_keys(void);  // mode.c
void bridge_start(void);  // mode.c
void config_mode_exit(void);  // mode.c
extern bool map_commit_ok;
void ask_key(uint8_t row, uint8_t col);  // hid.c
void oled_sync_task(void);  // oled.c
void oled_sync_handler(uint8_t in_len, const void *in_data, uint8_t out_len, void *out_data);  // oled.c
