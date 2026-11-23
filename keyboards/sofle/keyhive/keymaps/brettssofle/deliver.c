// Copyright 2026 Brett-D
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Typed page delivery: the keyboard stores sofle-config.html (minified and
// gzipped, config_page_blob.h generated at build time) and types it into a
// browser, so the config page can be had on any computer with nothing
// installed and nothing to download.
//
// 1. Brett opens a new tab (address bar focused), presses CONFIG and, at the
//    OLED's CONF? prompt, P (the physical confirm).
// 2. The keyboard types a short bootstrap data: URL (config_page/bootstrap.html)
//    into the address bar: printable characters only, no Enter or shortcuts.
// 3. Brett presses Enter himself; the bootstrap asks for Enter again; on that
//    second Enter the keyboard types the page as base32 keystrokes
//    (',' + symbols A-Z/2-7 + '.'), one report per symbol: the next key is
//    pressed in the same report that releases the previous one (a release-only
//    report separates equal symbols). The page reads KeyboardEvent.code, so
//    the host keyboard layout doesn't matter for this part.
// 4. The bootstrap unpacks it (the gzip CRC catches errors) and offers
//    "Save as sofle-config.html". If the transfer failed, the right knob
//    sends it again. Esc stops at any time.
//
// While typing, the keyboard's own key and knob events are swallowed (except
// Esc), so nothing can mix into the stream. The bootstrap is typed for a US
// layout (the address must come out right); the page stream works with any.

#include "brettssofle.h"
#include "deliver.h"
#include "config_page_blob.h"
#include "keymap_introspection.h"
#include "usb_driver.h"
#include "usb_endpoints.h"

#define DL_BOOT_REPORT_MS 3      // address bar: one report every 3 ms (down / up)
#define DL_STREAM_REPORT_MS 1    // page: one report per USB frame
#define DL_GO_DELAY_MS 150       // after the triggering key is released
#define DL_NAV_TIMEOUT_MS 60000  // waiting for the first Enter
#define DL_WAIT_TIMEOUT_MS 120000 // waiting for the second Enter / a retry

#define DL_KNOB_ROW 9 // right knob push (retry)
#define DL_KNOB_COL 5

#define DL_DATA_LEN (4 + CONFIG_PAGE_GZ_LEN)
#define DL_SYMBOLS ((DL_DATA_LEN * 8 + 4) / 5)
#define DL_STEPS (DL_SYMBOLS + 2) // ',' + symbols + '.'

extern usb_endpoint_in_t usb_endpoints_in[USB_ENDPOINT_IN_COUNT];

static struct {
    uint8_t  state;
    uint32_t pos;  // TYPE: report index (2 per character); STREAM: step
    uint8_t  held; // HID usage held down (0 = none)
    bool     shift;
    uint16_t last_report;
    uint32_t time;    // state start
    bool     go;      // start the stream once the trigger key is released
    uint8_t  go_row, go_col;
    uint32_t go_time; // trigger key release time (0 = still down)
} dl;
static matrix_row_t dl_swallowed[MATRIX_ROWS];

uint8_t deliver_state(void) {
    return dl.state;
}

uint8_t deliver_percent(void) {
    if (dl.state == DL_TYPE) {
        return dl.pos * 100 / (2 * CONFIG_BOOTSTRAP_LEN);
    }
    if (dl.state == DL_STREAM) {
        return dl.pos * 100 / (DL_STEPS + 1);
    }
    return dl.state == DL_DONE ? 100 : 0;
}

static bool endpoint_idle(void) {
#ifdef NKRO_ENABLE
    if (keymap_config.nkro) {
        return usb_endpoint_in_is_inactive(&usb_endpoints_in[USB_ENDPOINT_IN_SHARED]);
    }
#endif
    return usb_endpoint_in_is_inactive(&usb_endpoints_in[USB_ENDPOINT_IN_KEYBOARD]);
}

static void report(uint8_t key, bool shift) {
    if (dl.held) {
        del_key(dl.held);
    }
    if (dl.shift && !shift) {
        del_mods(MOD_BIT(KC_LSFT));
    } else if (shift && !dl.shift) {
        add_mods(MOD_BIT(KC_LSFT));
    }
    if (key) {
        add_key(key);
    }
    dl.held  = key;
    dl.shift = shift;
    send_keyboard_report();
    dl.last_report = timer_read();
}

static void release_all(void) {
    if (dl.held || dl.shift) {
        report(0, false);
    }
}

static void set_state(uint8_t state) {
    dl.state = state;
    dl.pos   = 0;
    dl.time  = timer_read32();
    dl.go    = false;
}

void deliver_start(void) {
    release_all();
    set_state(DL_TYPE);
}

void deliver_stop(void) {
    release_all();
    set_state(DL_OFF);
}

static uint8_t data_byte(uint32_t i) {
    if (i < 4) {
        return (uint32_t)CONFIG_PAGE_GZ_LEN >> (24 - 8 * i);
    }
    return i < DL_DATA_LEN ? pgm_read_byte(&config_page_gz[i - 4]) : 0;
}

static uint8_t symbol_at(uint32_t index) {
    uint32_t bit   = index * 5;
    uint8_t  value = 0;
    for (uint8_t i = 0; i < 5; i++, bit++) {
        value = value << 1 | ((data_byte(bit >> 3) >> (7 - (bit & 7))) & 1);
    }
    return value;
}

static uint8_t stream_key(uint32_t step) {
    if (step == 0) {
        return KC_COMMA;
    }
    if (step > DL_SYMBOLS) {
        return KC_DOT;
    }
    uint8_t s = symbol_at(step - 1);
    return s < 26 ? KC_A + s : KC_2 + (s - 26);
}

void deliver_task(void) {
    if (dl.state == DL_OFF) {
        return;
    }
    uint32_t waited = timer_elapsed32(dl.time);
    if ((dl.state == DL_WAIT_NAV && waited > DL_NAV_TIMEOUT_MS) || ((dl.state == DL_WAIT_GO || dl.state == DL_DONE) && waited > DL_WAIT_TIMEOUT_MS)) {
        deliver_stop();
        return;
    }
    if (dl.go && dl.go_time && timer_elapsed32(dl.go_time) >= DL_GO_DELAY_MS) {
        set_state(DL_STREAM);
    }
    if (dl.state != DL_TYPE && dl.state != DL_STREAM) {
        return;
    }
    uint8_t interval = dl.state == DL_TYPE ? DL_BOOT_REPORT_MS : DL_STREAM_REPORT_MS;
    if (timer_elapsed(dl.last_report) < interval || !endpoint_idle()) {
        return;
    }
    if (dl.state == DL_TYPE) {
        if (dl.pos >= 2 * CONFIG_BOOTSTRAP_LEN) {
            set_state(DL_WAIT_NAV);
            return;
        }
        uint8_t k = pgm_read_byte(&config_bootstrap_keys[dl.pos / 2]);
        if (dl.pos & 1) {
            report(0, false); // key up
        } else {
            report(k & 0x7F, k & 0x80);
        }
        dl.pos++;
        return;
    }
    // DL_STREAM
    if (dl.pos >= DL_STEPS) {
        release_all();
        set_state(DL_DONE);
        return;
    }
    uint8_t k = stream_key(dl.pos);
    if (k == dl.held) {
        report(0, false); // equal symbols need a release in between
        return;
    }
    report(k, false);
    dl.pos++;
}

static bool is_enter(uint8_t row, uint8_t col) {
    uint16_t kc = keycode_at_keymap_location(get_highest_layer(default_layer_state), row, col);
    if (kc >= QK_MOD_TAP && kc <= QK_LAYER_TAP_MAX) {
        kc &= 0xFF;
    }
    return kc == KC_ENTER || kc == KC_KP_ENTER;
}

static bool is_escape(uint8_t row, uint8_t col) {
    uint16_t kc = keycode_at_keymap_location(get_highest_layer(default_layer_state), row, col);
    if (kc >= QK_MOD_TAP && kc <= QK_LAYER_TAP_MAX) {
        kc &= 0xFF;
    }
    return kc == KC_ESCAPE;
}

bool deliver_process(keyrecord_t *record) {
    keyevent_t  *ev     = &record->event;
    bool         is_key = ev->type == KEY_EVENT && ev->key.row < MATRIX_ROWS && ev->key.col < MATRIX_COLS;
    uint8_t      row = ev->key.row, col = ev->key.col;
    matrix_row_t bit = is_key ? (matrix_row_t)1 << col : 0;

    if (is_key && !ev->pressed && (dl_swallowed[row] & bit)) {
        dl_swallowed[row] &= ~bit;
        if (dl.go && row == dl.go_row && col == dl.go_col) {
            dl.go_time = timer_stamp32();
        }
        return true;
    }
    if (dl.state == DL_OFF) {
        return false;
    }
    bool typing = dl.state == DL_TYPE || dl.state == DL_STREAM;
    if (is_key && ev->pressed && is_escape(row, col)) {
        deliver_stop();
        if (typing) {
            dl_swallowed[row] |= bit;
            return true;
        }
        return false; // Esc also reaches the computer while waiting
    }
    if (typing) {
        if (is_key && ev->pressed) {
            dl_swallowed[row] |= bit;
        }
        // Keys pressed before typing started still release normally.
        return !(is_key && !ev->pressed);
    }
    if (!is_key) {
        return false;
    }
    if (dl.state == DL_WAIT_NAV && is_enter(row, col) && !ev->pressed) {
        set_state(DL_WAIT_GO); // the address bar opened the bootstrap
        return false;
    }
    if (ev->pressed && !dl.go && ((dl.state == DL_WAIT_GO && is_enter(row, col)) || (dl.state == DL_DONE && row == DL_KNOB_ROW && col == DL_KNOB_COL))) {
        // Start once this key is released, so its key-up can't land in the stream.
        dl.go      = true;
        dl.go_row  = row;
        dl.go_col  = col;
        dl.go_time = 0;
        dl_swallowed[row] |= bit; // kept from the computer (the page starts on the stream itself)
        return true;
    }
    return false;
}
