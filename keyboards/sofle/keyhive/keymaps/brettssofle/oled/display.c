// Copyright 2026 Brett-D
// SPDX-License-Identifier: GPL-2.0-or-later
//
// v27, Brett: "maybe should add a brightness slider for the oled display as
// well or a menu to turn off via menu on the display and adjust brightness".
//
// Settings (master, EECONFIG_KB via kb_config.c): brightness 1-5, sleep after
// 30 s - 30 min (no "never": burn-in), Off. Synced to the right half, which
// drives the OLED, with RPC_ID_USER_DISP; its reply brings back the right
// half's loop profile (perf.c), so the config page can show both halves.
//
// The Display menu, on the INFO page (as the pet's care menu on the pet page):
// a right-knob turn opens it (on that page only, the turn isn't PgUp/PgDn);
// turns move between Brite / Sleep / Off / Back; a tap on Brite or Sleep edits
// it (turns change it live, another tap saves); Off turns the screen off;
// Back (or 6 s without a turn) closes it; a 0.5-2 s hold leaves the page.
// While Off, a right-knob press only wakes it (page_key.c never sees it); a
// left-knob press wakes it and still does its own job. Config, CONF?, network
// and delivery screens always show, even when Off.

#include QMK_KEYBOARD_H
#include "transactions.h"
#include "split_util.h"
#include "kb_config.h"
#include "brett_util.h"
#include "display.h"

#define MENU_MS 6000
#define SYNC_MIN_MS 50
#define SYNC_MAX_MS 1000
#define DISP_MAGIC 0xD5
#define PERF_MAGIC0 0x5F
#define PERF_MAGIC1 0xA7

enum { M_BRIGHT, M_SLEEP, M_OFF, M_BACK, M_COUNT };
enum { DF_OFF = 1, DF_MENU = 2, DF_EDIT = 4 };

static const uint8_t  bright_value[OLED_BRIGHT_LEVELS] = {16, 64, 128, 192, 255};
static const uint32_t sleep_ms[OLED_SLEEP_CHOICES]     = {30000, 60000, 120000, 300000, 600000, 1800000};
static const char     sleep_name[OLED_SLEEP_CHOICES][4] = {"30s", " 1m", " 2m", " 5m", "10m", "30m"};

typedef struct {
    uint8_t  magic, bright, sleep, flags, sel, reset_seq;
    uint16_t left_loop; // the left half's longest pass, last 10 s (10 us)
} disp_m2s_t;
_Static_assert(sizeof(disp_m2s_t) <= RPC_M2S_BUFFER_SIZE, "disp_m2s_t too big for the split RPC");
_Static_assert(2 + PERF_REPORT_LEN <= RPC_S2M_BUFFER_SIZE, "the perf report doesn't fit the split RPC reply");

/* ---- master ---- */

static uint8_t  bright = OLED_BRIGHT_DEFAULT, sleep_i = OLED_SLEEP_DEFAULT;
static bool     off, dirty;
static struct {
    bool     on, edit;
    uint8_t  sel;
    uint32_t t;
} menu;
static uint8_t  reset_seq;
static uint8_t  right_perf[PERF_REPORT_LEN];
static uint32_t right_at; // 0 = no report yet

static void save(void) {
    kb_config_t c;
    kb_config_get(&c);
    c.oled_bright = bright;
    c.oled_sleep  = sleep_i;
    c.oled_off    = off;
    kb_config_set(&c);
    dirty = false;
}

bool display_menu_open(void) {
    return menu.on;
}

void display_menu_close(void) {
    menu.on = menu.edit = false;
    if (dirty) {
        save();
    }
}

bool display_menu_turn(int8_t dir) {
    if (off) {
        return false;
    }
    menu.t = timer_read32();
    if (!menu.on) {
        menu.on  = true; // opens on its first choice
        menu.sel = M_BRIGHT;
        return true;
    }
    if (!menu.edit) {
        menu.sel = (uint8_t)((menu.sel + M_COUNT + dir) % M_COUNT);
    } else if (menu.sel == M_BRIGHT) {
        int8_t v = (int8_t)bright + dir;
        bright   = v < 1 ? 1 : v > OLED_BRIGHT_LEVELS ? OLED_BRIGHT_LEVELS : (uint8_t)v;
        dirty    = true;
    } else if (menu.sel == M_SLEEP) {
        int8_t v = (int8_t)sleep_i + dir;
        sleep_i  = v < 0 ? 0 : v >= OLED_SLEEP_CHOICES ? OLED_SLEEP_CHOICES - 1 : (uint8_t)v;
        dirty    = true;
    }
    return true;
}

bool display_menu_press(void) {
    if (!menu.on) {
        return false;
    }
    menu.t = timer_read32();
    switch (menu.sel) {
        case M_BRIGHT:
        case M_SLEEP:
            menu.edit = !menu.edit;
            if (!menu.edit && dirty) {
                save();
            }
            return true;
        case M_OFF:
            off = true;
            display_menu_close();
            save();
            return true;
        default: // Back
            display_menu_close();
            return true;
    }
}

bool display_is_off(void) {
    return off;
}

void display_wake(void) {
    if (off) {
        off = false;
        save();
    }
}

void display_get(uint8_t *b, uint8_t *s, bool *o) {
    *b = bright;
    *s = sleep_i;
    *o = off;
}

bool display_set(uint8_t b, uint8_t s, bool o) {
    if (b < 1 || b > OLED_BRIGHT_LEVELS || s >= OLED_SLEEP_CHOICES) {
        return false;
    }
    bright  = b;
    sleep_i = s;
    off     = o;
    if (off) {
        menu.on = menu.edit = false;
    }
    save();
    return true;
}

bool display_right_perf(uint8_t out[PERF_REPORT_LEN]) {
    if (!right_at || timer_elapsed32(right_at) > 5000) {
        return false;
    }
    memcpy(out, right_perf, PERF_REPORT_LEN);
    return true;
}

void display_perf_reset(void) {
    perf_reset();
    reset_seq++;
}

/* ---- right half ---- */

static disp_m2s_t rx = {.magic = DISP_MAGIC, .bright = OLED_BRIGHT_DEFAULT, .sleep = OLED_SLEEP_DEFAULT};
static bool       rx_got;

static void disp_handler(uint8_t in_len, const void *in_data, uint8_t out_len, void *out_data) {
    disp_m2s_t m;
    if (in_len < sizeof(m)) {
        return;
    }
    memcpy(&m, in_data, sizeof(m));
    if (m.magic != DISP_MAGIC) {
        return;
    }
    if (rx_got && m.reset_seq != rx.reset_seq) {
        perf_reset();
    }
    rx     = m;
    rx_got = true;
    if (out_len >= 2 + PERF_REPORT_LEN) {
        uint8_t *o = out_data;
        o[0]       = PERF_MAGIC0;
        o[1]       = PERF_MAGIC1;
        perf_report(&o[2]);
    }
}

void display_init(void) {
    transaction_register_rpc(RPC_ID_USER_DISP, disp_handler);
    if (is_keyboard_master()) {
        kb_config_t c;
        kb_config_get(&c);
        bright  = c.oled_bright;
        sleep_i = c.oled_sleep;
        off     = c.oled_off;
    }
}

void display_task(void) {
    if (!is_keyboard_master()) {
        return;
    }
    if (menu.on && timer_elapsed32(menu.t) > MENU_MS) {
        display_menu_close();
    }
    static disp_m2s_t sent;
    static uint16_t   sent_t;
    static bool       sent_once;
    if (!is_transport_connected()) {
        return;
    }
    disp_m2s_t now = {.magic = DISP_MAGIC, .bright = bright, .sleep = sleep_i, .sel = menu.sel, .reset_seq = reset_seq, .left_loop = perf_loop_max()};
    now.flags      = (off ? DF_OFF : 0) | (menu.on ? DF_MENU : 0) | (menu.edit ? DF_EDIT : 0);
    uint16_t el    = timer_elapsed(sent_t);
    bool changed   = !sent_once || now.bright != sent.bright || now.sleep != sent.sleep || now.flags != sent.flags || now.sel != sent.sel || now.reset_seq != sent.reset_seq;
    if (!((changed && el >= SYNC_MIN_MS) || el >= SYNC_MAX_MS)) {
        return;
    }
    uint8_t reply[2 + PERF_REPORT_LEN] = {0};
    sent_t = timer_read();
    if (transaction_rpc_exec(RPC_ID_USER_DISP, sizeof(now), &now, sizeof(reply), reply)) {
        sent      = now;
        sent_once = true;
        if (reply[0] == PERF_MAGIC0 && reply[1] == PERF_MAGIC1) { // a v27+ right half
            memcpy(right_perf, &reply[2], PERF_REPORT_LEN);
            right_at = timer_stamp32();
        }
    }
}

#ifdef OLED_ENABLE
uint16_t display_left_loop(void) {
    return rx_got ? rx.left_loop : 0;
}

bool display_oled_off(bool busy, uint32_t idle_ms) {
    static uint8_t applied;
    uint8_t        b = rx.bright >= 1 && rx.bright <= OLED_BRIGHT_LEVELS ? rx.bright : OLED_BRIGHT_DEFAULT;
    if (busy) {
        return false; // config / CONF? / network / delivery screens always show
    }
    if ((rx.flags & DF_OFF) || idle_ms > sleep_ms[rx.sleep < OLED_SLEEP_CHOICES ? rx.sleep : OLED_SLEEP_DEFAULT]) {
        return true;
    }
    if (applied != b) {
        oled_set_brightness(bright_value[b - 1]);
        applied = b;
    }
    return false;
}

// "  3/5 " style lines: 5 characters, editing: "< 3 >".
static void value_line(const char *v3, bool edit) {
    char s[6] = {edit ? '<' : ' ', v3[0], v3[1], v3[2], edit ? '>' : ' ', 0};
    oled_write(s, edit);
}

bool display_menu_showing(void) {
    return rx.flags & DF_MENU;
}

bool display_menu_render(void) {
    if (!(rx.flags & DF_MENU)) {
        return false;
    }
    bool edit = rx.flags & DF_EDIT;
    oled_write_P(PSTR("DISP "), true);
    oled_write_P(PSTR("     "), false);
    char b[4] = {' ', (char)('0' + (rx.bright <= OLED_BRIGHT_LEVELS ? rx.bright : OLED_BRIGHT_DEFAULT)), ' ', 0};
    oled_write_P(PSTR("Brite"), rx.sel == M_BRIGHT && !edit);
    value_line(b, edit && rx.sel == M_BRIGHT);
    oled_write_P(PSTR("     "), false);
    oled_write_P(PSTR("Sleep"), rx.sel == M_SLEEP && !edit);
    value_line(sleep_name[rx.sleep < OLED_SLEEP_CHOICES ? rx.sleep : OLED_SLEEP_DEFAULT], edit && rx.sel == M_SLEEP);
    oled_write_P(PSTR("     "), false);
    oled_write_P(PSTR("Off  "), rx.sel == M_OFF);
    oled_write_P(PSTR("     "), false);
    oled_write_P(PSTR("Back "), rx.sel == M_BACK);
    oled_write_P(PSTR("     "), false);
    oled_write_P(edit ? PSTR("Turn= +/- Tap= save ") : PSTR("Turn= pickTap= do   "), false);
    return true;
}
#endif
