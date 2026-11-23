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
// Keyboard-only controls once connected:
//   left knob turn       layer (Keys) / slot (Knobs) / field (LEDs, Trackpad)
//   left knob press      next tab; hold ~1 s = resend everything to the page
//   right knob turn      pick keycode / change value (fast turns jump faster)
//   right knob held+turn jump to the next/previous keycode category
//   right knob press     apply (Keys, Knobs) / save (LEDs, Trackpad)
//   any key (Keys tab)   select it; while holding it, tap another key to copy
//                        that key's default-layer keycode
//   any key (Knobs tab)  copy that key's default-layer keycode

#include "brettssofle.h"
#include "bridge.h"
#include "config_picker.h"
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
#define CONFIG_MODE_PROTOCOL 2

// Raw HID command for the page (keep in sync with page.html):
//   [0xF6, sub, args...] -> [0xF6, sub, proto, hid state, keyboard-only state,
//                            led idle idx, led max val, led mode count,
//                            led enabled, vial unlocked, ask seconds left]
#define CFG_RAW_HID_ID 0xF6
enum cfg_hid_cmd {
    CMD_STATUS      = 0x00, // arg: 1 = keep the underglow awake (LED tab open); refreshes the session
    CMD_SET_IDLE    = 0x01, // arg: idle timeout index (saved)
    CMD_MODE_TABLE  = 0x02, // arg: first mode -> [.., .., first, mode count, effect id x 28]
    CMD_EXIT_BRIDGE = 0x03, // end keyboard-only mode
    CMD_LED_REVERT  = 0x04, // drop unsaved lighting changes
    CMD_LED_ENABLE  = 0x05, // arg: 0/1 = lights off/on (preview; saved by VIA lighting save)
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
    F_LED   = 0x20, // enabled, mode, effect, variant, hue, sat, val, speed, idle, max val, unsaved
    F_TP    = 0x21, // enabled, threshold, release z, unsaved
    F_TPZ   = 0x22, // z, peak, last peak, touching, pressed
};

enum cfg_state { ST_OFF, ST_ASK, ST_CHALLENGE, ST_READY, ST_CLOSING };
enum cfg_tab { TAB_KEYS, TAB_KNOBS, TAB_LEDS, TAB_TRACKPAD, TAB_COUNT };
enum led_field { LF_ENABLE, LF_EFFECT, LF_HUE, LF_SAT, LF_VAL, LF_SPEED, LF_IDLE, LF_COUNT };
enum tp_field { TF_ENABLE, TF_THRESHOLD, TF_RELEASE, TF_COUNT };
// Knobs tab slots: left ccw/cw/press, right ccw/cw/press.
#define KNOB_SLOTS 6

// Letters for the connect check (no I/O/Q/Z/B/G/V to avoid look-alikes). One
// letter is enough: it is only shown by the page that received the HELLO, so
// pressing it proves that page has focus.
static const char challenge_letters[] = "ACDEFHJKLMNPRSTUWXY";
#define CHALLENGE_LEN 1

typedef struct {
    bool    enable;
    uint8_t mode, hue, sat, val, speed, idle;
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
    uint8_t  held_layer; // layer of the held layer key (0 = none)
    uint8_t  held_layer_row, held_layer_col;

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

// State mirrored to the right half for its OLED.
typedef struct {
    uint8_t state, tab, layer, hid;
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
    if (cfg.tab == TAB_KEYS && cfg.sel_row != NO_KEY) {
        set_pending(dynamic_keymap_get_keycode(cfg.layer, cfg.sel_row, cfg.sel_col));
    } else if (cfg.tab == TAB_KNOBS) {
        set_pending(knob_slot_keycode(cfg.knob_slot));
    }
    cfg.tx_ui = true;
}

/* ---------------------------------------------------------------------------
 * Underglow and trackpad settings
 * ------------------------------------------------------------------------ */

#ifdef RGBLIGHT_ENABLE
enum led_effect { EFF_STATIC_LIGHT, EFF_BREATHING, EFF_RAINBOW_MOOD, EFF_RAINBOW_SWIRL, EFF_SNAKE, EFF_KNIGHT, EFF_CHRISTMAS, EFF_STATIC_GRADIENT, EFF_RGB_TEST, EFF_ALTERNATING, EFF_TWINKLE };

// Effect of each rgblight mode number (index 0 unused).
static const uint8_t PROGMEM led_mode_effect[] = {
    0,
#    define _RGBM_SINGLE_STATIC(sym) EFF_##sym,
#    define _RGBM_SINGLE_DYNAMIC(sym) EFF_##sym,
#    define _RGBM_MULTI_STATIC(sym) EFF_##sym,
#    define _RGBM_MULTI_DYNAMIC(sym) EFF_##sym,
#    define _RGBM_TMP_STATIC(sym, msym) EFF_##msym,
#    define _RGBM_TMP_DYNAMIC(sym, msym) EFF_##msym,
#    include "rgblight_modes.h"
};

static uint8_t led_effect_of(uint8_t mode) {
    return mode < sizeof(led_mode_effect) ? pgm_read_byte(&led_mode_effect[mode]) : 0;
}

static uint8_t led_variant_of(uint8_t mode) {
    uint8_t effect  = led_effect_of(mode);
    uint8_t variant = 0;
    while (mode > 1 && led_effect_of(mode - 1) == effect) {
        mode--;
        variant++;
    }
    return variant;
}

static void led_load(void) {
    cfg.led.enable = rgblight_is_enabled();
    cfg.led.mode   = rgblight_get_mode();
    cfg.led.hue    = rgblight_get_hue();
    cfg.led.sat    = rgblight_get_sat();
    cfg.led.val    = rgblight_get_val();
    cfg.led.speed  = rgblight_get_speed();
    cfg.led.idle   = user_config_rgb_idle_idx();
    cfg.led.dirty  = false;
    cfg.tx_led     = true;
}

// Live preview: change the lights without writing EEPROM.
static void led_preview(void) {
    if (!cfg.led.enable) {
        if (rgblight_is_enabled()) {
            rgblight_disable_noeeprom();
        }
        return;
    }
    if (!rgblight_is_enabled()) {
        rgblight_enable_noeeprom();
    }
    if (rgblight_get_mode() != cfg.led.mode) {
        rgblight_mode_noeeprom(cfg.led.mode);
    }
    rgblight_sethsv_noeeprom(cfg.led.hue, cfg.led.sat, cfg.led.val);
    rgblight_set_speed_noeeprom(cfg.led.speed);
}

static void led_revert(void) {
    if (cfg.led.dirty) {
        rgblight_reload_from_eeprom();
    }
    cfg.led.dirty = false;
}

static void led_save(void) {
    led_preview();
    eeconfig_update_rgblight_current();
    user_config_set_rgb_idle_idx(cfg.led.idle);
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
            cfg.led.mode = cfg.led.mode + dir < 1 ? RGBLIGHT_MODES : cfg.led.mode + dir > RGBLIGHT_MODES ? 1 : cfg.led.mode + dir;
            break;
        case LF_HUE:
            cfg.led.hue += dir * 8;
            break;
        case LF_SAT:
            cfg.led.sat = clamp_add(cfg.led.sat, dir * 16, 0, 255);
            break;
        case LF_VAL:
            cfg.led.val = clamp_add(cfg.led.val, dir * 10, 0, RGBLIGHT_LIMIT_VAL);
            break;
        case LF_SPEED:
            cfg.led.speed = clamp_add(cfg.led.speed, dir, 0, 3);
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
    cfg.sel_row = NO_KEY;
    cfg.sel_col = NO_KEY;
    cfg.pick    = 0;
    cfg.pending = picker_at(0);
    start_handshake();
}

static void finish_exit(void) {
    bridge_abort();
    clear_keyboard();
    cfg.state = ST_OFF;
}

static void config_mode_exit(void) {
#ifdef RGBLIGHT_ENABLE
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
#ifdef RGBLIGHT_ENABLE
        case CMD_SET_IDLE:
            if (hid_writes_allowed()) {
                user_config_set_rgb_idle_idx(arg);
            }
            break;
        case CMD_MODE_TABLE:
            data[3] = RGBLIGHT_MODES;
            for (uint8_t i = 0; i < 28; i++) {
                uint8_t mode = arg + i;
                data[4 + i]  = mode >= 1 && mode <= RGBLIGHT_MODES ? led_effect_of(mode) : 0xFF;
            }
            return;
        case CMD_LED_REVERT:
            if (hid_writes_allowed()) {
                rgblight_reload_from_eeprom();
            }
            break;
        case CMD_LED_ENABLE:
            if (hid_writes_allowed()) {
                if (arg) {
                    rgblight_enable_noeeprom();
                } else {
                    rgblight_disable_noeeprom();
                }
            }
            break;
#endif
        case CMD_EXIT_BRIDGE:
            if (hid_writes_allowed() && cfg.state != ST_OFF && cfg.state != ST_CLOSING) {
                config_mode_exit();
            }
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
#ifdef RGBLIGHT_ENABLE
    data[5] = user_config_rgb_idle_idx();
    data[6] = RGBLIGHT_LIMIT_VAL;
    data[7] = RGBLIGHT_MODES;
    data[8] = rgblight_is_enabled();
#endif
    data[9]  = vial_unlocked ? 1 : 0;
    data[10] = (ask_left + 999) / 1000;
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
#ifdef RGBLIGHT_ENABLE
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
#ifdef RGBLIGHT_ENABLE
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
        case TAB_KEYS:
            cfg.layer = (cfg.layer + LAYERS + dir) % LAYERS;
            load_pending_for_tab();
            break;
        case TAB_KNOBS:
            cfg.knob_slot = (cfg.knob_slot + KNOB_SLOTS + dir) % KNOB_SLOTS;
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
        case TAB_KEYS:
        case TAB_KNOBS:
            pick_step(dir);
            break;
#ifdef RGBLIGHT_ENABLE
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
        case TAB_KEYS:
            if (cfg.sel_row != NO_KEY) {
                dynamic_keymap_set_keycode(cfg.layer, cfg.sel_row, cfg.sel_col, cfg.pending);
                cfg.tx_rows[cfg.layer] |= 1u << cfg.sel_row;
            }
            break;
        case TAB_KNOBS: {
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
#ifdef RGBLIGHT_ENABLE
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

    // Layer keys are swallowed too, so remember which layer key is held: the
    // CONFIG key on that layer (Raise by default) still exits.
    uint8_t layer_of_key = 0xFF;
    if (raw >= QK_MOMENTARY && raw <= QK_MOMENTARY_MAX) {
        layer_of_key = raw & 0x1F;
    } else if (raw >= QK_LAYER_TAP_TOGGLE && raw <= QK_LAYER_TAP_TOGGLE_MAX) {
        layer_of_key = raw & 0x1F;
    } else if (raw >= QK_LAYER_TAP && raw <= QK_LAYER_TAP_MAX) {
        layer_of_key = (raw >> 8) & 0xF;
    }
    if (layer_of_key != 0xFF) {
        if (pressed) {
            cfg.held_layer     = layer_of_key;
            cfg.held_layer_row = row;
            cfg.held_layer_col = col;
        } else if (row == cfg.held_layer_row && col == cfg.held_layer_col) {
            cfg.held_layer = 0;
        }
    }
    if (pressed && (raw == CONFIG_MODE || (cfg.held_layer && keycode_at_keymap_location(cfg.held_layer, row, col) == CONFIG_MODE))) {
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
            if (cfg.tab == TAB_KEYS) {
                if (cfg.sel_held && (row != cfg.sel_row || col != cfg.sel_col)) {
                    set_pending(default_layer_keycode(row, col));
                } else {
                    cfg.sel_row  = row;
                    cfg.sel_col  = col;
                    cfg.sel_held = true;
                    load_pending_for_tab();
                }
            } else if (cfg.tab == TAB_KNOBS) {
                set_pending(default_layer_keycode(row, col));
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
        if (cfg.tab == TAB_KEYS || cfg.tab == TAB_KNOBS) {
            pick_category(dir);
        }
    } else {
        change_value(dir);
    }
}

bool pre_process_record_user(uint16_t keycode, keyrecord_t *record) {
    keyevent_t *ev     = &record->event;
    bool        is_key = ev->type == KEY_EVENT && ev->key.row < MATRIX_ROWS;
    uint8_t     row = ev->key.row, col = ev->key.col;

    bool asking = cfg.state == ST_ASK || hid.state == HS_ASK;

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
#ifdef RGBLIGHT_ENABLE
    if (cfg.tx_led) {
        cfg.tx_led = false;
        buf[0]     = F_LED;
        buf[n++]   = cfg.led.enable;
        buf[n++]   = cfg.led.mode;
        buf[n++]   = led_effect_of(cfg.led.mode);
        buf[n++]   = led_variant_of(cfg.led.mode);
        buf[n++]   = cfg.led.hue;
        buf[n++]   = cfg.led.sat;
        buf[n++]   = cfg.led.val;
        buf[n++]   = cfg.led.speed;
        buf[n++]   = cfg.led.idle;
        buf[n++]   = RGBLIGHT_LIMIT_VAL;
        buf[n++]   = cfg.led.dirty;
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
    cfg_oled_t        now    = {cfg.state, cfg.tab, cfg.layer, hid.state};
    bool              active = now.state != ST_OFF || now.hid == HS_ASK || now.hid == HS_ACTIVE;
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
    uint8_t        screen = active ? 1 + (asking ? 0x80 : 0) + oled_state.state * 8 + oled_state.hid : 0;
    if (screen != last_screen) {
        oled_clear();
        last_screen = screen;
    }
    if (!active) {
        return false;
    }
    // Portrait OLED: 5 characters per line, so each string below is one line.
    static const char tab_names[TAB_COUNT][6] PROGMEM = {"Keys ", "Knobs", "LEDs ", "Pad  "};
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
    if (!is_keyboard_master()) {
        return;
    }
    knob_combo_task();
    oled_sync_task();

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

#ifdef RGBLIGHT_ENABLE
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
