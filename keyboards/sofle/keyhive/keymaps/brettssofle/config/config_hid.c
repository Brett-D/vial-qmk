// Copyright 2026 Brett-D
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Config mode: the web session (sofle-config.html over WebHID or the network
// API), confirmed on the keyboard, and the 0xF6 raw HID commands. Without a
// session (or Vial unlock) VIA/Vial writes are refused. See config_mode.c.

#include "config_internal.h"

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
    if (deliver_state() == DL_OFF && deliver_address_typing()) {
        deliver_stop(); // the network address: a page is open already, the rest would go into it
    }
    hid.state = HS_ASK;
    hid.time  = timer_read32();
}

// Answer to a "CONF?" prompt (web session or keyboard-only mode).
void ask_key(uint8_t row, uint8_t col) {
    uint16_t base = basic_of(default_layer_keycode(row, col));
    bool     yes  = (row == KNOB_R_ROW && col == KNOB_R_COL) || base == KC_ENTER || base == KC_SPACE;
    bool     no   = (row == KNOB_L_ROW && col == KNOB_L_COL) || base == KC_ESCAPE;
    if (hid.state != HS_ASK && (base == KC_P || base == KC_F)) {
        cfg.state = ST_OFF;
        deliver_start(base == KC_F ? DL_FULL : DL_LIGHT); // type a config page into the browser (deliver.c)
        return;
    }
#ifdef NETWORK_ENABLE
    if (hid.state != HS_ASK && base == KC_N) {
        cfg.state = ST_OFF;
        net_mode_enter(); // USB network + web page at 192.168.7.1 (net/)
        return;
    }
#endif
    if (!yes && !no) {
        return;
    }
    // v26: the Enter the keyboard typed after the network address (or one you
    // press out of habit right after it) opens the page; it must not also say
    // yes to the session that page then asks for. The knob always counts.
    uint32_t entered = deliver_address_enter_at();
    if (hid.state == HS_ASK && yes && (base == KC_ENTER || base == KC_SPACE) &&
        (timer_elapsed32(hid.time) < CONFIG_ASK_KEY_GUARD_MS || (entered && timer_elapsed32(entered) < CONFIG_ADDR_ENTER_GUARD_MS))) {
        return;
    }
    if (hid.state == HS_ASK) {
        hid.state     = yes ? HS_ACTIVE : HS_DENIED;
        hid.time      = timer_read32();
        hid.led_dirty = false;
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

#ifdef RGB_MATRIX_ENABLE
// Back to the saved lighting (the page's Undo, or a session ending unsaved).
static void hid_led_revert(void) {
    led_layers_revert();
    rgb_matrix_reload_from_eeprom();
    fx_settings_t s;
    led_fx_get(&s);
    led_fx_set(&s); // the mode that goes with them
    zones_load();
    zones_apply_flags(zones.zone);
    hid.led_dirty = false;
}
#endif

// The page closed or stopped polling: previews it didn't save are undone, or
// a later save (a lighting key, Vial, keyboard-only mode) would keep them.
void hid_session_end(void) {
#ifdef RGB_MATRIX_ENABLE
    if (hid.state == HS_ACTIVE && hid.led_dirty) {
        hid_led_revert();
    }
#endif
    hid.state     = HS_NONE;
    hid.led_dirty = false;
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
                hid_led_revert();
            }
            break;
        case CMD_LEDFX: {
            fx_settings_t s;
            led_fx_get(&s);
            bool ok = hid_writes_allowed();
            if (arg == 1 && ok) {
                fx_settings_t n = {.base = led_mode_of(data[3]), .effect = data[4], .blend = data[5], .colour = data[6], .hue = data[7], .sat = data[8],
                                   .speed = data[9], .width = data[10], .radius = data[11], .fade = data[12], .echoes = data[13], .dim = data[14],
                                   .bright = data[15] & 0x80 ? data[15] & 0x7F : s.bright,  // v21 pages set bit 7; older ones keep it
                                   .curve  = data[16] & 0x80 ? data[16] & 0x7F : s.curve}; // v25b, the same way
                ok = (n.base || !data[3]) && led_fx_set(&n); // refused: led_fx_table.h doesn't allow it
                hid.led_dirty |= ok;
            } else if (arg == 2 && ok) {
                led_fx_save(); // the page's last save step (after VIA's lighting save)
                hid.led_dirty = false;
            } else if (arg == 3 && ok) {
                led_layers_revert();
                led_fx_get(&s);
                led_fx_set(&s);
            } else if (arg == 4 && ok) {
                led_fx_fire(data[3], data[4]);
            }
            led_fx_get(&s);
            uint8_t reply[15] = {led_effect_of(s.base), s.effect, s.blend, s.colour, s.hue, s.sat, s.speed, s.width, s.radius, s.fade, s.echoes, s.dim, (uint8_t)(arg == 0 || ok), s.bright, (uint8_t)(0x80 | s.curve)};
            memcpy(&data[3], reply, sizeof(reply)); // [15] = done (0 = refused), [16] = key-press brightness (v21), [17] = 0x80 | curve (v25b)
            return;
        }
        case CMD_SET_ZONE: // preview: zone, underglow hue, underglow sat
            if (hid_writes_allowed()) {
                zones.zone = data[2] < ZONE_COUNT ? data[2] : ZONE_ALL;
                zones.hue  = data[3];
                zones.sat  = data[4];
                zones_apply_flags(zones.zone);
                hid.led_dirty = true;
            }
            break;
        case CMD_LAYERS: {
            uint8_t base = led_mode_of(data[2]), react = data[3] == LED_LAYERS_KEEP ? LED_LAYERS_KEEP : led_mode_of(data[3]);
            bool    ok   = hid_writes_allowed() && (base || !data[2]) && (react || !data[3]) && led_layers_apply(base, react, data[4], data[5]);
            if (ok && data[6]) {
                led_layers_save(); // the light page's save (after VIA's lighting save)
                hid.led_dirty = false;
            } else {
                hid.led_dirty |= ok;
            }
            break;
        }
        case CMD_LOOK:
            if (hid_writes_allowed()) {
                rgb_matrix_set_speed_noeeprom(data[2]);
                rgb_matrix_sethsv_noeeprom(data[3], data[4], data[5]);
                hid.led_dirty = true;
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
                hid.led_dirty = true;
            }
            break;
#endif
        case CMD_EXIT_BRIDGE:
            if (hid_writes_allowed() && cfg.state != ST_OFF && cfg.state != ST_CLOSING) {
                config_mode_exit();
            }
            break;
        case CMD_WIGGLE:
            if (hid_writes_allowed()) {
                wiggler_set(data[2], data[3]);
            }
            break;
        case CMD_NAV:
            if (hid.state == HS_ACTIVE) {
                hid.time = timer_read32();
            }
            nav.on   = arg && hid.state == HS_ACTIVE && cfg.state == ST_OFF;
            nav.time = timer_read32();
            break;
        case CMD_DIAG:
            diag_raw_hid(data); // its own reply layout
            return;
#ifdef PET_ENABLE
        case CMD_PET_GET:
        case CMD_PET_ACT:
        case CMD_PET_SET:
        case CMD_PET_RESTORE:
        case CMD_PET_GLOW:
            if (hid.state == HS_ACTIVE) {
                hid.time = timer_read32();
            }
            pet_raw_hid(data, hid_writes_allowed());
            return;
#endif
        case CMD_PREFS: // v26: [2] 0 get / 1 set; [3] bit 0 = Enter after the network address
            if (arg == 1 && hid_writes_allowed()) {
                net_addr_enter_set(data[3] & 1);
            }
            data[2] = 0xA1;
            data[3] = net_addr_enter() ? 1 : 0;
            data[4] = arg == 1 ? (hid_writes_allowed() ? 1 : 3) : 0;
            return;
        case CMD_REQUEST:
            hid_request();
            break;
        case CMD_END:
            hid_session_end();
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
#ifdef RGB_MATRIX_ENABLE
    {
        uint8_t base, react, hue, sat;
        led_layers_get(&base, &react, &hue, &sat);
        data[26] = led_effect_of(base);
        fx_settings_t s;
        led_fx_get(&s);
        data[27] = s.effect ? LED_LAYERS_KEEP : 0; // v20 overlays aren't rgb_matrix modes: 0xF6 0x1C has them
        data[28] = hue;
        data[29] = sat;
    }
#endif
    data[30] = (wiggler_on() ? 1 : 0) | wiggler_interval() << 1;
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

