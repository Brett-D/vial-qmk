// Copyright 2026 Brett-D
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Config mode, for sofle-config.html. Two ways in, both confirmed on the
// keyboard (the right OLED asks "CONF?"; right knob / Enter / Space = yes,
// left knob / Esc = no, 30 s timeout):
//
// 1. Web session (Chrome/Edge, WebHID): the page sends 0xF6 "request session".
//    Once confirmed, raw HID writes (VIA keymap/encoder/lighting sets, the
//    trackpad 0xF5 set, 0xF6 sets) are allowed until the page stops polling
//    for CONFIG_HID_SESSION_IDLE_MS. Without a session (or Vial unlock) those
//    writes are refused, so another website can't silently rewrite the
//    keyboard. The keyboard keeps working normally during a web session.
//
// 2. Keyboard-only mode (any browser): the CONFIG keycode (Raise + top-right
//    by default) or holding both knob buttons for 2 s asks "CONF?". After the
//    confirm, every key and knob event is swallowed and drives the editor
//    below; the keyboard reports the editor state by typing frames into the
//    page through the keystroke bridge (bridge.c). The page shows one letter
//    to press, which proves it is the window receiving the frames. Exit with
//    Raise + CONFIG, both knob buttons held 2 s, Esc held 2 s, or
//    CONFIG_MODE_IDLE_EXIT_MS without input.
//
// Keyboard-only controls once connected (tabs: Layout, LEDs, Trackpad):
//   left knob turn       layer (Layout) / field (LEDs, Trackpad)
//   left knob held+turn  select a knob action (Layout: turn left/right, press)
//   left knob press      next tab; hold ~1 s = resend everything to the page
//   right knob turn      pick keycode / change value (fast turns jump faster)
//   right knob held+turn jump to the next/previous keycode category
//   right knob press     apply (Layout) / save (LEDs, Trackpad); hold 0.6 s = next tab
//   any key (Layout)     select it; while holding it, tap another key to copy
//                        that key's default-layer keycode. With a knob action
//                        selected, hold the left knob and tap a key to copy.

#include "brettssofle.h"
#include "bridge.h"
#include "config_picker.h"
#include "led_cal.h"
#include "led_map.h"
#include "dynamic_keymap.h"
#include "keymap_introspection.h"
#include "raw_hid.h"
#include "transactions.h"
#include "via.h"

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
//                            knob nav (CMD_NAV only): turns (int8), presses, long presses, nav on]
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

// Letters for the connect check (no I/O/Q/Z/B/G/V to avoid look-alikes). One
// letter is enough: it is only shown by the page that received the HELLO, so
// pressing it proves that page has focus.
static const char challenge_letters[] = "ACDEFHJKLMNPRSTUWXY";
#define CHALLENGE_LEN 1

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

static struct {
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
} cfg;

// Keys pressed while config mode was on (or swallowed knob presses): their
// release is swallowed too, even after config mode ends.
static matrix_row_t held_swallowed[MATRIX_ROWS];

// Web (raw HID) session.
static struct {
    uint8_t  state;
    uint32_t time; // ask start / last poll / deny time
} hid;

// Right knob driving the focused config page (web session): its turns and
// presses are reported to the page instead of acting normally.
static struct {
    bool     on, down, long_sent;
    int8_t   turns;
    uint8_t  presses, longs;
    uint32_t time, down_time; // last page poll / knob press
} nav;

// State mirrored to the right half for its OLED.
typedef struct {
    uint8_t state, tab, layer, hid;
    uint8_t zone, ug_hue, ug_sat; // LED zones, for the right half's LEDs
    uint8_t cal;                  // LED calibration / test state (led_cal.h)
    uint8_t nav;                  // right knob drives the page
} cfg_oled_t;
static cfg_oled_t oled_state;

/* ---------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------ */

static bool is_knob(uint8_t row, uint8_t col) {
    return (row == KNOB_L_ROW && col == KNOB_L_COL) || (row == KNOB_R_ROW && col == KNOB_R_COL);
}

static uint16_t default_layer_keycode(uint8_t row, uint8_t col) {
    return keycode_at_keymap_location(get_highest_layer(default_layer_state), row, col);
}

// Keycode the held layer keys would give at (row, col), like the real layer
// stack: highest active layer first, transparent falls through (0 = none).
static uint16_t held_layers_keycode(uint8_t row, uint8_t col) {
    layer_state_t state = update_tri_layer_state(cfg.held_layers, _LOWER, _RAISE, _ADJUST);
    for (int8_t layer = LAYERS - 1; layer > 0; layer--) {
        if (state & ((layer_state_t)1 << layer)) {
            uint16_t kc = keycode_at_keymap_location(layer, row, col);
            if (kc != KC_TRANSPARENT) {
                return kc;
            }
        }
    }
    return 0;
}

static uint16_t basic_of(uint16_t kc) {
    // Mod-tap / layer-tap keys count as their tap keycode.
    if (kc >= QK_MOD_TAP && kc <= QK_LAYER_TAP_MAX) {
        return kc & 0xFF;
    }
    return kc;
}

static uint16_t picker_at(int16_t i) {
    return pgm_read_word(&cfg_picker[i]);
}

static int16_t picker_find(uint16_t kc) {
    for (int16_t i = 0; i < CFG_PICKER_COUNT; i++) {
        if (picker_at(i) == kc) {
            return i;
        }
    }
    return -1;
}

static void set_pending(uint16_t kc) {
    cfg.pending = kc;
    cfg.pick    = picker_find(kc);
    cfg.tx_ui   = true;
}

static uint16_t knob_slot_keycode(uint8_t slot) {
    uint8_t knob = slot / 3;
    if (slot % 3 == 2) {
        return knob == 0 ? dynamic_keymap_get_keycode(cfg.layer, KNOB_L_ROW, KNOB_L_COL) : dynamic_keymap_get_keycode(cfg.layer, KNOB_R_ROW, KNOB_R_COL);
    }
    return dynamic_keymap_get_encoder(cfg.layer, knob, slot % 3 == 1);
}

static void load_pending_for_tab(void) {
    if (cfg.tab == TAB_LAYOUT && cfg.knob_slot != NO_SLOT) {
        set_pending(knob_slot_keycode(cfg.knob_slot));
    } else if (cfg.tab == TAB_LAYOUT && cfg.sel_row != NO_KEY) {
        set_pending(dynamic_keymap_get_keycode(cfg.layer, cfg.sel_row, cfg.sel_col));
    }
    cfg.tx_ui = true;
}

/* ---------------------------------------------------------------------------
 * Underglow and trackpad settings
 * ------------------------------------------------------------------------ */

#ifdef RGB_MATRIX_ENABLE
// VialRGB effect ids (stable across builds) name the effects on the page.
typedef struct {
    uint16_t vialrgb_id;
    uint16_t qmk_id;
} vialrgb_supported_mode_t;
#    include "vialrgb_effects.inc"

// Enabled RGB Matrix modes are numbered 1..LED_MODE_COUNT.
#    define LED_MODE_COUNT (RGB_MATRIX_EFFECT_MAX - 1)

static uint8_t led_effect_of(uint8_t mode) {
    for (uint8_t i = 0; i < sizeof(supported_modes) / sizeof(supported_modes[0]); i++) {
        if (pgm_read_word(&supported_modes[i].qmk_id) == mode) {
            return pgm_read_word(&supported_modes[i].vialrgb_id);
        }
    }
    return 0;
}

/* LED zones: effect on keys + underglow, keys + solid underglow, keys only. */
static struct {
    uint8_t zone, hue, sat;
} zones;

static void zones_load(void) {
    user_config_get_led_zone(&zones.zone, &zones.hue, &zones.sat);
}

static void zones_apply_flags(uint8_t zone) {
    led_flags_t want = zone == ZONE_ALL ? LED_FLAG_ALL : LED_FLAG_KEYLIGHT;
    if (rgb_matrix_get_flags() != want) {
        rgb_matrix_set_flags_noeeprom(want);
    }
}

// Runs on both halves; the right half gets the zone settings from the master
// (oled_state, synced with the config-mode state).
static uint8_t zones_zone(void) {
    return is_keyboard_master() ? zones.zone : oled_state.zone;
}

static void zones_color(uint8_t *hue, uint8_t *sat) {
    *hue = is_keyboard_master() ? zones.hue : oled_state.ug_hue;
    *sat = is_keyboard_master() ? zones.sat : oled_state.ug_sat;
}

static bool map_commit_ok;

bool rgb_matrix_indicators_advanced_user(uint8_t led_min, uint8_t led_max) {
    if (led_cal_indicators(led_min, led_max, is_keyboard_master() ? led_cal_code() : oled_state.cal)) {
        return false;
    }
    uint8_t zone = zones_zone();
    if (zone == ZONE_ALL) {
        return false;
    }
    rgb_t rgb = {0, 0, 0};
    if (zone == ZONE_SOLID_UNDERGLOW) {
        uint8_t hue, sat;
        zones_color(&hue, &sat);
        rgb = hsv_to_rgb((hsv_t){hue, sat, rgb_matrix_get_val()});
    }
    for (uint8_t i = led_min; i < led_max; i++) {
        if (HAS_FLAGS(g_led_config.flags[i], LED_FLAG_UNDERGLOW)) {
            rgb_matrix_set_color(i, rgb.r, rgb.g, rgb.b);
        }
    }
    return false;
}

static void led_load(void) {
    cfg.led.enable = rgb_matrix_is_enabled();
    cfg.led.mode   = rgb_matrix_get_mode();
    cfg.led.hue    = rgb_matrix_get_hue();
    cfg.led.sat    = rgb_matrix_get_sat();
    cfg.led.val    = rgb_matrix_get_val();
    cfg.led.speed  = rgb_matrix_get_speed();
    cfg.led.idle   = user_config_rgb_idle_idx();
    zones_load();
    cfg.led.zone   = zones.zone;
    cfg.led.ug_hue = zones.hue;
    cfg.led.ug_sat = zones.sat;
    cfg.led.dirty  = false;
    cfg.tx_led     = true;
}

// Live preview: change the lights without writing EEPROM.
static void led_preview(void) {
    zones.zone = cfg.led.zone;
    zones.hue  = cfg.led.ug_hue;
    zones.sat  = cfg.led.ug_sat;
    zones_apply_flags(zones.zone);
    if (!cfg.led.enable) {
        if (rgb_matrix_is_enabled()) {
            rgb_matrix_disable_noeeprom();
        }
        return;
    }
    if (!rgb_matrix_is_enabled()) {
        rgb_matrix_enable_noeeprom();
    }
    if (rgb_matrix_get_mode() != cfg.led.mode) {
        rgb_matrix_mode_noeeprom(cfg.led.mode);
    }
    rgb_matrix_sethsv_noeeprom(cfg.led.hue, cfg.led.sat, cfg.led.val);
    rgb_matrix_set_speed_noeeprom(cfg.led.speed);
}

static void led_revert(void) {
    if (cfg.led.dirty) {
        rgb_matrix_reload_from_eeprom();
        zones_load();
        zones_apply_flags(zones.zone);
    }
    cfg.led.dirty = false;
}

static void led_save(void) {
    led_preview();
    eeconfig_force_flush_rgb_matrix();
    user_config_set_rgb_idle_idx(cfg.led.idle);
    user_config_set_led_zone(cfg.led.zone, cfg.led.ug_hue, cfg.led.ug_sat);
    led_load();
}

static uint8_t clamp_add(uint8_t value, int16_t delta, uint8_t min, uint8_t max) {
    int16_t v = (int16_t)value + delta;
    return v < min ? min : v > max ? max : (uint8_t)v;
}

static void led_change(int8_t dir) {
    switch (cfg.led_field) {
        case LF_ENABLE:
            cfg.led.enable = !cfg.led.enable;
            break;
        case LF_EFFECT:
            cfg.led.mode = cfg.led.mode + dir < 1 ? LED_MODE_COUNT : cfg.led.mode + dir > LED_MODE_COUNT ? 1 : cfg.led.mode + dir;
            break;
        case LF_HUE:
            cfg.led.hue += dir * 8;
            break;
        case LF_SAT:
            cfg.led.sat = clamp_add(cfg.led.sat, dir * 16, 0, 255);
            break;
        case LF_VAL:
            cfg.led.val = clamp_add(cfg.led.val, dir * 10, 0, RGB_MATRIX_MAXIMUM_BRIGHTNESS);
            break;
        case LF_SPEED:
            cfg.led.speed = clamp_add(cfg.led.speed, dir * 16, 0, 255);
            break;
        case LF_ZONE:
            cfg.led.zone = (cfg.led.zone + ZONE_COUNT + dir) % ZONE_COUNT;
            break;
        case LF_UG_HUE:
            cfg.led.ug_hue += dir * 8;
            break;
        case LF_UG_SAT:
            cfg.led.ug_sat = clamp_add(cfg.led.ug_sat, dir * 17, 0, 255);
            break;
        case LF_IDLE:
            cfg.led.idle = clamp_add(cfg.led.idle, dir, 0, LED_IDLE_CHOICES - 1);
            break;
    }
    cfg.led.dirty = true;
    cfg.tx_led    = true;
    led_preview();
}
#endif

void raw_hid_receive_kb(uint8_t *data, uint8_t length);

// Reuses the trackpad's raw HID 0xF5 command (trackpad_z.c): 0 = get, 1 = set.
static void tp_command(uint8_t command) {
    uint8_t buf[32] = {0xF5, command, cfg.tp.enable, cfg.tp.threshold, cfg.tp.release};
    raw_hid_receive_kb(buf, sizeof(buf));
    cfg.tp.enable    = buf[7];
    cfg.tp.threshold = buf[8];
    cfg.tp.release   = buf[9];
    cfg.tp.dirty     = false;
    cfg.tx_tp        = true;
}

static void tp_change(int8_t dir) {
    switch (cfg.tp_field) {
        case TF_ENABLE:
            cfg.tp.enable = !cfg.tp.enable;
            break;
        case TF_THRESHOLD:
            cfg.tp.threshold = clamp_add(cfg.tp.threshold, dir, 1, 63);
            if (cfg.tp.release >= cfg.tp.threshold) {
                cfg.tp.release = cfg.tp.threshold - 1;
            }
            break;
        case TF_RELEASE:
            cfg.tp.release = clamp_add(cfg.tp.release, dir, 0, cfg.tp.threshold - 1);
            break;
    }
    cfg.tp.dirty = true;
    cfg.tx_tp    = true;
}

/* ---------------------------------------------------------------------------
 * Enter / exit
 * ------------------------------------------------------------------------ */

static void start_handshake(void);

// Stop sending keys to the computer (keyboard-only mode and confirm prompts).
static void block_keys(void) {
    clear_keyboard();
    clear_oneshot_mods();
    layer_clear();
    trackpad_set_drag_scroll(false);
}

// Keyboard-only mode: ask "CONF?" on the OLED first.
static void bridge_ask(void) {
    block_keys();
    cfg.state    = ST_ASK;
    cfg.ask_time = timer_read32();
}

// Confirmed: start keyboard-only mode and send HELLO to the page.
static void bridge_start(void) {
    memset(&cfg, 0, sizeof(cfg));
    cfg.sel_row   = NO_KEY;
    cfg.sel_col   = NO_KEY;
    cfg.knob_slot = NO_SLOT;
    cfg.pick      = 0;
    cfg.pending = picker_at(0);
    start_handshake();
}

static void finish_exit(void) {
    bridge_abort();
    clear_keyboard();
    cfg.state = ST_OFF;
}

static void config_mode_exit(void) {
#ifdef RGB_MATRIX_ENABLE
    led_revert();
#endif
    if (cfg.state == ST_READY || cfg.state == ST_CHALLENGE) {
        // Tell the page, then stop once the bridge has typed it.
        bridge_abort();
        bool was_ready = cfg.state == ST_READY;
        memset(&cfg.tx_hello, 0, offsetof(typeof(cfg), last_tpz) - offsetof(typeof(cfg), tx_hello));
        cfg.tx_bye = was_ready;
        cfg.state  = ST_CLOSING;
    } else {
        finish_exit();
    }
}

bool config_mode_active(void) {
    return cfg.state != ST_OFF;
}

void config_mode_toggle(void) {
    if (hid.state == HS_ASK) {
        return; // a web session prompt is on screen
    }
    if (cfg.state == ST_OFF) {
        bridge_ask();
    } else if (cfg.state == ST_ASK) {
        cfg.state = ST_OFF;
    } else if (cfg.state != ST_CLOSING) {
        config_mode_exit();
    }
}

/* ---------------------------------------------------------------------------
 * Web session (raw HID)
 * ------------------------------------------------------------------------ */

#ifdef VIAL_ENABLE
extern int vial_unlocked;
#else
static const int vial_unlocked = 0;
#endif

static bool hid_writes_allowed(void) {
    return hid.state == HS_ACTIVE || vial_unlocked;
}

static void hid_request(void) {
    if (hid.state == HS_ACTIVE) {
        hid.time = timer_read32();
        return;
    }
    if (hid.state == HS_ASK || (hid.state == HS_DENIED && timer_elapsed32(hid.time) < CONFIG_HID_DENY_COOLDOWN_MS)) {
        return;
    }
    if (cfg.state == ST_ASK) {
        cfg.state = ST_OFF;
    } else if (cfg.state != ST_OFF && cfg.state != ST_CLOSING) {
        config_mode_exit(); // keyboard-only mode makes way for the web session
    }
    block_keys();
    hid.state = HS_ASK;
    hid.time  = timer_read32();
}

// Answer to a "CONF?" prompt (web session or keyboard-only mode).
static void ask_key(uint8_t row, uint8_t col) {
    uint16_t base = basic_of(default_layer_keycode(row, col));
    bool     yes  = (row == KNOB_R_ROW && col == KNOB_R_COL) || base == KC_ENTER || base == KC_SPACE;
    bool     no   = (row == KNOB_L_ROW && col == KNOB_L_COL) || base == KC_ESCAPE;
    if (!yes && !no) {
        return;
    }
    if (hid.state == HS_ASK) {
        hid.state = yes ? HS_ACTIVE : HS_DENIED;
        hid.time  = timer_read32();
    } else if (yes) {
        bridge_start();
    } else {
        cfg.state = ST_OFF;
    }
}

static bool is_write_command(const uint8_t *d) {
    switch (d[0]) {
        case id_set_keyboard_value:
        case id_dynamic_keymap_set_keycode:
        case id_dynamic_keymap_reset:
        case id_lighting_set_value:
        case id_lighting_save:
        case id_eeprom_reset:
        case id_dynamic_keymap_macro_set_buffer:
        case id_dynamic_keymap_macro_reset:
        case id_dynamic_keymap_set_buffer:
            return true;
        case id_vial_prefix:
            // Dynamic entry ops: even subcommands (2, 4, ...) are the "set"s.
            return d[1] == VIAL_SET_ENCODER || d[1] == VIAL_QMK_SETTINGS_SET || d[1] == VIAL_QMK_SETTINGS_RESET || (d[1] == VIAL_DYNAMIC_ENTRY_OP && d[2] != 0 && d[2] % 2 == 0);
        case 0xF5: // trackpad press-to-click settings (trackpad_z.c)
            return d[1] == 0x01;
        default:
            return false;
    }
}

static void cfg_raw_hid(uint8_t *data) {
    uint8_t arg = data[2];
    switch (data[1]) {
        case CMD_STATUS:
            if (hid.state == HS_ACTIVE) {
                hid.time = timer_read32();
                if (arg & 1) {
                    // The page is editing the underglow: count as activity so
                    // the idle timeout doesn't turn it off mid-edit.
                    set_activity_timestamps(timer_read32(), last_encoder_activity_time(), last_pointing_device_activity_time());
                }
            }
            break;
#ifdef RGB_MATRIX_ENABLE
        case CMD_SET_IDLE:
            if (hid_writes_allowed()) {
                user_config_set_rgb_idle_idx(arg);
            }
            break;
        case CMD_MODE_TABLE:
            data[3] = LED_MODE_COUNT;
            for (uint8_t i = 0; i < 28; i++) {
                uint8_t mode = arg + i;
                data[4 + i]  = mode >= 1 && mode <= LED_MODE_COUNT ? led_effect_of(mode) : 0xFF; // VialRGB id
            }
            return;
        case CMD_LED_REVERT:
            if (hid_writes_allowed()) {
                rgb_matrix_reload_from_eeprom();
                zones_load();
                zones_apply_flags(zones.zone);
            }
            break;
        case CMD_SET_ZONE: // preview: zone, underglow hue, underglow sat
            if (hid_writes_allowed()) {
                zones.zone = data[2] < ZONE_COUNT ? data[2] : ZONE_ALL;
                zones.hue  = data[3];
                zones.sat  = data[4];
                zones_apply_flags(zones.zone);
            }
            break;
        case CMD_SAVE_ZONE:
            if (hid_writes_allowed()) {
                user_config_set_led_zone(zones.zone, zones.hue, zones.sat);
            }
            break;
        case CMD_CAL_LIGHT:
            if (hid_writes_allowed()) {
                led_cal_light(arg);
            }
            break;
        case CMD_CAL_TEST:
            if (hid_writes_allowed()) {
                led_cal_test();
            }
            break;
        case CMD_CAL_END:
            led_cal_end();
            break;
        case CMD_MAP_READ:
            data[3] = LED_MAP_COUNT;
            led_map_read(arg, &data[4], 26);
            return;
        case CMD_MAP_WRITE:
            if (hid_writes_allowed() && data[3] <= 26) {
                led_map_write(arg, &data[4], data[3]);
            }
            break;
        case CMD_MAP_COMMIT:
            map_commit_ok = hid_writes_allowed() && led_map_commit();
            break;
        case CMD_MAP_RESET:
            if (hid_writes_allowed()) {
                led_map_reset();
            }
            break;
        case CMD_LED_ENABLE:
            if (hid_writes_allowed()) {
                if (arg) {
                    rgb_matrix_enable_noeeprom();
                } else {
                    rgb_matrix_disable_noeeprom();
                }
            }
            break;
#endif
        case CMD_EXIT_BRIDGE:
            if (hid_writes_allowed() && cfg.state != ST_OFF && cfg.state != ST_CLOSING) {
                config_mode_exit();
            }
            break;
        case CMD_NAV:
            if (hid.state == HS_ACTIVE) {
                hid.time = timer_read32();
            }
            nav.on   = arg && hid.state == HS_ACTIVE && cfg.state == ST_OFF;
            nav.time = timer_read32();
            break;
        case CMD_REQUEST:
            hid_request();
            break;
        case CMD_END:
            hid.state = HS_NONE;
            break;
    }
    uint32_t ask_left = hid.state == HS_ASK ? CONFIG_MODE_ASK_MS - timer_elapsed32(hid.time) : 0;
    data[2]           = CONFIG_MODE_PROTOCOL;
    data[3]           = hid.state;
    data[4]           = cfg.state;
#ifdef RGB_MATRIX_ENABLE
    data[5] = user_config_rgb_idle_idx();
    data[6] = RGB_MATRIX_MAXIMUM_BRIGHTNESS;
    data[7] = LED_MODE_COUNT;
    data[8]  = rgb_matrix_is_enabled();
    data[11] = led_effect_of(rgb_matrix_get_mode()); // VialRGB id, also while off
    data[12] = zones.zone;
    data[13] = zones.hue;
    data[14] = zones.sat;
    data[16] = (led_map_is_measured() ? 1 : 0) | (map_commit_ok ? 2 : 0);
    led_cal_status(data); // 15, 17-21
#endif
    data[9]  = vial_unlocked ? 1 : 0;
    data[10] = (ask_left + 999) / 1000;
    if (data[1] == CMD_NAV) {
        data[22]    = (uint8_t)nav.turns;
        data[23]    = nav.presses;
        data[24]    = nav.longs;
        data[25]    = nav.on;
        nav.turns   = 0;
        nav.presses = nav.longs = 0;
    }
}

bool via_command_kb(uint8_t *data, uint8_t length) {
    if (data[0] == CFG_RAW_HID_ID) {
        cfg_raw_hid(data);
    } else if (is_write_command(data)) {
        if (!hid_writes_allowed()) {
            data[0] = id_unhandled; // refused: no confirmed session
        } else {
            if (hid.state == HS_ACTIVE) {
                hid.time = timer_read32();
            }
            return false; // let VIA/Vial handle it
        }
    } else {
        return false;
    }
    raw_hid_send(data, length);
    return true;
}

/* ---------------------------------------------------------------------------
 * Connect handshake
 * ------------------------------------------------------------------------ */

static uint8_t challenge_letter(uint8_t pos) {
    return challenge_letters[(cfg.nonce >> (pos * 5)) % (sizeof(challenge_letters) - 1)];
}

static void start_handshake(void) {
    bridge_abort();
    cfg.nonce         = (uint16_t)((timer_read32() * 2654435761u) >> 16) ^ (uint16_t)last_input_activity_time();
    cfg.challenge_pos = 0;
    cfg.state         = ST_CHALLENGE;
    cfg.tx_hello      = true;
}

static void resync(void) {
    cfg.tx_ready = cfg.tx_ui = cfg.tx_led = cfg.tx_tp = true;
    for (uint8_t l = 0; l < LAYERS; l++) {
        cfg.tx_rows[l] = ALL_ROWS;
    }
    cfg.tx_enc = (1u << LAYERS) - 1;
}

static void become_ready(void) {
    cfg.state = ST_READY;
#ifdef RGB_MATRIX_ENABLE
    led_load();
#endif
    tp_command(0);
    resync();
}

static void challenge_key(uint16_t kc) {
    uint16_t expected = KC_A + (challenge_letter(cfg.challenge_pos) - 'A');
    if (kc == expected) {
        if (++cfg.challenge_pos == CHALLENGE_LEN) {
            become_ready();
        }
    } else if (kc == KC_SPACE || kc == KC_ENTER) {
        start_handshake();
    } else {
        cfg.challenge_pos = kc == KC_A + (challenge_letter(0) - 'A') ? 1 : 0;
    }
}

/* ---------------------------------------------------------------------------
 * Editor input
 * ------------------------------------------------------------------------ */

static void next_tab(void) {
#ifdef RGB_MATRIX_ENABLE
    led_revert();
#endif
    cfg.tab = (cfg.tab + 1) % TAB_COUNT;
    if (cfg.tab == TAB_LEDS) {
        // Loaded from the housekeeping task, after rgb_idle_task has restored
        // lights that were off for idle.
        cfg.led_load_pending = true;
    } else if (cfg.tab == TAB_TRACKPAD) {
        tp_command(0);
    }
    load_pending_for_tab();
}

static void navigate(int8_t dir) {
    switch (cfg.tab) {
        case TAB_LAYOUT:
            if (cfg.knob_down[0]) {
                // Left knob held + turn: step through the knob actions.
                cfg.knob_slot = cfg.knob_slot == NO_SLOT ? (dir > 0 ? 0 : KNOB_SLOTS - 1) : (cfg.knob_slot + KNOB_SLOTS + dir) % KNOB_SLOTS;
                cfg.sel_row = cfg.sel_col = NO_KEY;
                cfg.sel_held                = false;
            } else {
                cfg.layer = (cfg.layer + LAYERS + dir) % LAYERS;
            }
            load_pending_for_tab();
            break;
        case TAB_LEDS:
            cfg.led_field = (cfg.led_field + LF_COUNT + dir) % LF_COUNT;
            break;
        case TAB_TRACKPAD:
            cfg.tp_field = (cfg.tp_field + TF_COUNT + dir) % TF_COUNT;
            break;
    }
    cfg.tx_ui = true;
}

static void pick_step(int8_t dir) {
    int16_t step = timer_elapsed(cfg.last_value_turn) < CONFIG_MODE_FAST_TURN_MS ? 4 : 1;
    int16_t pick = cfg.pick < 0 ? 0 : cfg.pick + dir * step;
    pick         = (pick % CFG_PICKER_COUNT + CFG_PICKER_COUNT) % CFG_PICKER_COUNT;
    cfg.pick     = pick;
    cfg.pending  = picker_at(pick);
    cfg.tx_ui    = true;
}

static void pick_category(int8_t dir) {
    uint8_t cat = 0;
    for (uint8_t i = 0; i < CFG_PICKER_CATS; i++) {
        if (cfg.pick >= (int16_t)pgm_read_word(&cfg_picker_cat_start[i])) {
            cat = i;
        }
    }
    cat         = (cat + CFG_PICKER_CATS + dir) % CFG_PICKER_CATS;
    cfg.pick    = pgm_read_word(&cfg_picker_cat_start[cat]);
    cfg.pending = picker_at(cfg.pick);
    cfg.tx_ui   = true;
}

static void change_value(int8_t dir) {
    switch (cfg.tab) {
        case TAB_LAYOUT:
            pick_step(dir);
            break;
#ifdef RGB_MATRIX_ENABLE
        case TAB_LEDS:
            led_change(dir);
            break;
#endif
        case TAB_TRACKPAD:
            tp_change(dir);
            break;
    }
    cfg.last_value_turn = timer_read();
}

static void apply(void) {
    switch (cfg.tab) {
        case TAB_LAYOUT: {
            if (cfg.knob_slot == NO_SLOT) {
                if (cfg.sel_row != NO_KEY) {
                    dynamic_keymap_set_keycode(cfg.layer, cfg.sel_row, cfg.sel_col, cfg.pending);
                    cfg.tx_rows[cfg.layer] |= 1u << cfg.sel_row;
                }
                break;
            }
            uint8_t knob = cfg.knob_slot / 3;
            if (cfg.knob_slot % 3 == 2) {
                uint8_t row = knob == 0 ? KNOB_L_ROW : KNOB_R_ROW;
                dynamic_keymap_set_keycode(cfg.layer, row, knob == 0 ? KNOB_L_COL : KNOB_R_COL, cfg.pending);
                cfg.tx_rows[cfg.layer] |= 1u << row;
            } else {
                dynamic_keymap_set_encoder(cfg.layer, knob, cfg.knob_slot % 3 == 1, cfg.pending);
                cfg.tx_enc |= 1u << cfg.layer;
            }
            break;
        }
#ifdef RGB_MATRIX_ENABLE
        case TAB_LEDS:
            led_save();
            break;
#endif
        case TAB_TRACKPAD:
            tp_command(1);
            break;
    }
    cfg.tx_ui = true;
}

static void knob_press(uint8_t knob, bool pressed) {
    if (pressed) {
        cfg.knob_down[knob] = true;
        cfg.knob_used[knob] = false;
        cfg.knob_time[knob] = timer_read32();
        return;
    }
    if (!cfg.knob_down[knob]) {
        return; // pressed before config mode started
    }
    cfg.knob_down[knob] = false;
    if (cfg.knob_used[knob] || cfg.state != ST_READY) {
        return;
    }
    if (knob == 0) {
        if (timer_elapsed32(cfg.knob_time[0]) >= CONFIG_MODE_RESYNC_HOLD_MS) {
            resync();
        } else {
            next_tab();
        }
    } else if (timer_elapsed32(cfg.knob_time[1]) >= CONFIG_KNOB_LONG_MS) {
        next_tab(); // same gesture as on the web page
    } else {
        apply();
    }
}

static void key_event(uint8_t row, uint8_t col, bool pressed) {
    if (row == KNOB_L_ROW && col == KNOB_L_COL) {
        knob_press(0, pressed);
        return;
    }
    if (row == KNOB_R_ROW && col == KNOB_R_COL) {
        knob_press(1, pressed);
        return;
    }

    uint16_t raw  = default_layer_keycode(row, col);
    uint16_t base = basic_of(raw);
    if (base == KC_ESCAPE) {
        cfg.esc_down = pressed;
        cfg.esc_time = timer_read32();
    }

    // Layer keys are swallowed too, so track which layers they would turn on
    // (plus Adjust for Lower + Raise): the CONFIG key on them (Raise by
    // default, also reached through Adjust) still exits.
    uint8_t layer_of_key = 0xFF;
    if (raw >= QK_MOMENTARY && raw <= QK_MOMENTARY_MAX) {
        layer_of_key = raw & 0x1F;
    } else if (raw >= QK_LAYER_TAP_TOGGLE && raw <= QK_LAYER_TAP_TOGGLE_MAX) {
        layer_of_key = raw & 0x1F;
    } else if (raw >= QK_LAYER_TAP && raw <= QK_LAYER_TAP_MAX) {
        layer_of_key = (raw >> 8) & 0xF;
    }
    if (layer_of_key < 32) {
        if (pressed) {
            cfg.held_layers |= (layer_state_t)1 << layer_of_key;
        } else {
            cfg.held_layers &= ~((layer_state_t)1 << layer_of_key);
        }
    }
    if (pressed && (raw == CONFIG_MODE || held_layers_keycode(row, col) == CONFIG_MODE)) {
        config_mode_exit();
        return;
    }

    if (!pressed) {
        if (row == cfg.sel_row && col == cfg.sel_col && cfg.sel_held) {
            cfg.sel_held = false;
            cfg.tx_ui    = true;
        }
        return;
    }

    switch (cfg.state) {
        case ST_CHALLENGE:
            challenge_key(base);
            break;
        case ST_READY:
            if (cfg.tab == TAB_LAYOUT) {
                if (cfg.knob_slot != NO_SLOT && cfg.knob_down[0]) {
                    // Left knob held + key: copy its keycode for the knob action.
                    cfg.knob_used[0] = true;
                    set_pending(default_layer_keycode(row, col));
                } else if (cfg.sel_held && (row != cfg.sel_row || col != cfg.sel_col)) {
                    set_pending(default_layer_keycode(row, col));
                } else {
                    cfg.sel_row   = row;
                    cfg.sel_col   = col;
                    cfg.sel_held  = true;
                    cfg.knob_slot = NO_SLOT;
                    load_pending_for_tab();
                }
            }
            break;
    }
}

static void knob_turn(uint8_t knob, bool clockwise) {
    if (cfg.state != ST_READY) {
        return;
    }
    int8_t dir = clockwise ? 1 : -1;
    if (knob == 0) {
        if (cfg.knob_down[0]) {
            cfg.knob_used[0] = true;
        }
        navigate(dir);
    } else if (cfg.knob_down[1]) {
        cfg.knob_used[1] = true;
        if (cfg.tab == TAB_LAYOUT) {
            pick_category(dir);
        }
    } else {
        change_value(dir);
    }
}

bool pre_process_record_user(uint16_t keycode, keyrecord_t *record) {
#ifdef RGB_MATRIX_ENABLE
    if (led_cal_process(record)) {
        return false; // LED calibration / test: the page reads the key instead
    }
#endif
    keyevent_t *ev     = &record->event;
    bool        is_key = ev->type == KEY_EVENT && ev->key.row < MATRIX_ROWS;
    uint8_t     row = ev->key.row, col = ev->key.col;

    bool asking = cfg.state == ST_ASK || hid.state == HS_ASK;

    if (nav.on && cfg.state == ST_OFF && !asking) {
        if (IS_ENCODEREVENT(*ev) && ev->key.col == 1) {
            if (ev->pressed) {
                int8_t dir = ev->type == ENCODER_CW_EVENT ? 1 : -1;
                if ((dir > 0 && nav.turns < 100) || (dir < 0 && nav.turns > -100)) {
                    nav.turns += dir;
                }
            }
            return false;
        }
        if (is_key && row == KNOB_R_ROW && col == KNOB_R_COL) {
            if (ev->pressed) {
                held_swallowed[row] |= (matrix_row_t)1 << col;
                nav.down      = true;
                nav.long_sent = false;
                nav.down_time = timer_read32();
            } else {
                held_swallowed[row] &= ~((matrix_row_t)1 << col);
                if (nav.down && !nav.long_sent && nav.presses < 255) {
                    nav.presses++;
                }
                nav.down = false;
            }
            return false;
        }
    }

    if (cfg.state == ST_OFF && !asking) {
        if (is_key && !ev->pressed && (held_swallowed[row] & ((matrix_row_t)1 << col))) {
            held_swallowed[row] &= ~((matrix_row_t)1 << col);
            return false;
        }
        // Second knob press of the hold-both-knobs combo: don't send it.
        if (is_key && ev->pressed && is_knob(row, col) && matrix_is_on(KNOB_L_ROW, KNOB_L_COL) && matrix_is_on(KNOB_R_ROW, KNOB_R_COL)) {
            held_swallowed[row] |= (matrix_row_t)1 << col;
            return false;
        }
        return true;
    }

    if (is_key) {
        if (ev->pressed) {
            held_swallowed[row] |= (matrix_row_t)1 << col;
        } else {
            held_swallowed[row] &= ~((matrix_row_t)1 << col);
        }
    }
    if (asking) {
        if (is_key && ev->pressed) {
            ask_key(row, col);
        }
        return false;
    }
    if (is_key) {
        if (cfg.state != ST_CLOSING) {
            key_event(row, col, ev->pressed);
        }
    } else if (IS_ENCODEREVENT(*ev) && ev->pressed && cfg.state != ST_CLOSING) {
        knob_turn(ev->key.col, ev->type == ENCODER_CW_EVENT);
    }
    return false;
}

/* ---------------------------------------------------------------------------
 * Frames for the page (called by the bridge when it is ready for more)
 * ------------------------------------------------------------------------ */

static uint8_t put16(uint8_t *p, uint16_t v) {
    p[0] = v & 0xFF;
    p[1] = v >> 8;
    return 2;
}

uint8_t bridge_next_frame(uint8_t *buf) {
    uint8_t n = 1;
    if (cfg.tx_hello) {
        cfg.tx_hello = false;
        buf[0]       = F_HELLO;
        buf[n++]     = CONFIG_MODE_PROTOCOL;
        n += put16(&buf[n], cfg.nonce);
        return n;
    }
    if (cfg.tx_bye) {
        cfg.tx_bye = false;
        buf[0]     = F_BYE;
        return n;
    }
    if (cfg.state != ST_READY) {
        return 0;
    }
    if (cfg.tx_ready) {
        cfg.tx_ready = false;
        buf[0]       = F_READY;
        buf[n++]     = CONFIG_MODE_PROTOCOL;
        buf[n++]     = LAYERS;
        buf[n++]     = MATRIX_ROWS;
        buf[n++]     = MATRIX_COLS;
        buf[n++]     = NUM_ENCODERS;
        buf[n++]     = get_highest_layer(default_layer_state);
        n += put16(&buf[n], CFG_PICKER_HASH);
        n += put16(&buf[n], CFG_PICKER_COUNT);
        return n;
    }
    if (cfg.tx_ui) {
        cfg.tx_ui = false;
        buf[0]    = F_UI;
        buf[n++]  = cfg.tab;
        buf[n++]  = cfg.layer;
        buf[n++]  = cfg.sel_row;
        buf[n++]  = cfg.sel_col;
        n += put16(&buf[n], (uint16_t)cfg.pick);
        n += put16(&buf[n], cfg.pending);
        buf[n++] = cfg.knob_slot;
        buf[n++] = cfg.led_field;
        buf[n++] = cfg.tp_field;
        buf[n++] = (cfg.led.dirty ? 1 : 0) | (cfg.tp.dirty ? 2 : 0) | (cfg.sel_held ? 4 : 0);
        return n;
    }
#ifdef RGB_MATRIX_ENABLE
    if (cfg.tx_led) {
        cfg.tx_led = false;
        buf[0]     = F_LED;
        buf[n++]   = cfg.led.enable;
        buf[n++]   = cfg.led.mode;
        buf[n++]   = led_effect_of(cfg.led.mode);
        buf[n++]   = 0;
        buf[n++]   = cfg.led.hue;
        buf[n++]   = cfg.led.sat;
        buf[n++]   = cfg.led.val;
        buf[n++]   = cfg.led.speed;
        buf[n++]   = cfg.led.idle;
        buf[n++]   = RGB_MATRIX_MAXIMUM_BRIGHTNESS;
        buf[n++]   = cfg.led.dirty;
        buf[n++]   = cfg.led.zone;
        buf[n++]   = cfg.led.ug_hue;
        buf[n++]   = cfg.led.ug_sat;
        return n;
    }
#endif
    if (cfg.tx_tp) {
        cfg.tx_tp = false;
        buf[0]    = F_TP;
        buf[n++]  = cfg.tp.enable;
        buf[n++]  = cfg.tp.threshold;
        buf[n++]  = cfg.tp.release;
        buf[n++]  = cfg.tp.dirty;
        return n;
    }
    if (cfg.tx_tpz) {
        cfg.tx_tpz           = false;
        const trackpad_z_t *z = trackpad_z_get();
        cfg.tpz_sent         = *z;
        buf[0]               = F_TPZ;
        buf[n++]             = z->z;
        buf[n++]             = z->peak;
        buf[n++]             = z->last_peak;
        buf[n++]             = z->touching;
        buf[n++]             = z->pressed;
        return n;
    }
    // Keymap rows: the layer being edited first, then the rest.
    for (uint8_t i = 0; i < LAYERS; i++) {
        uint8_t layer = (cfg.layer + i) % LAYERS;
        if (cfg.tx_enc & (1u << layer)) {
            cfg.tx_enc &= ~(1u << layer);
            buf[0]   = F_ENC;
            buf[n++] = layer;
            for (uint8_t knob = 0; knob < 2; knob++) {
                n += put16(&buf[n], dynamic_keymap_get_encoder(layer, knob, false));
                n += put16(&buf[n], dynamic_keymap_get_encoder(layer, knob, true));
            }
            return n;
        }
        if (cfg.tx_rows[layer]) {
            uint8_t row = 0;
            while (!(cfg.tx_rows[layer] & (1u << row))) {
                row++;
            }
            cfg.tx_rows[layer] &= ~(1u << row);
            buf[0]   = F_ROW;
            buf[n++] = layer;
            buf[n++] = row;
            for (uint8_t col = 0; col < MATRIX_COLS; col++) {
                n += put16(&buf[n], dynamic_keymap_get_keycode(layer, row, col));
            }
            return n;
        }
    }
    return 0;
}

/* ---------------------------------------------------------------------------
 * Right-half OLED
 * ------------------------------------------------------------------------ */

static void oled_sync_handler(uint8_t in_len, const void *in_data, uint8_t out_len, void *out_data) {
    if (in_len == sizeof(oled_state)) {
        memcpy(&oled_state, in_data, sizeof(oled_state));
    }
}

static void oled_sync_task(void) {
    static cfg_oled_t last_sent;
    static uint16_t   last_time;
    cfg_oled_t        now    = {cfg.state, cfg.tab, cfg.layer, hid.state, zones.zone, zones.hue, zones.sat, led_cal_code(), nav.on};
    bool              active = now.state != ST_OFF || now.hid == HS_ASK || now.hid == HS_ACTIVE || now.cal;
    bool              due    = memcmp(&now, &last_sent, sizeof(now)) != 0 || (active && timer_elapsed(last_time) > CONFIG_MODE_OLED_SYNC_MS);
    if (due && transaction_rpc_send(RPC_ID_USER_CFG, sizeof(now), &now)) {
        last_sent = now;
        last_time = timer_read();
    }
}

#ifdef OLED_ENABLE
bool config_mode_oled_task(void) {
    static uint8_t last_screen;
    bool           asking = oled_state.state == ST_ASK || oled_state.hid == HS_ASK;
    bool           active = oled_state.state != ST_OFF || asking || oled_state.hid == HS_ACTIVE;
    uint8_t        screen = active ? 1 + (asking ? 0x80 : 0) + (oled_state.nav ? 0x40 : 0) + oled_state.state * 8 + oled_state.hid : 0;
    if (screen != last_screen) {
        oled_clear();
        last_screen = screen;
    }
    if (!active) {
        return false;
    }
    // Portrait OLED: 5 characters per line, so each string below is one line.
    static const char tab_names[TAB_COUNT][6] PROGMEM = {"Keys ", "LEDs ", "Pad  "};
    if (asking) {
        oled_write_P(PSTR("CONF?"), true);
        oled_write_P(oled_state.hid == HS_ASK ? PSTR("Web  ") : PSTR("     "), false);
        oled_write_P(PSTR("Knob=yes  Esc= no   "), false);
        return true;
    }
    oled_write_P(PSTR("CONFG"), true);
    oled_write_P(PSTR("     "), false);
    switch (oled_state.state) {
        case ST_OFF: // web session only
            oled_write_P(PSTR("Web  page "), false);
            if (oled_state.nav) {
                oled_write_P(PSTR("     Knob nav  "), false); // right knob drives the page
            }
            break;
        case ST_CHALLENGE:
            oled_write_P(PSTR("Press"), false);
            oled_write_P(PSTR("lettr"), false);
            break;
        case ST_READY:
            oled_write_P(tab_names[oled_state.tab % TAB_COUNT], false);
            oled_write_P(PSTR("Lyr "), false);
            oled_write_char('0' + oled_state.layer, false);
            break;
        default:
            oled_write_P(PSTR("Bye       "), false);
            break;
    }
    return true;
}
#endif

/* ---------------------------------------------------------------------------
 * Init / task
 * ------------------------------------------------------------------------ */

void config_mode_init(void) {
    transaction_register_rpc(RPC_ID_USER_CFG, oled_sync_handler);
#ifdef RGB_MATRIX_ENABLE
    zones_load();
    led_map_init();
#endif
}

static void knob_combo_task(void) {
    static uint32_t start;
    static bool     armed = true;
    if (!(matrix_is_on(KNOB_L_ROW, KNOB_L_COL) && matrix_is_on(KNOB_R_ROW, KNOB_R_COL))) {
        start = 0;
        armed = true;
        return;
    }
    if (!armed) {
        return;
    }
    if (start == 0) {
        start = timer_read32() | 1;
    } else if (timer_elapsed32(start) >= CONFIG_MODE_COMBO_MS) {
        armed            = false;
        cfg.knob_used[0] = cfg.knob_used[1] = true;
        config_mode_toggle();
    }
}

void config_mode_task(void) {
#ifdef RGB_MATRIX_ENABLE
    zones_apply_flags(zones_zone());
    led_map_task();
    if (hid.state != HS_ACTIVE) {
        led_cal_end(); // the page went away mid-calibration
    }
#endif
    if (!is_keyboard_master()) {
        return;
    }
    knob_combo_task();
    oled_sync_task();

    if (nav.on && (hid.state != HS_ACTIVE || cfg.state != ST_OFF || timer_elapsed32(nav.time) >= CONFIG_NAV_IDLE_MS)) {
        nav.on = nav.down = false;
        nav.turns         = 0;
        nav.presses = nav.longs = 0;
    }
    if (nav.on && nav.down && !nav.long_sent && timer_elapsed32(nav.down_time) >= CONFIG_KNOB_LONG_MS) {
        nav.long_sent = true;
        if (nav.longs < 255) {
            nav.longs++;
        }
    }
    if (hid.state == HS_ASK && timer_elapsed32(hid.time) >= CONFIG_MODE_ASK_MS) {
        hid.state = HS_TIMEOUT;
    } else if (hid.state == HS_ACTIVE && timer_elapsed32(hid.time) >= CONFIG_HID_SESSION_IDLE_MS) {
        hid.state = HS_NONE; // the page stopped polling
    }
    if (cfg.state == ST_ASK && timer_elapsed32(cfg.ask_time) >= CONFIG_MODE_ASK_MS) {
        cfg.state = ST_OFF;
    }
    if (cfg.state == ST_OFF || cfg.state == ST_ASK) {
        return;
    }

    if (cfg.state == ST_CLOSING) {
        if (!cfg.tx_bye && !bridge_busy()) {
            finish_exit();
        }
    } else if (last_input_activity_elapsed() > CONFIG_MODE_IDLE_EXIT_MS || (cfg.esc_down && timer_elapsed32(cfg.esc_time) >= CONFIG_MODE_ESC_HOLD_MS)) {
        config_mode_exit();
    }

#ifdef RGB_MATRIX_ENABLE
    if (cfg.led_load_pending) {
        cfg.led_load_pending = false;
        led_load();
    }
#endif
    if (cfg.state == ST_READY && cfg.tab == TAB_TRACKPAD && timer_elapsed(cfg.last_tpz) >= CONFIG_MODE_TPZ_INTERVAL_MS) {
        const trackpad_z_t *z = trackpad_z_get();
        if (z->z != cfg.tpz_sent.z || z->last_peak != cfg.tpz_sent.last_peak || z->touching != cfg.tpz_sent.touching || z->pressed != cfg.tpz_sent.pressed) {
            cfg.tx_tpz   = true;
            cfg.last_tpz = timer_read();
        }
    }
    bridge_task();
}
