// Copyright 2026 Brett-D
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Config mode on the right half's OLED: the left half mirrors its state
// (cfg_oled_t, prefix-compatible with older right halves) and the right half
// draws the CONF? prompt, the config screens, network mode and typed delivery.

#include "config_internal.h"

static const uint8_t cfg_sync_ack[2] = {0xA7, 0x5E}; // reply of a v19+ right half

/* ---------------------------------------------------------------------------
 * Right-half OLED
 * ------------------------------------------------------------------------ */

static uint8_t  left_fw;        // right half: the left half's version (0 = none seen yet)
static uint32_t old_left_since; // right half: first sync without a reply request (an older left half, or a v19 one before its first full sync)
static uint32_t fw_mismatch_at; // right half: when the halves were found to differ

uint8_t config_mode_left_fw(void) {
    return left_fw;
}

// Right half. v18 and older only took exactly their own size, so flashing one
// half with a version whose cfg_oled_t had grown left the right OLED blank in
// config mode (v18 on the left, v17 on the right). Now any length >= 18 is
// read as a prefix, the rest zero; the reply tells the left half so.
void oled_sync_handler(uint8_t in_len, const void *in_data, uint8_t out_len, void *out_data) {
    if (out_len >= sizeof(cfg_sync_ack)) {
        memcpy(out_data, cfg_sync_ack, sizeof(cfg_sync_ack));
    }
    if (in_len >= CFG_SYNC_V16_LEN) {
        uint8_t n = in_len < sizeof(oled_state) ? in_len : sizeof(oled_state);
        memset(&oled_state, 0, sizeof(oled_state));
        memcpy(&oled_state, in_data, n);
        // Only a v19+ left half asks for the reply, with its full payload.
        if (out_len >= sizeof(cfg_sync_ack) && in_len > offsetof(cfg_oled_t, fw)) {
            left_fw = oled_state.fw;
        } else if (!old_left_since) {
            old_left_since = timer_stamp32();
        }
        bool differ = left_fw ? left_fw != BRETT_FW_VERSION : old_left_since && timer_elapsed32(old_left_since) > 2000;
        if (differ && !fw_mismatch_at) {
            fw_mismatch_at = timer_stamp32();
        }
#ifdef RGB_MATRIX_ENABLE
        led_layers_set_remote(oled_state.lay_base, oled_state.lay_react, oled_state.lay_hue, oled_state.lay_sat);
#endif
    }
}

void oled_sync_task(void) {
    static cfg_oled_t last_sent;
    static uint16_t   last_time;
    cfg_oled_t        now    = {cfg.state, cfg.tab, cfg.layer, hid.state, zones.zone, zones.hue, zones.sat, led_cal_code(), nav.on};
#ifdef RGB_MATRIX_ENABLE
    led_layers_get(&now.lay_base, &now.lay_react, &now.lay_hue, &now.lay_sat);
#endif
    now.dl     = deliver_state();
    now.dl_pct = deliver_percent();
    now.dl_pass = deliver_pass();
    if (now.dl == DL_DONE) {
        now.dl_pct = deliver_seconds_left(); // the OLED counts down instead
    }
#ifdef NETWORK_ENABLE
    uint32_t typed = deliver_address_typed_at();
    now.net        = net_mode_active() ? net_state() | (net_failed() ? 0x80 : 0) | (typed && timer_elapsed32(typed) < 2500 ? 0x40 : 0) : 0; // 0x40: v22 "Typed"
    uint32_t ago;
    uint8_t  why = net_last_exit(&ago);
    now.net_bye  = !now.net && why && ago < CONFIG_NET_BYE_MS ? why : 0;
    now.net_secs = now.net && (now.net & 0x0F) != NET_READY ? (net_mode_seconds() > 255 ? 255 : net_mode_seconds()) : 0;
#endif
    bool              active = now.state != ST_OFF || now.hid == HS_ASK || now.hid == HS_ACTIVE || now.cal || now.dl || now.net || now.net_bye;
    // Resent now and then even when idle: the right half may have restarted.
    bool              due    = memcmp(&now, &last_sent, sizeof(now)) != 0 || timer_elapsed(last_time) > (active ? CONFIG_MODE_OLED_SYNC_MS : 5000);
    now.fw = BRETT_FW_VERSION;
    if (!due) {
        return;
    }
    static bool right_v19; // the right half takes any size (it answered with cfg_sync_ack)
    if (!right_v19) {
        // An older right half takes only its own size: send each older layout too.
        uint8_t v13[CFG_SYNC_V13_LEN];
        memcpy(v13, &now, 15); // up to dl_pct
        v13[15] = now.net;
        v13[16] = now.net_bye;
        transaction_rpc_send(RPC_ID_USER_CFG, sizeof(v13), v13);
        transaction_rpc_send(RPC_ID_USER_CFG, CFG_SYNC_V16_LEN, &now);
        transaction_rpc_send(RPC_ID_USER_CFG, CFG_SYNC_V18_LEN, &now);
    }
    uint8_t reply[sizeof(cfg_sync_ack)] = {0};
    if (transaction_rpc_exec(RPC_ID_USER_CFG, sizeof(now), &now, sizeof(reply), reply)) {
        right_v19 = memcmp(reply, cfg_sync_ack, sizeof(reply)) == 0;
        last_sent = now;
        last_time = timer_read();
    }
}

#ifdef OLED_ENABLE
// CONF?, keyboard-only config mode or a typed delivery is on screen, or the
// NET address while the computer is still getting it: the OLED stays on even
// without key presses (each of those ends by itself). A connected page (web
// session) doesn't keep it on: that can last for hours.
#    define NET_OLED_ON_SECS 120
bool config_mode_oled_busy(void) {
    bool net_wait = oled_state.net && (oled_state.net & 0x0F) != NET_READY && oled_state.net_secs < NET_OLED_ON_SECS && oled_state.hid != HS_ACTIVE;
    return oled_state.state != ST_OFF || oled_state.hid == HS_ASK || oled_state.dl || net_wait;
}

bool config_mode_oled_task(void) {
    static uint8_t last_screen;
    bool           asking = oled_state.state == ST_ASK || oled_state.hid == HS_ASK;
    bool           active = oled_state.state != ST_OFF || asking || oled_state.hid == HS_ACTIVE;
    bool           net    = oled_state.net && oled_state.hid != HS_ASK; // CONF? for the page wins
    bool           dl     = oled_state.dl && !asking && oled_state.state == ST_OFF; // CONF? / config mode win over the delivery screen
    bool           halves = !active && !net && !dl && fw_mismatch_at && timer_elapsed32(fw_mismatch_at) < 10000;
    uint8_t        screen = halves ? 0xA0 : !net && oled_state.net_bye && !oled_state.dl && !asking ? 0xB0 | oled_state.net_bye : net ? 0xC0 | (oled_state.hid == HS_ACTIVE ? 0x10 : 0) | (oled_state.net & 0x80 ? 0x08 : 0) | (oled_state.net & 0x07) : dl ? 0xF0 | oled_state.dl : active ? 1 + (asking ? 0x80 : 0) + (oled_state.nav ? 0x40 : 0) + oled_state.state * 8 + oled_state.hid : 0;
    if (screen != last_screen) {
        oled_clear();
        last_screen = screen;
    }
    if (net) {
        // USB network config mode (net/). The address is fixed, so it is on
        // screen from the start (v17 only showed it once the computer had a
        // lease, and Brett had nothing to open while Windows was still busy).
        oled_write_P(PSTR("NET  "), true);
        oled_write_P(PSTR("     192. 168. 7.1       "), false); // rows 1-4
        if (oled_state.net & 0x80) {
            oled_write_P(PSTR("No IPyet  P=   type page "), false); // the fallback: type the page
        } else {
            switch (oled_state.net & 0x0F) {
                case 1: // NET_USB
                    oled_write_P(PSTR("USB  conn."), false);
                    break;
                case 2: // NET_LINK
                    oled_write_P(PSTR("Link up   DHCP "), false);
                    break;
                default: // NET_READY
                    oled_write_P(oled_state.hid == HS_ACTIVE ? PSTR("Page conn.") : PSTR("Ready"), false); // confirmed: the page can save
                    break;
            }
        }
        if ((oled_state.net & 0x0F) != NET_READY) {
            oled_set_cursor(0, 10);
            oled_write(get_u8_str(oled_state.net_secs, ' ') + 1, false); // waiting time: Windows can take ~30 s
            oled_write_P(PSTR("s"), false);
        }
        oled_set_cursor(0, 11);
        oled_write_P(oled_state.net & 0x40 ? PSTR("Typed     ") : PSTR("Knob =type"), oled_state.net & 0x40); // the right knob types the address
        oled_set_cursor(0, 13);
        oled_write_P(PSTR("CONFG=exit "), false);
        return true;
    }
    if (screen >> 4 == 0xB) {
        // Network mode just ended: say why (v10 left it by itself, unseen).
        static const char why[4][11] PROGMEM = {"          ", "by   key  ", "idle 10min", "P=   type "};
        oled_write_P(PSTR("NET  "), true);
        oled_write_P(PSTR("     off  "), false);
        oled_write_P(why[oled_state.net_bye & 3], false);
        return true;
    }
    if (halves) {
        // The halves run different firmware: the right half may not show everything.
        oled_write_P(PSTR("HALVS"), true);
        oled_write_P(PSTR("     diff fw   R v"), false); // rows 1-4 (5 characters a line)
        oled_write(get_u8_str(BRETT_FW_VERSION, ' ') + 1, false);
        oled_set_cursor(0, 5);
        if (left_fw) {
            oled_write_P(PSTR("L v"), false);
            oled_write(get_u8_str(left_fw, ' ') + 1, false);
        } else {
            oled_write_P(PSTR("L old"), false); // v18 or older: no version in its sync
        }
        oled_set_cursor(0, 7);
        oled_write_P(PSTR("Flashboth sides"), false);
        return true;
    }
    if (dl) {
        // Typed page delivery (deliver.c).
        oled_write_P(PSTR("PAGE "), true);
        oled_write_P(PSTR("     "), false);
        // Portrait: 5 characters per line.
        switch (oled_state.dl) {
            case DL_TYPE:
                oled_write_P(PSTR("Typngaddr "), false);
                break;
            case DL_WAIT_NAV:
                oled_write_P(PSTR("PressEnter"), false);
                break;
            case DL_WAIT_GO:
                oled_write_P(PSTR("PressEnteragain"), false);
                break;
            case DL_LOADER:
                oled_write_P(PSTR("Load "), false);
                break;
            case DL_LOADED:
                oled_write_P(PSTR("PressEnter     Knob=again"), false); // knob: the page said the loader is damaged
                break;
            case DL_STREAM:
                oled_write_P(oled_state.dl_pass > 1 ? PSTR("Fix  ") : PSTR("Send "), false); // repair round / the page
                break;
            default:
                // The page says Done or "missing"; the keyboard can't tell which, so it waits for the knob, then stops.
                oled_write_P(PSTR("Sent.     Knob=fix  Esc= done "), false);
                oled_write(get_u8_str(oled_state.dl_pct, ' ') + 1, false);
                oled_write_P(PSTR("s"), false);
                break;
        }
        if (oled_state.dl == DL_TYPE || oled_state.dl == DL_LOADER || oled_state.dl == DL_STREAM) {
            oled_write(get_u8_str(oled_state.dl_pct, ' '), false);
            oled_write_P(PSTR("%"), false);
        }
        if (oled_state.dl == DL_STREAM || oled_state.dl == DL_DONE) {
            oled_set_cursor(0, 11);
            oled_write_P(PSTR("pass "), false);
            oled_write(get_u8_str(oled_state.dl_pass, ' ') + 1, false);
        }
        oled_set_cursor(0, 13);
        oled_write_P(PSTR("Esc= stop "), false);
        return true;
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
        if (oled_state.hid != HS_ASK) {
            oled_write_P(PSTR("P=   lite F=   full "), false); // deliver.c: type the light / full page
#ifdef NETWORK_ENABLE
            oled_write_P(PSTR("N=   net  "), false); // net/
#endif
        }
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

