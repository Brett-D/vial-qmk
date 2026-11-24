// Copyright 2026 Brett-D
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Typed page delivery: the keyboard stores sofle-config.html (minified and
// gzipped, config_page_blob.h generated at build time) and types it into a
// browser, so the config page can be had on any computer with nothing
// installed and nothing to download.
//
// 1. Brett opens a new tab (address bar focused), presses CONFIG and, at the
//    OLED's CONF? prompt, P (light page) or F (full page): the physical
//    confirm.
// 2. The keyboard types a tiny data: URL (page/bootstrap.html, ~680
//    characters: address bars get slower with every character, and the
//    computer fell far behind with a 3 KB one) into the address bar:
//    printable characters only, no Enter or shortcuts.
// 3. Brett presses Enter himself; the page asks for Enter again; on that
//    second Enter (kept from the computer) the keyboard types the loader
//    (page/loader.js, ~4.3K characters) into the page, which checks
//    its hash and runs it. The loader only asks for Enter once the computer
//    has processed all of it, so the next step can't start early. If it
//    arrived damaged the page says to press the right knob (= type it again).
// 4. On the third Enter the keyboard types the page as base32 keystrokes
//    A-Z/2-7, in blocks:
//
//      ',' + 96 symbols (60 bytes: block index u16, gzip length u16, page
//      kind u8, 51 page bytes, CRC-32)   ... one block after another, '...'
//      at the end of the pass.
//
//    Every key is a key-down report and a separate key-up report, 2 ms apart
//    on the first pass (4 ms per key, ~250 keys/s: at 500/s Brett's PC fell
//    behind). Each report waits for the previous one to be collected by the
//    host (endpoint idle), so the progress is what the computer has
//    received. The page reads KeyboardEvent.code, so the host keyboard
//    layout doesn't matter for this part.
// 5. The loader keeps every block whose CRC is right. A block with one
//    dropped or doubled key is mended by trying each insertion / deletion
//    against the CRC-32; worse only costs that block (a lost ',' joins two
//    blocks, which are split again). If blocks are missing after the
//    pass, the right knob sends *repair* blocks (index bit 15 set): repair
//    block r is the XOR of the page blocks s for which deliver_hash(r, s) is odd
//    (the same hash in bootstrap.html), so any repair block can fill any hole
//    and the page solves for the missing blocks (GF(2) elimination). Repair
//    rounds grow (16, 32, 64 ... blocks, at most half the page + 8), so after
//    a few lost blocks the keyboard stops soon after the page says Done; they
//    are slower: 3, then 4 ms per report. The page ends a pass on '...' or
//    after 3 s without keys. After a pass the keyboard waits 30 s for the
//    knob (the OLED counts down) and then finishes by itself; it never starts
//    a repair round on its own. There is no way back from a page to the keyboard (a page
//    can't set the lock LEDs), so the page says when to press the knob.
//    Then it offers "Save as sofle-light.html" / "sofle-config.html". Esc
//    stops at any time.
//
// While typing, the keyboard's own key and knob events are swallowed (except
// Esc), so nothing can mix into the stream. The address and the loader are
// typed for a US layout; the page stream works with any.
//
// Waiting states give up after a few minutes (Esc any time); while waiting,
// the keyboard types normally except for the key that starts the next step.

#include "brettssofle.h"
#include "deliver.h"
#include "config_page_blob.h"
#include "keymap_introspection.h"
#include "usb_driver.h"
#include "usb_endpoints.h"
#include "brett_util.h"

#define DL_BOOT_REPORT_MS 3      // address bar: one report every 3 ms (down / up)
#define DL_LOADER_REPORT_MS 2    // loader: one report every 2 ms
#define DL_BLOCK_PAYLOAD 51
#define DL_BLOCK_BYTES 60        // header 5 + payload 51 + CRC-32
#define DL_BLOCK_SYMBOLS 96      // 60 * 8 / 5
#define DL_BLOCK_KEYS (DL_BLOCK_SYMBOLS + 1)
#define DL_END_KEYS 3            // '...' (one is enough; the rest in case one is lost)
#define DL_GO_DELAY_MS 150       // after the triggering key is released
#define DL_NAV_TIMEOUT_MS 120000  // waiting for the first Enter
#define DL_WAIT_TIMEOUT_MS 180000 // waiting for the second / third Enter
#define DL_DONE_TIMEOUT_MS 30000  // after a pass: the right knob = repair blocks, else finished

#define DL_KNOB_ROW 9 // right knob push (retry)
#define DL_KNOB_COL 5

_Static_assert(DL_BLOCK_BYTES * 8 == DL_BLOCK_SYMBOLS * 5, "a block must be whole base32 symbols");
_Static_assert(CONFIG_PAGE_GZ_LEN < 65536 && CONFIG_LIGHT_GZ_LEN < 65536, "gzip length is sent as 16 bits");

extern usb_endpoint_in_t usb_endpoints_in[USB_ENDPOINT_IN_COUNT];

static struct {
    uint8_t  state;
    uint32_t pos;  // TYPE / LOADER: report index (2 per character); STREAM: key in the pass
    uint8_t  held; // HID usage held down (0 = none)
    bool     shift;
    uint16_t last_report;
    uint8_t  kind;            // DL_LIGHT / DL_FULL
    uint8_t  pass;            // 1 = the page, 2+ = repair rounds
    uint16_t blocks;          // blocks in this pass
    uint16_t repair_next;     // next repair block number
    uint8_t  buf[DL_BLOCK_BYTES];
    uint8_t  interval;        // ms per report in this pass
    uint32_t time;    // state start
    bool     go;      // start go_next once the trigger key is released
    uint8_t  go_next; // DL_LOADER or DL_STREAM
    uint8_t  go_row, go_col;
    uint32_t go_time; // trigger key release time (0 = still down)
} dl;
static matrix_row_t dl_swallowed[MATRIX_ROWS];

// Network mode's address, typed by the right knob on the NET screen
// (config_mode.c): US layout, printable keys only, no Enter (Brett presses it).
#define ADDR_IDLE 0xFF
static const uint8_t addr_keys[] PROGMEM = {KC_H, KC_T, KC_T, KC_P, 0x80 | KC_SCLN, KC_SLSH, KC_SLSH, KC_1, KC_9, KC_2, KC_DOT, KC_1, KC_6, KC_8, KC_DOT, KC_7, KC_DOT, KC_1};
_Static_assert(2 * sizeof(addr_keys) < ADDR_IDLE, "address too long");
static uint8_t  addr_pos = ADDR_IDLE; // report index (2 per character)
static uint32_t addr_done_at;

uint8_t deliver_state(void) {
    return dl.state;
}

uint8_t deliver_percent(void) {
    if (dl.state == DL_TYPE) {
        return dl.pos * 100 / (2 * CONFIG_BOOTSTRAP_LEN);
    }
    if (dl.state == DL_LOADER) {
        return dl.pos * 100 / (2 * CONFIG_LOADER_LEN);
    }
    if (dl.state == DL_STREAM) {
        return dl.pos * 100 / ((uint32_t)dl.blocks * DL_BLOCK_KEYS + DL_END_KEYS);
    }
    return dl.state == DL_DONE ? 100 : 0;
}

uint8_t deliver_pass(void) {
    return dl.pass;
}

uint8_t deliver_seconds_left(void) {
    uint32_t waited = timer_elapsed32(dl.time);
    return dl.state != DL_DONE || waited >= DL_DONE_TIMEOUT_MS ? 0 : (DL_DONE_TIMEOUT_MS - waited + 999) / 1000;
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

void deliver_start(uint8_t kind) {
    release_all();
    set_state(DL_TYPE);
    dl.kind = kind;
    dl.pass = 0;
}

void deliver_stop(void) {
    release_all();
    set_state(DL_OFF);
    addr_pos = ADDR_IDLE;
}

bool deliver_type_address(void) {
    if (dl.state != DL_OFF || addr_pos != ADDR_IDLE) {
        return false;
    }
    release_all();
    addr_pos = 0;
    return true;
}

bool deliver_address_typing(void) {
    return addr_pos != ADDR_IDLE;
}

uint32_t deliver_address_typed_at(void) {
    return addr_done_at;
}

static void address_task(void) {
    if (timer_elapsed(dl.last_report) < DL_BOOT_REPORT_MS || !endpoint_idle()) {
        return;
    }
    if (addr_pos >= 2 * sizeof(addr_keys)) {
        addr_pos     = ADDR_IDLE;
        addr_done_at = timer_read32() | 1;
        return;
    }
    uint8_t k = pgm_read_byte(&addr_keys[addr_pos / 2]);
    if (addr_pos & 1) {
        report(0, false); // key up
    } else {
        report(k & 0x7F, k & 0x80);
    }
    addr_pos++;
}

static const uint8_t *page_gz(void) {
    return dl.kind == DL_FULL ? config_page_gz : config_light_gz;
}

static uint16_t page_len(void) {
    return dl.kind == DL_FULL ? CONFIG_PAGE_GZ_LEN : CONFIG_LIGHT_GZ_LEN;
}

static uint16_t page_blocks(void) {
    return (page_len() + DL_BLOCK_PAYLOAD - 1) / DL_BLOCK_PAYLOAD;
}

// Which page blocks repair block r covers (bootstrap.html: H).
uint32_t deliver_hash(uint32_t r, uint32_t s) {
    uint32_t h = (r + 1) * 0x9E3779B1u ^ (s + 1) * 0x85EBCA77u;
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    h *= 0x297A2D39u;
    return (h ^ h >> 15) & 1;
}

static uint8_t page_byte(uint32_t i) {
    return i < page_len() ? pgm_read_byte(&page_gz()[i]) : 0;
}

// CRC-32 (zlib's), bitwise: the block is checked once per 97 keys.
uint32_t deliver_crc32(const uint8_t *b, uint8_t n) {
    uint32_t c = 0xFFFFFFFF;
    for (uint8_t i = 0; i < n; i++) {
        c ^= b[i];
        for (uint8_t j = 0; j < 8; j++) {
            c = c >> 1 ^ (c & 1 ? 0xEDB88320 : 0);
        }
    }
    return ~c;
}

// Block `n` of this pass into dl.buf.
static void build_block(uint16_t n) {
    uint16_t index = dl.pass == 1 ? n : (0x8000 | (dl.repair_next + n));
    uint16_t len = page_len(), count = page_blocks();
    dl.buf[0]    = index >> 8;
    dl.buf[1]    = index;
    dl.buf[2]    = len >> 8;
    dl.buf[3]    = len;
    dl.buf[4]    = dl.kind;
    uint8_t *p   = dl.buf + 5;
    if (dl.pass == 1) {
        for (uint8_t j = 0; j < DL_BLOCK_PAYLOAD; j++) {
            p[j] = page_byte((uint32_t)n * DL_BLOCK_PAYLOAD + j);
        }
    } else {
        memset(p, 0, DL_BLOCK_PAYLOAD);
        for (uint16_t s = 0; s < count; s++) {
            if (deliver_hash(index & 0x7FFF, s)) {
                for (uint8_t j = 0; j < DL_BLOCK_PAYLOAD; j++) {
                    p[j] ^= page_byte((uint32_t)s * DL_BLOCK_PAYLOAD + j);
                }
            }
        }
    }
    uint32_t crc = deliver_crc32(dl.buf, DL_BLOCK_BYTES - 4);
    dl.buf[56]   = crc >> 24;
    dl.buf[57]   = crc >> 16;
    dl.buf[58]   = crc >> 8;
    dl.buf[59]   = crc;
}

static uint8_t block_symbol(uint8_t i) {
    uint16_t bit   = i * 5;
    uint8_t  value = 0;
    for (uint8_t k = 0; k < 5; k++, bit++) {
        value = value << 1 | ((dl.buf[bit >> 3] >> (7 - (bit & 7))) & 1);
    }
    return value;
}

// Pass `pass`: 1 = the whole page, 2+ = a round of repair blocks.
static void start_pass(void) {
    uint8_t pass = dl.pass + 1;
    if (pass > 1) {
        dl.repair_next += dl.pass > 1 ? dl.blocks : 0;
    } else {
        dl.repair_next = 0;
    }
    set_state(DL_STREAM);
    dl.pass     = pass;
    uint16_t most = page_blocks() / 2 + 8;
    uint16_t size = pass > 1 && pass < 9 ? 16u << (pass - 2) : most;
    dl.blocks     = pass == 1 ? page_blocks() : size < most ? size : most;
    dl.interval = pass == 1 ? 2 : pass == 2 ? 3 : 4;
}

// The key for position `pos` in the pass: ',' starts each block, '...' ends.
static uint8_t stream_key(uint32_t pos) {
    uint16_t block = pos / DL_BLOCK_KEYS;
    uint8_t  at    = pos % DL_BLOCK_KEYS;
    if (block >= dl.blocks) {
        return KC_DOT;
    }
    if (at == 0) {
        build_block(block);
        return KC_COMMA;
    }
    uint8_t s = block_symbol(at - 1);
    return s < 26 ? KC_A + s : KC_2 + (s - 26);
}

void deliver_task(void) {
    if (dl.state == DL_OFF) {
        if (addr_pos != ADDR_IDLE) {
            address_task();
        }
        return;
    }
    uint32_t waited = timer_elapsed32(dl.time);
    uint32_t limit  = dl.state == DL_WAIT_NAV ? DL_NAV_TIMEOUT_MS : dl.state == DL_DONE ? DL_DONE_TIMEOUT_MS : DL_WAIT_TIMEOUT_MS;
    if ((dl.state == DL_WAIT_NAV || dl.state == DL_WAIT_GO || dl.state == DL_LOADED || dl.state == DL_DONE) && waited > limit) {
        deliver_stop();
        return;
    }
    if (dl.go && dl.go_time && timer_elapsed32(dl.go_time) >= DL_GO_DELAY_MS) {
        if (dl.go_next == DL_LOADER) {
            set_state(DL_LOADER);
        } else {
            start_pass();
        }
    }
    if (dl.state != DL_TYPE && dl.state != DL_LOADER && dl.state != DL_STREAM) {
        return;
    }
    uint8_t interval = dl.state == DL_TYPE ? DL_BOOT_REPORT_MS : dl.state == DL_LOADER ? DL_LOADER_REPORT_MS : dl.interval;
    if (timer_elapsed(dl.last_report) < interval || !endpoint_idle()) {
        return;
    }
    if (dl.state == DL_TYPE || dl.state == DL_LOADER) {
        bool           boot = dl.state == DL_TYPE;
        const uint8_t *text = boot ? config_bootstrap_keys : config_loader_keys;
        if (dl.pos >= 2u * (boot ? CONFIG_BOOTSTRAP_LEN : CONFIG_LOADER_LEN)) {
            set_state(boot ? DL_WAIT_NAV : DL_LOADED);
            return;
        }
        uint8_t k = pgm_read_byte(&text[dl.pos / 2]);
        if (dl.pos & 1) {
            report(0, false); // key up
        } else {
            report(k & 0x7F, k & 0x80);
        }
        dl.pos++;
        return;
    }
    // DL_STREAM: key down, then key up in its own report.
    if (dl.held) {
        report(0, false);
        return;
    }
    if (dl.pos >= (uint32_t)dl.blocks * DL_BLOCK_KEYS + DL_END_KEYS) {
        uint8_t pass   = dl.pass;
        uint16_t blocks = dl.blocks;
        set_state(DL_DONE);
        dl.pass   = pass;
        dl.blocks = blocks;
        return;
    }
    report(stream_key(dl.pos), false);
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
    if (dl.state == DL_OFF && addr_pos == ADDR_IDLE) {
        return false;
    }
    bool typing = dl.state == DL_TYPE || dl.state == DL_LOADER || dl.state == DL_STREAM || addr_pos != ADDR_IDLE;
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
    bool knob = row == DL_KNOB_ROW && col == DL_KNOB_COL, enter = is_enter(row, col);
    uint8_t next = DL_OFF;
    if (dl.state == DL_WAIT_NAV && knob) {
        next = DL_LOADER; // the page is open but the keyboard didn't see its Enter
    } else if (dl.state == DL_WAIT_GO && (enter || knob)) {
        next = DL_LOADER;
    } else if (dl.state == DL_LOADED) {
        next = enter ? DL_STREAM : knob ? DL_LOADER : DL_OFF; // knob: the loader arrived damaged
    } else if (dl.state == DL_DONE && knob) {
        next = DL_STREAM; // repair blocks
    }
    if (ev->pressed && !dl.go && next != DL_OFF) {
        // Start once this key is released, so its key-up can't land in the typing.
        dl.go      = true;
        dl.go_next = next;
        dl.go_row  = row;
        dl.go_col  = col;
        dl.go_time = 0;
        dl_swallowed[row] |= bit; // kept from the computer (the page starts on the stream itself)
        return true;
    }
    return false;
}
