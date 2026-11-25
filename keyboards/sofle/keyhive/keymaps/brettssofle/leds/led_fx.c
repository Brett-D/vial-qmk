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
#include "led_fx.h"
#include "led_map.h"

#ifdef RGB_MATRIX_ENABLE
#    include "transactions.h"
#    include "split_util.h"
#include "brett_util.h"

#    define LAYERS_EE_OFFSET 80 // after the LED map (0-75) and the OLED page (78-79)
#    define LAYERS_EE_OLD 0x4C  // v6-v19: magic, base, react, hue, sat, sum
#    define LAYERS_EE_MAGIC 0x4D
#    define BRIGHT_EE_OFFSET 76 // v21 key-press brightness + check, between the LED map (0-75) and the OLED page
#    define BRIGHT_EE_CHECK 0x5B
#    define BRIGHT_EE_GAMMA 0x80 // v23: brightness settings converted for the output curve (bit 7 of the value byte)
// v25b: the output curve, in the check byte: b[1] = b[0] ^ CHECK ^ code. Code 0
// = v23-v25's gamma 2.2 (no choice then): moved to Balanced once.
#    define CURVE_CODE_LEGACY 0
#    define FX_SYNC_MS 2000

_Static_assert(LAYERS_EE_OFFSET + 8 <= EECONFIG_KB_DATA_SIZE, "raise EECONFIG_KB_DATA_SIZE");
_Static_assert(1 + LED_MAP_COUNT + 1 <= BRIGHT_EE_OFFSET && BRIGHT_EE_OFFSET + 2 <= 78, "key-press brightness overlaps the LED map or the OLED page");

static fx_settings_t fx;
static bool          gamma_done; // v23 conversion of the brightness settings done (EEPROM)
static bool          pre_gamma;  // lighting saved by v6-v22 firmware: the brightness needs converting
static uint8_t       from_curve; // the curve the saved brightness was set for (converted to fx.curve once)
static uint32_t      remote_at; // right half: last RPC_ID_USER_LEDFX (newer than the old sync)

static const fx_settings_t fx_default = {.base = 0, .effect = FX_RING, .blend = FX_REPLACE, .colour = FX_OPPOSITE, .hue = 0, .sat = 0,
                                         .speed = 8, .width = 4, .radius = 8, .fade = 6, .echoes = 0, .dim = 2, .bright = 0, .curve = FX_CURVE_BALANCED};

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
           s->speed < 16 && s->width < 16 && s->radius < 16 && s->fade < 16 && s->echoes < 4 && s->dim < 8 && s->bright <= 100 && s->curve < FX_CURVE_COUNT && (s->base || s->effect);
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
    bool          record = true;
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
        fx     = fx_default;
        record = false;
    }
    uint8_t b[2]; // v21; before: the background's brightness
    eeconfig_read_kb_datablock(b, BRIGHT_EE_OFFSET, sizeof(b));
    uint8_t code = b[0] ^ b[1] ^ BRIGHT_EE_CHECK; // v25b: the curve
    bool    ok   = code <= FX_CURVE_COUNT;
    fx.bright    = ok && (b[0] & 0x7F) <= 100 ? b[0] & 0x7F : 0;
    gamma_done   = ok && (b[0] & BRIGHT_EE_GAMMA) && code != CURVE_CODE_LEGACY;
    // What the saved brightness was set for: v23-v25 gamma 2.2 (code 0), the
    // chosen curve (v25b), or linear (v6-v22: a brightness record without the
    // gamma bit, or only the lighting record). Nothing saved at all is a new or
    // reset block: its settings are new already (converting them could raise a
    // converted brightness twice).
    fx.curve   = ok && code != CURVE_CODE_LEGACY ? code - 1 : FX_CURVE_BALANCED;
    from_curve = ok && (b[0] & BRIGHT_EE_GAMMA) ? (code == CURVE_CODE_LEGACY ? FX_CURVE_SMOOTH : fx.curve) : FX_CURVE_LINEAR;
    pre_gamma  = ok ? !gamma_done : record;
}

void led_fx_save(void) {
    uint8_t p[8];
    pack(&fx, p);
    eeconfig_update_kb_datablock(p, LAYERS_EE_OFFSET, sizeof(p));
    uint8_t v    = fx.bright | (gamma_done ? BRIGHT_EE_GAMMA : 0);
    uint8_t b[2] = {v, (uint8_t)(v ^ BRIGHT_EE_CHECK ^ (fx.curve + 1))};
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
        s.curve   = in_len >= 12 && p[11] < FX_CURVE_COUNT ? p[11] : FX_CURVE_SMOOTH; // v25b; v23-v25 left halves: gamma 2.2
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
    static uint8_t  sent[10];
    static uint32_t sent_at;
    uint8_t         p[12];
    pack(&fx, p);
    p[8]  = fire_row < 0 ? 0xFF : fire_row;
    p[9]  = fire_col < 0 ? 0xFF : fire_col;
    p[10] = fx.bright; // v21 (a v20 right half reads the first 10)
    p[11] = fx.curve;  // v25b
    if (fire_row < 0 && !memcmp(p, sent, 8) && sent[8] == p[10] && sent[9] == p[11] && sent_at && timer_elapsed32(sent_at) < FX_SYNC_MS) {
        return;
    }
    if (transaction_rpc_send(RPC_ID_USER_LEDFX, sizeof(p), p)) {
        memcpy(sent, p, 8);
        sent[8]  = p[10];
        sent[9]  = p[11];
        sent_at  = timer_stamp32();
        fire_row = fire_col = -1;
    }
}

// The LEDs go out through an output curve (rgb_driver.c; v23: gamma 2.2,
// v25b: a choice, Balanced by default). When the saved brightness was set for
// another curve (v6-v22: linear; v23-v25: gamma 2.2), it is converted once so a
// full-level LED gives exactly the light it gave before (same peak, same
// power); from then on the sliders follow the curve.
static uint8_t curve_level(uint8_t c, uint8_t v) {
    return fx_curve16(c, (uint16_t)v << 8, RGB_MATRIX_MAXIMUM_BRIGHTNESS, fx_gamma_k(RGB_MATRIX_MAXIMUM_BRIGHTNESS)) >> 8;
}

static uint8_t curve_inverse(uint8_t c, uint8_t light) {
    uint32_t k = fx_gamma_k(RGB_MATRIX_MAXIMUM_BRIGHTNESS);
    uint8_t  x = 0;
    while (x < RGB_MATRIX_MAXIMUM_BRIGHTNESS && fx_curve16(c, (uint16_t)x << 8, RGB_MATRIX_MAXIMUM_BRIGHTNESS, k) + 128 < ((uint16_t)light << 8)) x++; // within half a level
    return x;
}

static uint8_t convert_level(uint8_t v) {
    return v ? curve_inverse(fx.curve, curve_level(from_curve, v > RGB_MATRIX_MAXIMUM_BRIGHTNESS ? RGB_MATRIX_MAXIMUM_BRIGHTNESS : v)) : 0;
}

static void gamma_convert(void) {
    uint8_t v = rgb_matrix_get_val();
    if (v) {
        uint8_t n = convert_level(v);
        rgb_matrix_sethsv(rgb_matrix_get_hue(), rgb_matrix_get_sat(), n ? n : 1);
    }
    if (fx.bright) {
        uint8_t x = convert_level(fx_overlay_v(fx.bright, 0, RGB_MATRIX_MAXIMUM_BRIGHTNESS));
        uint8_t b = (uint8_t)(((uint16_t)x * 100 + RGB_MATRIX_MAXIMUM_BRIGHTNESS / 2) / RGB_MATRIX_MAXIMUM_BRIGHTNESS);
        fx.bright = b ? (b > 100 ? 100 : b) : 1;
    }
    gamma_done = true;
    led_fx_save();
}

void led_layers_init(void) {
    if (is_keyboard_master()) {
        load();
        if (!gamma_done && pre_gamma) {
            gamma_convert();
        } else if (!gamma_done) {
            gamma_done = true; // nothing to convert: just mark it
            led_fx_save();
        }
    } else {
        fx = fx_default;
        transaction_register_rpc(RPC_ID_USER_LEDFX, remote_handler);
    }
}

uint8_t led_fx_curve(void) {
    return fx.curve < FX_CURVE_COUNT ? fx.curve : FX_CURVE_BALANCED;
}

uint8_t led_fx_base(void) {
    return fx.base;
}

#endif
