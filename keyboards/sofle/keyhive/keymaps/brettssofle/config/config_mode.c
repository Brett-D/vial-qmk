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

#include "config_internal.h"
#include "display.h"
#ifdef RGB_MATRIX_ENABLE
#    include "screensaver.h"
#endif

// Letters for the connect check (no I/O/Q/Z/B/G/V to avoid look-alikes). One
// letter is enough: it is only shown by the page that received the HELLO, so
// pressing it proves that page has focus.
static const char challenge_letters[] = "ACDEFHJKLMNPRSTUWXY";
#define CHALLENGE_LEN 1

cfg_t cfg;
hid_session_t hid;
nav_t nav;
matrix_row_t held_swallowed[MATRIX_ROWS];
cfg_oled_t oled_state;

/* ---------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------ */

static bool is_knob(uint8_t row, uint8_t col) {
    return (row == KNOB_L_ROW && col == KNOB_L_COL) || (row == KNOB_R_ROW && col == KNOB_R_COL);
}

uint16_t default_layer_keycode(uint8_t row, uint8_t col) {
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

uint16_t basic_of(uint16_t kc) {
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


uint8_t led_effect_of(uint8_t mode) {
    if (mode == led_layers_mode()) {
        return LED_LAYERS_VIALRGB_ID;
    }
    if (mode >= FX_OWN_BG && mode < LED_LAYERS_VIALRGB_ID) {
        return mode; // our own effects keep their ids (led_fx_table.h)
    }
    for (uint8_t i = 0; i < sizeof(supported_modes) / sizeof(supported_modes[0]); i++) {
        if (pgm_read_word(&supported_modes[i].qmk_id) == mode) {
            return pgm_read_word(&supported_modes[i].vialrgb_id);
        }
    }
    return 0;
}

// VialRGB id -> rgb_matrix mode (0 = none / unknown).
static bool led_is_own(uint8_t id) { // our backgrounds / standalone effects (led_fx_table.h)
    return (id >= FX_OWN_BG && id < FX_OWN_BG + FX_OWN_BG_COUNT) || (id >= FX_OWN_SOLO && id < FX_OWN_SOLO + FX_OWN_SOLO_COUNT);
}

uint8_t led_mode_of(uint8_t id) {
    if (id >= FX_OWN_BG && id < LED_LAYERS_VIALRGB_ID) {
        return led_is_own(id) ? id : 0;
    }
    for (uint8_t i = 0; id && i < sizeof(supported_modes) / sizeof(supported_modes[0]); i++) {
        if (pgm_read_word(&supported_modes[i].vialrgb_id) == id) {
            return pgm_read_word(&supported_modes[i].qmk_id);
        }
    }
    return 0;
}

/* LED zones: effect on keys + underglow, keys + solid underglow, keys only. */
zones_t zones;

void zones_load(void) {
    user_config_get_led_zone(&zones.zone, &zones.hue, &zones.sat);
}

void zones_apply_flags(uint8_t zone) {
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

bool map_commit_ok;

bool config_mode_underglow_dark(void) {
    return zones_zone() == ZONE_KEYS_ONLY;
}

bool config_mode_leds_quiet(bool ambient) {
    if (config_mode_led_cal_busy() || cfg.state != ST_OFF || deliver_state() != DL_OFF || deliver_address_typing()) {
        return true;
    }
#    ifdef NETWORK_ENABLE
    if (ambient && net_mode_active()) {
        return true;
    }
#    endif
    return ambient && hid.state == HS_ACTIVE;
}

bool config_mode_led_cal_busy(void) {
    return (is_keyboard_master() ? led_cal_code() : oled_state.cal) != 0;
}

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

// Colour presets (QMK hue, saturation); the pages have the same list (PRESETS).
static const uint8_t led_presets[][2] PROGMEM = {{0, 255}, {16, 255}, {36, 255}, {85, 255}, {128, 255}, {170, 255}, {192, 255}, {224, 180}, {28, 120}, {0, 0}};
#define LED_PRESETS (sizeof(led_presets) / sizeof(led_presets[0]))

// Hold the right knob + turn on Hue / Saturation (or the underglow's): next / previous preset.
static bool led_preset(int8_t dir) {
    bool     ug = cfg.led_field == LF_UG_HUE || cfg.led_field == LF_UG_SAT;
    if (!ug && cfg.led_field != LF_HUE && cfg.led_field != LF_SAT) {
        return false;
    }
    uint8_t *h = ug ? &cfg.led.ug_hue : &cfg.led.hue, *s = ug ? &cfg.led.ug_sat : &cfg.led.sat;
    uint8_t  i = 0;
    for (uint8_t j = 0; j < LED_PRESETS; j++) {
        if (pgm_read_byte(&led_presets[j][0]) == *h && pgm_read_byte(&led_presets[j][1]) == *s) {
            i = (j + LED_PRESETS + dir) % LED_PRESETS;
            break;
        }
        i = dir > 0 ? 0 : LED_PRESETS - 1; // not on a preset: start at either end
    }
    *h = pgm_read_byte(&led_presets[i][0]);
    *s = pgm_read_byte(&led_presets[i][1]);
    cfg.led.dirty = true;
    cfg.tx_led    = true;
    led_preview();
    return true;
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
void block_keys(void) {
    clear_keyboard();
    clear_oneshot_mods();
    layer_clear();
    trackpad_set_drag_scroll(false);
}

// Keyboard-only mode: ask "CONF?" on the OLED first.
static void bridge_ask(void) {
    if (deliver_state() != DL_OFF) {
        deliver_stop(); // e.g. the 30 s repair countdown: CONF? takes over
    }
    block_keys();
    cfg.state    = ST_ASK;
    cfg.ask_time = timer_read32();
}

// Confirmed: start keyboard-only mode and send HELLO to the page.
void bridge_start(void) {
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

void config_mode_exit(void) {
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

// Master: the right OLED shows its pages (no config, CONF?, network, delivery or calibration screen).
bool config_mode_pages_visible(void) {
    if (cfg.state != ST_OFF || hid.state == HS_ASK || hid.state == HS_ACTIVE || deliver_state() != DL_OFF || config_mode_led_cal_busy()) {
        return false;
    }
#ifdef NETWORK_ENABLE
    if (net_mode_active()) {
        return false;
    }
#endif
    return true;
}

void config_mode_toggle(void) {
    if (hid.state == HS_ASK) {
        return; // a web session prompt is on screen
    }
#ifdef NETWORK_ENABLE
    if (net_mode_active()) {
        net_mode_exit(NET_EXIT_KEY); // CONFIG / both knobs leave network mode
        return;
    }
#endif
    if (cfg.state == ST_OFF) {
        bridge_ask();
    } else if (cfg.state == ST_ASK) {
        cfg.state = ST_OFF;
    } else if (cfg.state != ST_CLOSING) {
        config_mode_exit();
    }
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
#ifdef RGB_MATRIX_ENABLE
        if (cfg.tab == TAB_LEDS) {
            led_preset(dir);
        }
#endif
    } else {
        change_value(dir);
    }
}

#ifdef NETWORK_ENABLE
// Network mode got no DHCP request: P leaves it and types the page instead.
static uint32_t fallback_deliver_at;
static bool     net_fallback_key(keyrecord_t *record) {
    keyevent_t *ev = &record->event;
    if (!net_failed() || ev->type != KEY_EVENT || !ev->pressed || ev->key.row >= MATRIX_ROWS) {
        return false;
    }
    if (basic_of(default_layer_keycode(ev->key.row, ev->key.col)) != KC_P) {
        return false;
    }
    held_swallowed[ev->key.row] |= (matrix_row_t)1 << ev->key.col;
    net_mode_exit(NET_EXIT_FALLBACK);
    fallback_deliver_at = timer_stamp32();
    return true;
}
#endif

#ifdef NETWORK_ENABLE
// NET screen: a right-knob click types the address into the selected address
// bar (deliver.c, no Enter). It types on release, so holding both knobs
// (leave network mode) or holding it doesn't type; once per click, and not
// again for a moment after.
#    define NET_TYPE_HOLD_MS 1000
#    define NET_TYPE_AGAIN_MS 1500
static bool     net_type_pending;
static uint32_t net_type_down;
static bool     net_type_key(keyrecord_t *record) {
    keyevent_t *ev = &record->event;
    if (ev->type != KEY_EVENT || ev->key.row != KNOB_R_ROW || ev->key.col != KNOB_R_COL) {
        if (net_type_pending && ev->type == KEY_EVENT && ev->pressed) {
            net_type_pending = false; // another key (e.g. the left knob): not a click
        }
        return false;
    }
    if (ev->pressed) {
        if (!net_mode_active() || nav.on || cfg.state != ST_OFF || hid.state == HS_ASK || matrix_is_on(KNOB_L_ROW, KNOB_L_COL)) {
            return false;
        }
        held_swallowed[KNOB_R_ROW] |= (matrix_row_t)1 << KNOB_R_COL; // not the OLED page / pet key
        net_type_pending = true;
        net_type_down    = timer_read32();
        return true;
    }
    if (!(held_swallowed[KNOB_R_ROW] & ((matrix_row_t)1 << KNOB_R_COL)) || (!net_type_pending && !net_mode_active())) {
        return false;
    }
    held_swallowed[KNOB_R_ROW] &= ~((matrix_row_t)1 << KNOB_R_COL);
    uint32_t typed = deliver_address_typed_at();
    if (net_type_pending && net_mode_active() && hid.state != HS_ASK && timer_elapsed32(net_type_down) < NET_TYPE_HOLD_MS && !matrix_is_on(KNOB_L_ROW, KNOB_L_COL) &&
        !deliver_address_typing() && (!typed || timer_elapsed32(typed) > NET_TYPE_AGAIN_MS)) {
        deliver_type_address();
    }
    net_type_pending = false;
    return true;
}
#endif

bool pre_process_record_user(uint16_t keycode, keyrecord_t *record) {
#ifdef RGB_MATRIX_ENABLE
    if (record->event.pressed) {
        screensaver_input(); // a key or knob ends the screensaver's preview (the idle time restarts by itself)
    }
#endif
    // A web page asking for a session (CONF? Web): its answer comes first, so
    // nothing else can take the key (v24, Brett: in network mode the right
    // knob did nothing while the address it had typed was still going out).
    if (hid.state == HS_ASK && record->event.type == KEY_EVENT && record->event.key.row < MATRIX_ROWS && record->event.pressed) {
        held_swallowed[record->event.key.row] |= (matrix_row_t)1 << record->event.key.col;
#    ifdef NETWORK_ENABLE
        net_type_pending = false; // a click that started before: never types now
#    endif
        ask_key(record->event.key.row, record->event.key.col);
        return false;
    }
#ifdef RGB_MATRIX_ENABLE
    if (led_cal_process(record)) {
        return false; // LED calibration / test: the page reads the key instead
    }
#endif
    if (deliver_process(record)) {
        return false; // typing the config page into the browser
    }
#ifdef NETWORK_ENABLE
    if (net_fallback_key(record) || net_type_key(record)) {
        return false;
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
        // v27: the OLED is Off (Display menu or the config page): a knob press
        // wakes it. The right knob's press is used up (the page stays); the left
        // knob's still does its own job.
        if (is_key && ev->pressed && is_knob(row, col) && display_is_off()) {
            display_wake();
            if (row == KNOB_R_ROW && col == KNOB_R_COL) {
                held_swallowed[row] |= (matrix_row_t)1 << col;
                return false;
            }
        }
#ifdef PET_ENABLE
        if (pet_process(record, oled_page_current() == OLED_PAGE_PET)) {
            return false; // the right knob chooses care on the pet page
        }
#endif
        // v27: the right knob turns the Display menu on the INFO page (display.c).
        if (IS_ENCODEREVENT(*ev) && ev->key.col == 1 && ev->pressed && oled_page_current() == OLED_PAGE_INFO && config_mode_pages_visible() &&
            display_menu_turn(ev->type == ENCODER_CW_EVENT ? 1 : -1)) {
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
        start = timer_stamp32();
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
    deliver_task();
#ifdef NETWORK_ENABLE
    net_task();
    if (fallback_deliver_at && timer_elapsed32(fallback_deliver_at) > 2500) {
        fallback_deliver_at = 0;
        deliver_start(DL_LIGHT); // the plain keyboard is back: type the page instead
    }
#endif
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
        hid_session_end(); // the page stopped polling
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
