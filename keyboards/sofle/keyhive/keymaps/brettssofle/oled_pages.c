// Copyright 2026 Brett-D
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Right OLED view pages: status, trackpad, LEDs, info. OLED_PAGE (the right
// knob's press on the base layer) cycles them. The page is chosen and saved
// on the master (EEPROM keyboard datablock, after the LED map) and synced to
// the right half with the values each page shows (RPC_ID_USER_VIEW). View
// only: the 128x32 screen is too small for settings.

#include "brettssofle.h"
#include "led_layers.h"
#include "led_map.h"
#include "transactions.h"
#include "version.h"

#define VIEW_EE_OFFSET 78 // after the LED map (led_map.c: 76 bytes)
#define VIEW_EE_MAGIC 0xA5
#define VIEW_SYNC_MIN_MS 50
#define VIEW_SYNC_MAX_MS 1000
_Static_assert(1 + LED_MAP_COUNT + 1 <= VIEW_EE_OFFSET, "OLED page overlaps the LED map");
_Static_assert(VIEW_EE_OFFSET + 2 <= EECONFIG_KB_DATA_SIZE, "raise EECONFIG_KB_DATA_SIZE");

enum { TPF_TOUCHING = 1, TPF_PRESSED = 2, TPF_CLICK_ON = 4 };

typedef struct {
    uint8_t page;
    uint8_t tp_z, tp_peak, tp_last, tp_flags, tp_thr, tp_rel;
    uint8_t led_on, led_mode, led_val, led_zone, led_base, led_react;
    uint8_t wiggle; // mouse wiggler on (shown on every page)
} view_t;

static view_t view; // master: what it sent; right half: what it shows

static void view_handler(uint8_t in_len, const void *in_data, uint8_t out_len, void *out_data) {
    if (in_len == sizeof(view)) {
        memcpy(&view, in_data, sizeof(view));
    }
}

void oled_pages_init(void) {
    transaction_register_rpc(RPC_ID_USER_VIEW, view_handler);
    if (is_keyboard_master()) {
        uint8_t ee[2];
        eeconfig_read_kb_datablock(ee, VIEW_EE_OFFSET, sizeof(ee));
        view.page = ee[0] == VIEW_EE_MAGIC && ee[1] < OLED_PAGE_COUNT ? ee[1] : 0;
    }
}

void oled_page_next(void) {
    view.page          = (view.page + 1) % OLED_PAGE_COUNT;
    uint8_t ee[2]      = {VIEW_EE_MAGIC, view.page};
    eeconfig_update_kb_datablock(ee, VIEW_EE_OFFSET, sizeof(ee));
}

// Master: send the current page and its values (only those it shows).
void oled_pages_task(void) {
    static view_t   sent;
    static uint16_t sent_time;
    static bool     sent_once;
    if (!is_keyboard_master()) {
        return;
    }
    view_t now = {.page = view.page, .wiggle = wiggler_on()};
#ifdef POINTING_DEVICE_ENABLE
    if (now.page == OLED_PAGE_TRACKPAD) {
        const trackpad_z_t *z = trackpad_z_get();
        now.tp_z              = z->touching ? z->z : 0;
        now.tp_peak           = z->peak;
        now.tp_last           = z->last_peak;
        now.tp_flags          = (z->touching ? TPF_TOUCHING : 0) | (z->pressed ? TPF_PRESSED : 0) | (trackpad_press_enabled() ? TPF_CLICK_ON : 0);
        now.tp_thr            = trackpad_press_threshold();
        now.tp_rel            = trackpad_press_release_z();
    }
#endif
#ifdef RGB_MATRIX_ENABLE
    if (now.page == OLED_PAGE_LEDS) {
        uint8_t hue, sat;
        now.led_on   = rgb_matrix_is_enabled();
        now.led_mode = rgb_matrix_get_mode();
        now.led_val  = rgb_matrix_get_val();
        user_config_get_led_zone(&now.led_zone, &hue, &sat);
        if (now.led_mode == led_layers_mode()) {
            led_layers_get(&now.led_base, &now.led_react, &hue, &sat);
        }
    }
#endif
    uint16_t elapsed = timer_elapsed(sent_time);
    bool     changed = !sent_once || memcmp(&now, &sent, sizeof(now)) != 0;
    if ((changed && elapsed >= VIEW_SYNC_MIN_MS) || elapsed >= VIEW_SYNC_MAX_MS) {
        if (transaction_rpc_send(RPC_ID_USER_VIEW, sizeof(now), &now)) {
            sent      = now;
            sent_once = true;
        }
        sent_time = timer_read(); // retry later if the right half didn't answer
    }
}

/* ---------------------------------------------------------------------------
 * Right half: drawing (portrait, 5 characters x 16 lines)
 * ------------------------------------------------------------------------ */

#ifdef OLED_ENABLE

// Two lines (10 characters) per effect.
static const char *effect_name(uint8_t mode) {
    switch (mode) {
#    ifdef RGB_MATRIX_ENABLE
        case RGB_MATRIX_SOLID_COLOR: return "Solidcolor";
#        ifdef ENABLE_RGB_MATRIX_ALPHAS_MODS
        case RGB_MATRIX_ALPHAS_MODS: return "Alpha mods";
#        endif
#        ifdef ENABLE_RGB_MATRIX_GRADIENT_LEFT_RIGHT
        case RGB_MATRIX_GRADIENT_LEFT_RIGHT: return "Gradient  ";
#        endif
#        ifdef ENABLE_RGB_MATRIX_BREATHING
        case RGB_MATRIX_BREATHING: return "Breathing ";
#        endif
#        ifdef ENABLE_RGB_MATRIX_CYCLE_ALL
        case RGB_MATRIX_CYCLE_ALL: return "Cycle all ";
#        endif
#        ifdef ENABLE_RGB_MATRIX_CYCLE_LEFT_RIGHT
        case RGB_MATRIX_CYCLE_LEFT_RIGHT: return "Cycle L-R ";
#        endif
#        ifdef ENABLE_RGB_MATRIX_CYCLE_OUT_IN
        case RGB_MATRIX_CYCLE_OUT_IN: return "Cycle in  ";
#        endif
#        ifdef ENABLE_RGB_MATRIX_RAINBOW_MOVING_CHEVRON
        case RGB_MATRIX_RAINBOW_MOVING_CHEVRON: return "Chevron   ";
#        endif
#        ifdef ENABLE_RGB_MATRIX_CYCLE_PINWHEEL
        case RGB_MATRIX_CYCLE_PINWHEEL: return "Pinwheel  ";
#        endif
#        ifdef ENABLE_RGB_MATRIX_CYCLE_SPIRAL
        case RGB_MATRIX_CYCLE_SPIRAL: return "Spiral    ";
#        endif
#        ifdef ENABLE_RGB_MATRIX_RAINBOW_BEACON
        case RGB_MATRIX_RAINBOW_BEACON: return "Beacon    ";
#        endif
#        ifdef ENABLE_RGB_MATRIX_RAINDROPS
        case RGB_MATRIX_RAINDROPS: return "Raindrops ";
#        endif
#        ifdef ENABLE_RGB_MATRIX_JELLYBEAN_RAINDROPS
        case RGB_MATRIX_JELLYBEAN_RAINDROPS: return "Jellybean ";
#        endif
#        ifdef ENABLE_RGB_MATRIX_HUE_WAVE
        case RGB_MATRIX_HUE_WAVE: return "Hue wave  ";
#        endif
#        ifdef ENABLE_RGB_MATRIX_PIXEL_RAIN
        case RGB_MATRIX_PIXEL_RAIN: return "Pixelrain ";
#        endif
#        ifdef ENABLE_RGB_MATRIX_TYPING_HEATMAP
        case RGB_MATRIX_TYPING_HEATMAP: return "Heatmap   ";
#        endif
#        ifdef ENABLE_RGB_MATRIX_DIGITAL_RAIN
        case RGB_MATRIX_DIGITAL_RAIN: return "Digi rain ";
#        endif
#        ifdef ENABLE_RGB_MATRIX_SOLID_REACTIVE_SIMPLE
        case RGB_MATRIX_SOLID_REACTIVE_SIMPLE: return "Reactive  ";
#        endif
#        ifdef ENABLE_RGB_MATRIX_SOLID_REACTIVE
        case RGB_MATRIX_SOLID_REACTIVE: return "React hue ";
#        endif
#        ifdef ENABLE_RGB_MATRIX_SOLID_REACTIVE_WIDE
        case RGB_MATRIX_SOLID_REACTIVE_WIDE: return "React wide";
#        endif
#        ifdef ENABLE_RGB_MATRIX_SOLID_REACTIVE_MULTIWIDE
        case RGB_MATRIX_SOLID_REACTIVE_MULTIWIDE: return "Multi wide";
#        endif
#        ifdef ENABLE_RGB_MATRIX_SOLID_REACTIVE_CROSS
        case RGB_MATRIX_SOLID_REACTIVE_CROSS: return "Cross     ";
#        endif
#        ifdef ENABLE_RGB_MATRIX_SOLID_REACTIVE_MULTICROSS
        case RGB_MATRIX_SOLID_REACTIVE_MULTICROSS: return "MultiCross";
#        endif
#        ifdef ENABLE_RGB_MATRIX_SOLID_REACTIVE_NEXUS
        case RGB_MATRIX_SOLID_REACTIVE_NEXUS: return "Nexus     ";
#        endif
#        ifdef ENABLE_RGB_MATRIX_SOLID_REACTIVE_MULTINEXUS
        case RGB_MATRIX_SOLID_REACTIVE_MULTINEXUS: return "MultiNexus";
#        endif
#        ifdef ENABLE_RGB_MATRIX_SPLASH
        case RGB_MATRIX_SPLASH: return "Splash    ";
#        endif
#        ifdef ENABLE_RGB_MATRIX_MULTISPLASH
        case RGB_MATRIX_MULTISPLASH: return "MultiSplsh";
#        endif
#        ifdef ENABLE_RGB_MATRIX_SOLID_SPLASH
        case RGB_MATRIX_SOLID_SPLASH: return "SolidSplsh";
#        endif
#        ifdef ENABLE_RGB_MATRIX_SOLID_MULTISPLASH
        case RGB_MATRIX_SOLID_MULTISPLASH: return "SolidMulti";
#        endif
#    endif
        default: return "Effect    ";
    }
}

// "  42 " style: a number right-aligned in 4 characters plus a space.
static void write_num(uint16_t value, bool invert) {
    oled_write(get_u16_str(value, ' ') + 1, invert); // 4 characters
    oled_write_P(PSTR(" "), false);
}

static void write_bar(uint8_t value, uint8_t max) {
    uint8_t filled = ((uint16_t)value * 5 + max / 2) / max;
    for (uint8_t i = 0; i < 5; i++) {
        oled_write_P(PSTR(" "), i < filled);
    }
}

static void write_uptime(void) {
    uint32_t m = timer_read32() / 60000;
    char     s[6];
    if (m < 60) {
        snprintf(s, sizeof(s), "%3lum ", (unsigned long)m);
    } else if (m < 10 * 60) {
        snprintf(s, sizeof(s), "%luh%02lum", (unsigned long)(m / 60), (unsigned long)(m % 60));
    } else if (m < 24 * 60) {
        snprintf(s, sizeof(s), "%luh%02lu", (unsigned long)(m / 60), (unsigned long)(m % 60));
    } else {
        snprintf(s, sizeof(s), "%lud%02luh", (unsigned long)(m / 1440 % 10), (unsigned long)(m / 60 % 24));
    }
    oled_write(s, false);
}

static void page_trackpad(void) {
    oled_write_P(PSTR("TPAD "), true);
    oled_write_P(PSTR("Z    "), false);
    write_num(view.tp_z, view.tp_flags & TPF_TOUCHING);
    write_bar(view.tp_z, 63);
    oled_write_P(PSTR("Peak "), false);
    write_num(view.tp_peak, false);
    oled_write_P(PSTR("Last "), false);
    write_num(view.tp_last, false);
    oled_write_P(PSTR("Click"), false);
    oled_write_P(view.tp_flags & TPF_CLICK_ON ? PSTR("  on ") : PSTR("  off"), view.tp_flags & TPF_PRESSED);
    oled_write_P(PSTR("Thr  "), false);
    write_num(view.tp_thr, false);
    oled_write_P(PSTR("Rel  "), false);
    write_num(view.tp_rel, false);
}

static void page_leds(void) {
    static const char *const zones[3] = {"Keys+glow ", "Keys+solid", "Keys only "};
    oled_write_P(PSTR("LEDS "), true);
    oled_write_P(PSTR("     "), false);
    if (!view.led_on) {
        oled_write_P(PSTR(" off "), false);
        return;
    }
    if (view.led_mode == led_layers_mode()) {
        oled_write(view.led_base ? effect_name(view.led_base) : "Dark      ", false);
        oled_write_P(PSTR("  +  "), false);
        oled_write(effect_name(view.led_react), false);
    } else {
        oled_write(effect_name(view.led_mode), false);
        oled_write_P(PSTR("     "), false);
    }
    oled_write_P(PSTR("Zone "), false);
    oled_write(zones[view.led_zone < 3 ? view.led_zone : 0], false);
    oled_write_P(PSTR("Brt  "), false);
    write_num(view.led_val, false);
    write_bar(view.led_val, RGB_MATRIX_MAXIMUM_BRIGHTNESS);
}

static void page_info(void) {
    static const char date[] = QMK_BUILDDATE; // "YYYY-MM-DD-hh:mm:ss"
    oled_write_P(PSTR("INFO "), true);
    oled_write_P(PSTR("     Build"), false);
    char s[6];
    memcpy(s, &date[5], 5); // MM-DD
    s[5] = 0;
    oled_write(s, false);
    memcpy(s, &date[11], 5); // hh:mm
    oled_write(s, false);
    oled_write_P(PSTR("     Up   "), false);
    write_uptime();
    oled_write_P(PSTR("     Cfg  "), false);
    oled_write_P(PSTR("  v8 "), false);
}

// Last line: page number, "W" while the mouse wiggler is on.
static void footer(uint8_t page) {
    oled_set_cursor(0, 15);
    char s[6] = "  1/4";
    s[0]      = view.wiggle ? 'W' : ' ';
    s[2]      = '1' + page;
    s[4]      = '0' + OLED_PAGE_COUNT;
    oled_write(s, false);
}

// Right half: draws the selected page; false = page 0 (the caller draws the status).
bool oled_pages_render(void) {
    static uint8_t last = 0xFF;
    if (view.page != last) {
        oled_clear();
        last = view.page;
    }
    oled_set_cursor(0, 0);
    switch (view.page) {
        case OLED_PAGE_TRACKPAD:
            page_trackpad();
            break;
        case OLED_PAGE_LEDS:
            page_leds();
            break;
        case OLED_PAGE_INFO:
            page_info();
            break;
        default:
            return false;
    }
    footer(view.page);
    return true;
}

void oled_pages_status_footer(void) {
    footer(0);
}

#endif
