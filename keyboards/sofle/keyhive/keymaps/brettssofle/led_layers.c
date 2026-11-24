// Copyright 2026 Brett-D
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Layered lighting (v20 engine): a whole-keyboard effect ("background": any
// rgb_matrix effect, or one of ours) with a key-press overlay on top (ring,
// echo rings, splash, glow, cross, nexus, shockwave, sparks, lightning, rain
// splash, trail, heat; led_fx_table.h), each press its own event (RGB Matrix's hit tracker:
// the last 8 presses with their position and age, kept on both halves since
// the matrix is mirrored). The LAYERED effect (rgb_matrix_user.inc) draws
// only the background; the overlay is composited once per frame, at flush,
// over everything (rgb_driver.c calls led_fx_pixel), so effects that repaint
// every LED on every render pass (digital rain) can't wipe it, and the two
// never write each other's buffers. The maths is in led_fx_core.h.
//
// Settings: 8 bytes in the EEPROM keyboard datablock (80-87, where v6-v19
// kept their 6-byte "layers" record, which is migrated), synced to the right
// half with RPC_ID_USER_LEDFX (and the old fields in config_mode.c's sync for
// right halves older than v20).

#include "brettssofle.h"
#include "led_layers.h"
#include "led_map.h"

#ifdef RGB_MATRIX_ENABLE
#    include "transactions.h"
#    include "split_util.h"

#    define LAYERS_EE_OFFSET 80 // after the LED map (0-75) and the OLED page (78-79)
#    define LAYERS_EE_OLD 0x4C  // v6-v19: magic, base, react, hue, sat, sum
#    define LAYERS_EE_MAGIC 0x4D
#    define BRIGHT_EE_OFFSET 76 // v21 key-press brightness + check, between the LED map (0-75) and the OLED page
#    define BRIGHT_EE_CHECK 0x5B
#    define FX_SYNC_MS 2000
#    define FX_DIM_STEP 24 // per frame, background dim in / out

_Static_assert(LAYERS_EE_OFFSET + 8 <= EECONFIG_KB_DATA_SIZE, "raise EECONFIG_KB_DATA_SIZE");
_Static_assert(1 + LED_MAP_COUNT + 1 <= BRIGHT_EE_OFFSET && BRIGHT_EE_OFFSET + 2 <= 78, "key-press brightness overlaps the LED map or the OLED page");

static fx_settings_t fx;
static uint8_t       dim_now = 255;
static uint32_t      remote_at; // right half: last RPC_ID_USER_LEDFX (newer than the old sync)

static const fx_settings_t fx_default = {.base = 0, .effect = FX_RING, .blend = FX_REPLACE, .colour = FX_OPPOSITE, .hue = 0, .sat = 0,
                                         .speed = 8, .width = 4, .radius = 8, .fade = 6, .echoes = 0, .dim = 2};

/* ---- packing (EEPROM and RPC) ---- */

static void pack(const fx_settings_t *s, uint8_t p[8]) {
    p[0] = LAYERS_EE_MAGIC;
    p[1] = s->base;
    p[2] = (s->effect & 15) | (s->blend & 3) << 4 | (s->colour & 3) << 6;
    p[3] = s->hue;
    p[4] = s->sat;
    p[5] = (s->speed & 15) | (s->width & 15) << 4;
    p[6] = (s->radius & 15) | (s->fade & 15) << 4;
    p[7] = (s->echoes & 3) | (s->dim & 7) << 2;
    uint8_t sum = 0;
    for (uint8_t i = 0; i < 8; i++) sum += p[i];
    p[7] |= (sum & 7) << 5;
}

static bool unpack(const uint8_t p[8], fx_settings_t *s) {
    uint8_t sum = 0;
    for (uint8_t i = 0; i < 7; i++) sum += p[i];
    sum += p[7] & 0x1F;
    if (p[0] != LAYERS_EE_MAGIC || (sum & 7) != p[7] >> 5) {
        return false;
    }
    *s = (fx_settings_t){.base = p[1], .effect = p[2] & 15, .blend = p[2] >> 4 & 3, .colour = p[2] >> 6, .hue = p[3], .sat = p[4],
                         .speed = p[5] & 15, .width = p[5] >> 4, .radius = p[6] & 15, .fade = p[6] >> 4, .echoes = p[7] & 3, .dim = p[7] >> 2 & 7};
    return s->effect < FX_EFFECT_COUNT && s->blend < FX_BLEND_COUNT;
}

/* ---- which rgb_matrix modes go where ---- */

bool led_layers_is_react(uint8_t mode) {
    switch (mode) {
#    ifdef ENABLE_RGB_MATRIX_SOLID_REACTIVE_SIMPLE
        case RGB_MATRIX_SOLID_REACTIVE_SIMPLE:
#    endif
#    ifdef ENABLE_RGB_MATRIX_SOLID_REACTIVE_WIDE
        case RGB_MATRIX_SOLID_REACTIVE_WIDE:
#    endif
#    ifdef ENABLE_RGB_MATRIX_SOLID_REACTIVE_MULTIWIDE
        case RGB_MATRIX_SOLID_REACTIVE_MULTIWIDE:
#    endif
#    ifdef ENABLE_RGB_MATRIX_SOLID_REACTIVE_CROSS
        case RGB_MATRIX_SOLID_REACTIVE_CROSS:
#    endif
#    ifdef ENABLE_RGB_MATRIX_SOLID_REACTIVE_MULTICROSS
        case RGB_MATRIX_SOLID_REACTIVE_MULTICROSS:
#    endif
#    ifdef ENABLE_RGB_MATRIX_SOLID_REACTIVE_NEXUS
        case RGB_MATRIX_SOLID_REACTIVE_NEXUS:
#    endif
#    ifdef ENABLE_RGB_MATRIX_SOLID_REACTIVE_MULTINEXUS
        case RGB_MATRIX_SOLID_REACTIVE_MULTINEXUS:
#    endif
#    ifdef ENABLE_RGB_MATRIX_SPLASH
        case RGB_MATRIX_SPLASH:
#    endif
#    ifdef ENABLE_RGB_MATRIX_MULTISPLASH
        case RGB_MATRIX_MULTISPLASH:
#    endif
#    ifdef ENABLE_RGB_MATRIX_SOLID_SPLASH
        case RGB_MATRIX_SOLID_SPLASH:
#    endif
#    ifdef ENABLE_RGB_MATRIX_SOLID_MULTISPLASH
        case RGB_MATRIX_SOLID_MULTISPLASH:
#    endif
            return true;
        default:
            return false;
    }
}

// Standalone (FXK_SOLO in led_fx_table.h): reacts to keys itself, so no key-press effect goes with it.
bool led_layers_is_solo(uint8_t mode) {
#    define FX_SOLO_QMK(id, NAME, name, kind, hue) \
        if ((kind)&FXK_SOLO && mode == RGB_MATRIX_##NAME) return true;
    FX_QMK_FLAGS(FX_SOLO_QMK)
#    undef FX_SOLO_QMK
    return mode >= FX_OWN_SOLO && mode < FX_OWN_SOLO + FX_OWN_SOLO_COUNT;
}

static bool is_own_bg(uint8_t mode) {
    return mode >= FX_OWN_BG && mode < FX_OWN_BG + FX_OWN_BG_COUNT;
}

bool led_layers_is_base(uint8_t mode) {
#    ifdef RGB_MATRIX_EFFECT_VIALRGB_DIRECT
    if (mode == RGB_MATRIX_VIALRGB_DIRECT) {
        return false; // Vial's computer-controlled mode
    }
#    endif
    return is_own_bg(mode) || (mode >= 1 && mode < RGB_MATRIX_EFFECT_MAX && mode != led_layers_mode() && !led_layers_is_react(mode) && !led_layers_is_solo(mode));
}

uint8_t led_layers_mode(void) {
    return RGB_MATRIX_CUSTOM_LAYERED;
}

// An old key-press effect (rgb_matrix mode) as the nearest overlay, and back.
static uint8_t effect_of_react(uint8_t mode) {
    switch (mode) {
        case 0:
            return FX_NONE;
#    ifdef ENABLE_RGB_MATRIX_SOLID_REACTIVE_SIMPLE
        case RGB_MATRIX_SOLID_REACTIVE_SIMPLE:
            return FX_GLOW;
#    endif
#    ifdef ENABLE_RGB_MATRIX_SOLID_REACTIVE_WIDE
        case RGB_MATRIX_SOLID_REACTIVE_WIDE:
#    endif
#    ifdef ENABLE_RGB_MATRIX_SOLID_REACTIVE_MULTIWIDE
        case RGB_MATRIX_SOLID_REACTIVE_MULTIWIDE:
#    endif
            return FX_SPLASH;
#    ifdef ENABLE_RGB_MATRIX_SOLID_REACTIVE_CROSS
        case RGB_MATRIX_SOLID_REACTIVE_CROSS:
#    endif
#    ifdef ENABLE_RGB_MATRIX_SOLID_REACTIVE_MULTICROSS
        case RGB_MATRIX_SOLID_REACTIVE_MULTICROSS:
#    endif
            return FX_CROSS;
#    ifdef ENABLE_RGB_MATRIX_SOLID_REACTIVE_NEXUS
        case RGB_MATRIX_SOLID_REACTIVE_NEXUS:
#    endif
#    ifdef ENABLE_RGB_MATRIX_SOLID_REACTIVE_MULTINEXUS
        case RGB_MATRIX_SOLID_REACTIVE_MULTINEXUS:
#    endif
            return FX_NEXUS;
        default:
            return FX_RING; // the splashes: Brett expected a ring
    }
}

static uint8_t react_of_effect(uint8_t effect) {
    switch (effect) {
        case FX_NONE:
            return 0;
#    ifdef ENABLE_RGB_MATRIX_SOLID_REACTIVE_SIMPLE
        case FX_GLOW:
            return RGB_MATRIX_SOLID_REACTIVE_SIMPLE;
#    endif
#    ifdef ENABLE_RGB_MATRIX_SOLID_REACTIVE_MULTICROSS
        case FX_CROSS:
            return RGB_MATRIX_SOLID_REACTIVE_MULTICROSS;
#    endif
#    ifdef ENABLE_RGB_MATRIX_SOLID_REACTIVE_MULTINEXUS
        case FX_NEXUS:
            return RGB_MATRIX_SOLID_REACTIVE_MULTINEXUS;
#    endif
#    ifdef ENABLE_RGB_MATRIX_SOLID_REACTIVE_MULTIWIDE
        case FX_SPLASH:
            return RGB_MATRIX_SOLID_REACTIVE_MULTIWIDE;
#    endif
        default:
#    ifdef ENABLE_RGB_MATRIX_SOLID_MULTISPLASH
            return RGB_MATRIX_SOLID_MULTISPLASH;
#    else
            return 0;
#    endif
    }
}

/* ---- settings ---- */

void led_fx_get(fx_settings_t *s) {
    *s = fx;
}

// The combinations led_fx_table.h allows: a standalone effect has no key-press effect.
static bool valid(const fx_settings_t *s) {
    bool solo = led_layers_is_solo(s->base);
    return (!s->base || led_layers_is_base(s->base) || solo) && !(solo && s->effect) && s->effect < FX_EFFECT_COUNT && s->blend < FX_BLEND_COUNT && s->colour < FX_COLOUR_COUNT &&
           s->speed < 16 && s->width < 16 && s->radius < 16 && s->fade < 16 && s->echoes < 4 && s->dim < 8 && s->bright <= 100 && (s->base || s->effect);
}

// Ours are drawn by LAYERED; QMK's run as themselves when there's no key-press effect.
static uint8_t mode_for(const fx_settings_t *s) {
    return s->effect || s->base >= FX_OWN_BG ? led_layers_mode() : s->base;
}

// Applies (preview): the rgb_matrix mode follows (layered, or the plain background).
bool led_fx_set(const fx_settings_t *s) {
    if (!valid(s)) {
        return false;
    }
    fx           = *s;
    uint8_t mode = mode_for(&fx);
    if (rgb_matrix_get_mode() != mode) {
        rgb_matrix_mode_noeeprom(mode);
    }
    return true;
}

static void load(void) {
    uint8_t p[8];
    eeconfig_read_kb_datablock(p, LAYERS_EE_OFFSET, sizeof(p));
    fx_settings_t s;
    if (unpack(p, &s) && valid(&s)) {
        fx = s;
    } else if (p[0] == LAYERS_EE_OLD && p[5] == (uint8_t)(LAYERS_EE_OLD + p[1] + p[2] + p[3] + p[4])) {
        // v6-v19 record: background + key-press effect + its colour.
        fx        = fx_default;
        fx.base   = led_layers_is_base(p[1]) ? p[1] : 0;
        fx.effect = effect_of_react(p[2]);
        fx.colour = FX_FIXED;
        fx.hue    = p[3];
        fx.sat    = p[4];
    } else {
        fx = fx_default;
    }
    uint8_t b[2]; // v21; before: the background's brightness
    eeconfig_read_kb_datablock(b, BRIGHT_EE_OFFSET, sizeof(b));
    fx.bright = (b[0] ^ BRIGHT_EE_CHECK) == b[1] && b[0] <= 100 ? b[0] : 0;
}

void led_fx_save(void) {
    uint8_t p[8];
    pack(&fx, p);
    eeconfig_update_kb_datablock(p, LAYERS_EE_OFFSET, sizeof(p));
    uint8_t b[2] = {fx.bright, fx.bright ^ BRIGHT_EE_CHECK};
    eeconfig_update_kb_datablock(b, BRIGHT_EE_OFFSET, sizeof(b));
}

void led_layers_save(void) {
    led_fx_save();
}

void led_layers_revert(void) {
    load();
}

/* ---- the v6-v19 interface (raw HID 0xF6 0x14, the OLED, old right halves) ---- */

void led_layers_get(uint8_t *base, uint8_t *react, uint8_t *hue, uint8_t *sat) {
    *base  = fx.base;
    *react = react_of_effect(fx.effect);
    *hue   = fx.hue;
    *sat   = fx.sat;
}

// react: an rgb_matrix key-press mode (mapped to the nearest overlay), 0 =
// none, or LED_LAYERS_KEEP = leave the overlay as it is.
bool led_layers_apply(uint8_t base, uint8_t react, uint8_t hue, uint8_t sat) {
    fx_settings_t s = fx;
    s.base          = base;
    if (react != LED_LAYERS_KEEP) {
        if (react && led_layers_is_solo(react) && !base) {
            s.base   = react; // heatmap / solid reactive: on their own
            s.effect = FX_NONE;
        } else if (react && !led_layers_is_react(react)) {
            return false;
        } else {
            s.effect = effect_of_react(react);
        }
    }
    if (led_layers_is_solo(s.base)) {
        s.effect = FX_NONE;
    }
    s.hue = hue;
    s.sat = sat;
    return led_fx_set(&s);
}

// Right half: from an older left half's config sync (v19 and earlier).
void led_layers_set_remote(uint8_t base, uint8_t react, uint8_t hue, uint8_t sat) {
    if (remote_at && timer_elapsed32(remote_at) < 3 * FX_SYNC_MS) {
        return; // a v20+ left half sends the whole record
    }
    fx.base   = base;
    fx.effect = effect_of_react(react);
    fx.hue    = hue;
    fx.sat    = sat;
}

/* ---- split: settings and test ripples to the right half ---- */

static int8_t fire_row = -1, fire_col = -1;

static void remote_handler(uint8_t in_len, const void *in_data, uint8_t out_len, void *out_data) {
    const uint8_t *p = in_data;
    fx_settings_t  s;
    if (in_len >= 10 && unpack(p, &s)) {
        s.bright  = in_len >= 11 && p[10] <= 100 ? p[10] : 0; // v21; a v20 left half: the background's
        fx        = s;
        remote_at = timer_stamp32();
        if (p[8] < MATRIX_ROWS && p[9] < MATRIX_COLS) {
            rgb_matrix_handle_key_event(p[8], p[9], true);
        }
    }
}

// A ripple from the key at row, col without pressing it (the config page's preview).
void led_fx_fire(uint8_t row, uint8_t col) {
    if (row >= MATRIX_ROWS || col >= MATRIX_COLS) {
        return;
    }
    rgb_matrix_handle_key_event(row, col, true);
    fire_row = row;
    fire_col = col;
}

void led_fx_task(void) {
    if (!is_keyboard_master() || !is_transport_connected()) {
        return;
    }
    static uint8_t  sent[9];
    static uint32_t sent_at;
    uint8_t         p[11];
    pack(&fx, p);
    p[8]  = fire_row < 0 ? 0xFF : fire_row;
    p[9]  = fire_col < 0 ? 0xFF : fire_col;
    p[10] = fx.bright; // v21 (a v20 right half reads the first 10)
    if (fire_row < 0 && !memcmp(p, sent, 8) && sent[8] == p[10] && sent_at && timer_elapsed32(sent_at) < FX_SYNC_MS) {
        return;
    }
    if (transaction_rpc_send(RPC_ID_USER_LEDFX, sizeof(p), p)) {
        memcpy(sent, p, 8);
        sent[8]  = p[10];
        sent_at  = timer_stamp32();
        fire_row = fire_col = -1;
    }
}

void led_layers_init(void) {
    if (is_keyboard_master()) {
        load();
    } else {
        fx = fx_default;
        transaction_register_rpc(RPC_ID_USER_LEDFX, remote_handler);
    }
}

/* ---- drawing ---- */

// LAYERED's background pass for our own backgrounds (rgb_matrix_user.inc).
bool led_fx_background(effect_params_t *params) {
    RGB_MATRIX_USE_LIMITS(led_min, led_max);
    if (fx.base < FX_OWN_BG) {
        for (uint8_t i = led_min; i < led_max; i++) {
            rgb_matrix_set_color(i, 0, 0, 0); // no background: dark
        }
        return rgb_matrix_check_finished_leds(led_max);
    }
    // The hit tracker is a packed struct: copy its ticks (no unaligned 16-bit reads on the M0+).
    uint16_t ht[LED_HITS_TO_REMEMBER];
    uint8_t  hx[LED_HITS_TO_REMEMBER];
    for (uint8_t j = 0; j < g_last_hit_tracker.count; j++) {
        hx[j] = g_last_hit_tracker.x[j];
        ht[j] = g_last_hit_tracker.tick[j];
    }
    fx_bg_ctx_t c = {.t = g_rgb_timer, .hue = rgb_matrix_get_hue(), .sat = rgb_matrix_get_sat(), .val = rgb_matrix_get_val(), .spd = rgb_matrix_get_speed(),
                     .n = g_last_hit_tracker.count, .hx = hx, .ht = ht};
    c.wpm = fx_typing_wpm(c.n, ht); // from the presses both halves see (QMK's WPM counts on the left only)
    for (uint8_t i = led_min; i < led_max; i++) {
        RGB_MATRIX_TEST_LED_FLAGS();
        fx_hsv_t o   = fx_background(fx.base, &c, g_led_config.point[i].x, g_led_config.point[i].y);
        rgb_t    rgb = hsv_to_rgb((hsv_t){o.h, o.s, o.v});
        rgb_matrix_set_color(i, rgb.r, rgb.g, rgb.b);
    }
    return rgb_matrix_check_finished_leds(led_max);
}

uint8_t led_fx_base(void) {
    return fx.base;
}

// Compositing: rgb_driver.c, once per frame at flush, over every LED of this half.
static bool       frame_on;
static uint8_t    frame_v, frame_hue, frame_n;
static fx_prep_t  prep;
static fx_event_t ev[LED_HITS_TO_REMEMBER];
// Press-to-LED distances per event slot (this half's LEDs), kept while the slot holds the same press.
#    define FX_HALF_LEDS (RGB_MATRIX_LED_COUNT / 2)
static uint8_t dcache[LED_HITS_TO_REMEMBER][FX_HALF_LEDS];
static uint8_t dkey[LED_HITS_TO_REMEMBER][2];

bool led_fx_frame_begin(bool calibrating) {
    frame_on = rgb_matrix_is_enabled() && rgb_matrix_get_mode() == led_layers_mode() && fx.effect && !calibrating;
    if (!frame_on) {
        dim_now = 255;
        return false;
    }
    frame_v   = rgb_matrix_get_val();
    frame_hue = rgb_matrix_get_hue();
    fx_prepare(&prep, &fx);
    bool any = false;
    frame_n  = 0;
    for (uint8_t j = 0; j < g_last_hit_tracker.count; j++) {
        fx_event_t *e = &ev[frame_n];
        fx_event_prepare(&prep, e, g_last_hit_tracker.x[j], g_last_hit_tracker.y[j], g_last_hit_tracker.tick[j], g_rgb_timer - g_last_hit_tracker.tick[j]);
        if (fx_alive(&prep, e)) {
            if (dkey[frame_n][0] != e->x || dkey[frame_n][1] != e->y5) {
                dkey[frame_n][0] = e->x, dkey[frame_n][1] = e->y5;
                memset(dcache[frame_n], 0xFF, FX_HALF_LEDS);
            }
            frame_n++; // only the presses still lighting something
            any = true;
        }
    }
    // The background dims while a ripple runs (dim 0 = never), fading in and out.
    uint8_t want = any ? 255 - fx.dim * 28 : 255;
    dim_now      = dim_now < want ? (want - dim_now < FX_DIM_STEP ? want : dim_now + FX_DIM_STEP) : (dim_now - want < FX_DIM_STEP ? want : dim_now - FX_DIM_STEP);
    return true;
}

// The overlay over one LED's background colour (rgb, in place).
void led_fx_pixel(uint8_t led, uint8_t rgb[3]) {
    if (!frame_on || !HAS_ANY_FLAGS(g_led_config.flags[led], rgb_matrix_get_flags())) {
        return;
    }
    if (dim_now != 255) {
        for (uint8_t i = 0; i < 3; i++) rgb[i] = (uint16_t)rgb[i] * dim_now >> 8;
    }
    uint8_t  lx = g_led_config.point[led].x, ly = g_led_config.point[led].y;
    uint16_t best = 0;
    uint8_t  bj   = 0;
    for (uint8_t j = 0; j < frame_n; j++) {
        uint8_t a = fx_intensity_c(&prep, &ev[j], lx, ly, &dcache[j][led % FX_HALF_LEDS]);
        if (fx.effect == FX_HEAT) {
            best += a; // heat adds up
        } else if (a > best) {
            best = a, bj = j;
        }
    }
    if (!best) {
        return;
    }
    uint8_t v      = fx_overlay_v(fx.bright, frame_v, RGB_MATRIX_MAXIMUM_BRIGHTNESS);
    rgb_t   c      = hsv_to_rgb((hsv_t){fx_hue(&prep, &ev[bj], frame_hue, lx, ly), fx_sat(&prep), v});
    uint8_t top[3] = {c.r, c.g, c.b};
    fx_blend(fx.blend, rgb, top, best > 255 ? 255 : (uint8_t)best, v);
    fx_cap(rgb, RGB_MATRIX_MAXIMUM_BRIGHTNESS);
}

#endif
