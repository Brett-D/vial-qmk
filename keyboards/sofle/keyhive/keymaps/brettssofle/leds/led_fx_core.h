// Copyright 2026 Brett-D
// SPDX-License-Identifier: GPL-2.0-or-later
//
// The lighting engine's maths (led_fx.c), as pure integer functions so
// the host tests can run them and the config page's preview ("FX core" in
// page.html) can match them value for value. Change both together;
// The development test harness (outside this repo) checks that they agree. No division in the per-LED path: the
// reciprocals are worked out once per frame (fx_prepare / fx_event_prepare).
//
// Geometry is RGB Matrix LED space (x 0-224, y 0-64), with y scaled by 5/4
// ("y5") so a ring is round on the keyboard (key pitch ~14 units across, ~11
// down). Effects are listed in led_fx_table.h.

#pragma once

#include <stdint.h>
#include "led_fx_table.h"

enum fx_effect {
    FX_NONE,
#define FX_ENUM(id, NAME, name, kind, hue) FX_##NAME = id,
    FX_OVERLAYS(FX_ENUM)
#undef FX_ENUM
        FX_EFFECT_COUNT
};
enum fx_blend { FX_REPLACE, FX_ADD, FX_TINT, FX_BLEND_COUNT };
enum fx_colour { FX_FIXED, FX_OPPOSITE, FX_RAINBOW, FX_RANDOM, FX_COLOUR_COUNT };
// LED output curve (rgb_driver.c, fx_curve16): v25b's balanced default, v22's linear, v23's gamma 2.2.
enum fx_curve { FX_CURVE_BALANCED, FX_CURVE_LINEAR, FX_CURVE_SMOOTH, FX_CURVE_COUNT };
enum fx_own_bg {
#define FX_ENUM(id, NAME, name, kind, hue) FXB_##NAME = FX_OWN_BG + id,
    FX_OWN_BACKGROUNDS(FX_ENUM)
#undef FX_ENUM
#define FX_ENUM(id, NAME, name, kind, hue) FXB_##NAME = FX_OWN_SOLO + id,
        FX_OWN_SOLOS(FX_ENUM)
#undef FX_ENUM
};
#define FX_OWN_BG_COUNT (0 FX_OWN_BACKGROUNDS(FX_COUNT_ONE))
#define FX_OWN_SOLO_COUNT (0 FX_OWN_SOLOS(FX_COUNT_ONE))
#define FX_COUNT_ONE(id, NAME, name, kind, hue) +1

// The settings, unpacked (led_fx.c packs them into 8 EEPROM bytes).
typedef struct {
    uint8_t base;   // background: rgb_matrix mode, FX_OWN_BG+n, FX_OWN_SOLO+n, 0 = dark
    uint8_t effect; // fx_effect
    uint8_t blend;  // fx_blend
    uint8_t colour; // fx_colour
    uint8_t hue, sat;
    uint8_t speed;  // 0-15: ring speed
    uint8_t width;  // 0-15: band width
    uint8_t radius; // 0-15: how far it goes (and how long trails / heat last)
    uint8_t fade;   // 0-15: 0 = full strength to the end, 15 = fades out evenly
    uint8_t echoes; // 0-3: extra rings (echo rings)
    uint8_t dim;    // 0-7: background brightness while a ripple runs (0 = no change)
    uint8_t bright; // v21: key-press brightness, 1-100 % of the cap (0 = the background's brightness)
    uint8_t curve;  // v25b: LED output curve (fx_curve)
} fx_settings_t;

// Worked out once per frame from the settings.
typedef struct {
    fx_settings_t s;
    uint16_t      R, w, spacing, R3, HR, TT, HT;
    uint8_t       rings;
    uint32_t      speed_q16, band_k, lin_k, trail_k, tt_k, hr_k, ht_k;
} fx_prep_t;

static inline void fx_prepare(fx_prep_t *p, const fx_settings_t *s) {
    p->s         = *s;
    p->R         = 24 + s->radius * 16;
    p->w         = 3 + s->width * 2;
    p->spacing   = 2 * p->w + 8;
    p->rings     = s->effect == FX_WAVE ? s->echoes + 2 : 1;
    p->R3        = p->R / 3;
    p->HR        = 2 * p->w + 8;
    p->TT        = 400 + s->radius * 200;
    p->HT        = 1500 + s->radius * 300;
    p->speed_q16 = (uint32_t)(30 + s->speed * 15) * 65536 / 1000;
    p->band_k    = 255ul * 65536 / p->w;
    p->lin_k     = 255ul * 65536 / p->R;
    p->trail_k   = 255ul * 65536 / (3 * p->w);
    p->tt_k      = 255ul * 65536 / p->TT;
    p->hr_k      = 255ul * 65536 / p->HR;
    p->ht_k      = 255ul * 65536 / p->HT;
}

/* ---- helpers ---- */

static const uint8_t fx_sin_q[65] = {0,  3,  6,  9,  12, 16, 19, 22, 25, 28, 31, 34, 37, 40, 43, 46, 49, 51, 54, 57, 60, 63, 65, 68, 71, 73, 76, 78, 81, 83, 85, 88, 90,
                                     92, 94, 96, 98, 100, 102, 104, 106, 107, 109, 111, 112, 113, 115, 116, 117, 118, 120, 121, 122, 122, 123, 124, 125, 125, 126, 126, 126, 127, 127, 127, 127};

static inline uint8_t fx_sin8(uint8_t a) { // 128 + 127 sin(a / 256 turn)
    uint8_t i = a & 63;
    switch (a >> 6) {
        case 0: return 128 + fx_sin_q[i];
        case 1: return 128 + fx_sin_q[64 - i];
        case 2: return 128 - fx_sin_q[i];
        default: return 128 - fx_sin_q[64 - i];
    }
}

static inline uint32_t fx_hash32(uint32_t h) {
    h ^= h >> 16;
    h *= 0x7feb352du;
    h ^= h >> 15;
    h *= 0x846ca68bu;
    h ^= h >> 16;
    return h;
}

static inline uint8_t fx_div255(uint32_t x) { // x / 255 for x <= 65534
    return (uint8_t)((x + 1 + (x >> 8)) >> 8);
}

// Smoothstep on 0-255: eases in and out, so fades and ring edges have no
// corner (v23; the linear ramps showed as steps).
// fx_smooth_q[x] = x * x * (765 - 2x) / 65025 (no division per LED on the M0+).
static const uint8_t fx_smooth_q[256] = {0,0,0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,3,3,4,4,4,5,5,6,6,7,7,8,9,9,10,11,11,12,13,13,14,15,16,16,17,18,19,20,21,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,39,40,41,42,43,44,45,47,48,49,50,51,53,54,55,56,58,59,60,62,63,64,66,67,68,70,71,72,74,75,77,78,79,81,82,84,85,86,88,89,91,92,94,95,97,98,99,101,102,104,105,107,108,110,111,113,114,116,117,119,120,122,123,125,126,128,129,131,132,134,135,137,138,140,141,143,144,146,147,149,150,152,153,155,156,157,159,160,162,163,165,166,168,169,170,172,173,175,176,177,179,180,182,183,184,186,187,188,190,191,192,194,195,196,198,199,200,201,203,204,205,206,207,209,210,211,212,213,214,215,217,218,219,220,221,222,223,224,225,226,227,228,229,230,231,232,233,233,234,235,236,237,238,238,239,240,241,241,242,243,243,244,245,245,246,247,247,248,248,249,249,250,250,250,251,251,252,252,252,253,253,253,253,254,254,254,254,254,254,254,254,254,255};
static inline uint8_t fx_smooth8(uint8_t x) {
    return fx_smooth_q[x];
}

// Gamma 2.2 at the LEDs (rgb_driver.c): (i / 64)^2.2 * 65535, interpolated.
static const uint16_t fx_gamma_q[65] = {0,7,32,78,147,240,359,504,676,875,1104,1361,1648,1966,2314,2693,3104,3547,4022,4530,5072,5646,6255,6897,7574,8286,9033,9815,10632,11486,12375,13301,14263,15262,16298,17371,18482,19630,20816,22040,23303,24604,25943,27322,28739,30196,31692,33227,34802,36417,38072,39768,41503,43280,45097,46954,48853,50793,52774,54796,56860,58966,61114,63303,65535};

// For fx_gamma16: worked out once for the brightness cap.
static inline uint32_t fx_gamma_k(uint8_t cap) {
    return cap ? 4294901760ul / ((uint32_t)cap << 8) : 0; // 65535 * 65536 / (cap * 256)
}

// x: linear light in 8.8 (0 .. cap * 256) -> the same range with gamma 2.2:
// the cap stays the most the LEDs give (power unchanged), lower levels follow
// what the eye sees, so a fade looks even instead of dropping off late.
static inline uint16_t fx_gamma16(uint16_t x, uint8_t cap, uint32_t k) {
    uint32_t top = (uint32_t)cap << 8;
    if (x > top) x = (uint16_t)top;
    uint32_t t = (uint32_t)x * k >> 16;
    if (t > 65535) t = 65535;
    uint8_t  i = (uint8_t)(t >> 10);
    uint32_t y = fx_gamma_q[i] + (((uint32_t)(fx_gamma_q[i + 1] - fx_gamma_q[i]) * (t & 1023)) >> 10);
    return (uint16_t)(y * top >> 16);
}

// v25b "Balanced" curve: linear above a quarter of the cap (as bright as v22,
// whose effects were made for linear LEDs), a soft toe below it so the dim end
// still fades smoothly: 1/4 f(4u), f(t) = 2t^2 - t^3 (same level and slope at
// the knee). v23's gamma 2.2 for everything made mid-tones, mixed colours and
// sparse effects much darker (Brett: "why are some LED configurations so dim").
static const uint16_t fx_toe_q[65] = {0,124,480,1044,1792,2700,3744,4900,6144,7452,8800,10164,11520,12844,14112,15300,16384,17408,18432,19456,20480,21504,22528,23552,24576,25600,26624,27648,28672,29696,30720,31744,32768,33791,34815,35839,36863,37887,38911,39935,40959,41983,43007,44031,45055,46079,47103,48127,49151,50175,51199,52223,53247,54271,55295,56319,57343,58367,59391,60415,61439,62463,63487,64511,65535};

// x: linear light in 8.8 (0 .. cap * 256) through curve c (k = fx_gamma_k(cap)); the cap stays the most an LED gives.
static inline uint16_t fx_curve16(uint8_t c, uint16_t x, uint8_t cap, uint32_t k) {
    uint32_t top = (uint32_t)cap << 8;
    if (x > top) x = (uint16_t)top;
    if (c == FX_CURVE_LINEAR) return x;
    const uint16_t *q = c == FX_CURVE_SMOOTH ? fx_gamma_q : fx_toe_q;
    uint32_t        t = (uint32_t)x * k >> 16;
    if (t > 65535) t = 65535;
    uint8_t  i = (uint8_t)(t >> 10);
    uint32_t y = q[i] + (((uint32_t)(q[i + 1] - q[i]) * (t & 1023)) >> 10);
    return (uint16_t)(y * top >> 16);
}

static inline uint16_t fx_isqrt(uint32_t n) { // floor(sqrt(n)), n < 2^18 (LED space: < 225^2 + 81^2)
    uint32_t r = 0, bit = 1ul << 16;
    while (bit > n) bit >>= 2;
    while (bit) {
        if (n >= r + bit) {
            n -= r + bit;
            r = (r >> 1) + bit;
        } else {
            r >>= 1;
        }
        bit >>= 2;
    }
    return (uint16_t)r;
}

static inline uint8_t fx_y5(uint8_t y) { return (uint8_t)((y * 5) >> 2); }

/* ---- key-press overlay ---- */

static inline uint8_t fx_band(const fx_prep_t *p, uint16_t x) { // 255 on the ring's centre line, 0 at width (soft edges)
    return x >= p->w ? 0 : fx_smooth8((uint8_t)(255 - ((uint32_t)x * p->band_k >> 16)));
}

// Strength by how far the ring has gone: the fade setting eases it down, and
// it always eases out over the last fifth (v22 stopped dead at the end).
static inline uint8_t fx_fade(const fx_prep_t *p, uint16_t r) {
    if (r >= p->R) return 0;
    uint8_t lin = (uint8_t)((uint32_t)r * p->lin_k >> 16);
    uint8_t f   = (uint8_t)(255 - fx_div255((uint32_t)p->s.fade * 17 * fx_smooth8(lin)));
    if (lin > 204) f = fx_div255((uint32_t)f * fx_smooth8((uint8_t)((255 - lin) * 5)));
    return f;
}

static const uint8_t fx_echo_q[4] = {255, 160, 100, 62}; // each echo ring ~0.62 of the one before


// One press as seen this frame.
typedef struct {
    uint8_t  x, y5;   // where (LED space, y scaled)
    uint16_t t;       // ms since the press
    uint16_t r;       // how far it has gone
    uint32_t seed;    // per press (random colour, sparks, lightning)
    uint8_t  tx, ty5; // lightning: where the bolt goes
    uint16_t len;
    uint32_t inv_len;
    uint8_t  fade; // fx_fade(r): the same for every LED this frame
} fx_event_t;

// press_ms: when it happened on the synced clock (both halves agree to a few ms; >> 11 hides that).
static inline void fx_event_prepare(const fx_prep_t *p, fx_event_t *e, uint8_t x, uint8_t y, uint16_t t, uint32_t press_ms) {
    e->x    = x;
    e->y5   = fx_y5(y);
    e->t    = t;
    e->r    = (uint16_t)((uint32_t)t * p->speed_q16 >> 16);
    e->seed = fx_hash32((uint32_t)x * 73 + (uint32_t)y * 151 + (press_ms >> 11) * 2654435761u);
    e->tx   = (uint8_t)((x + 60 + e->seed % 104) % 224);
    e->ty5  = (uint8_t)((e->seed >> 8) % 81);
    int16_t sx = (int16_t)e->tx - e->x, sy = (int16_t)e->ty5 - e->y5;
    e->len     = fx_isqrt((uint32_t)(sx * sx) + (uint32_t)(sy * sy));
    e->inv_len = e->len ? 65536ul / e->len : 0;
    e->fade    = fx_fade(p, e->r);
}

static inline uint8_t fx_alive(const fx_prep_t *p, const fx_event_t *e) {
    switch (p->s.effect) {
        case FX_TRAIL: return e->t < p->TT;
        case FX_HEAT: return e->t < p->HT;
        default: return e->r < p->R + (p->rings - 1) * p->spacing + p->w;
    }
}

static inline uint8_t fx_ring_at(const fx_prep_t *p, uint16_t d, uint16_t rr, uint8_t fade) {
    return fx_div255((uint32_t)fx_band(p, d > rr ? d - rr : rr - d) * fade);
}

// Overlay strength (0-255) of press e at the LED at lx, ly (LED space).
// Heat adds up over presses; the others take the strongest (led_fx_pixel).
// dc: optional cache of the press-to-LED distance (a press doesn't move;
// 0xFF = not known yet), so the square root runs once per press and LED.
static inline uint8_t fx_intensity_c(const fx_prep_t *p, const fx_event_t *e, uint8_t lx, uint8_t ly, uint8_t *dc) {
    uint8_t  ly5 = fx_y5(ly);
    int16_t  dx = (int16_t)lx - e->x, dy = (int16_t)ly5 - e->y5;
    uint16_t ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
    uint16_t r = e->r;
    // Far outside the ripple: nothing (saves the square root).
    uint16_t reach = p->s.effect == FX_HEAT ? p->HR : p->s.effect == FX_TRAIL || p->s.effect == FX_GLOW ? 9 : p->s.effect == FX_LIGHTNING ? 255 : r + p->w + 1;
    if (ax > reach || ay > reach) return 0;
    uint16_t d;
    if (dc && *dc != 0xFF) {
        d = *dc;
    } else {
        d = fx_isqrt((uint32_t)ax * ax + (uint32_t)ay * ay); // <= 239
        if (dc) *dc = (uint8_t)d;
    }
    switch (p->s.effect) {
        case FX_RING:
            return fx_ring_at(p, d, r, e->fade);
        case FX_WAVE: {
            uint8_t best = 0;
            for (uint8_t k = 0; k < p->rings; k++) {
                uint16_t back = k * p->spacing;
                if (back > r) break;
                uint8_t v = fx_div255((uint32_t)fx_ring_at(p, d, r - back, k ? fx_fade(p, r - back) : e->fade) * fx_echo_q[k & 3]);
                if (v > best) best = v;
            }
            return best;
        }
        case FX_SPLASH:
            return fx_div255((uint32_t)(d <= r ? 255 : fx_band(p, d - r)) * e->fade);
        case FX_GLOW:
            return d > 8 || r >= p->R ? 0 : fx_smooth8((uint8_t)(255 - ((uint32_t)r * p->lin_k >> 16)));
        case FX_CROSS:
        case FX_NEXUS: {
            uint8_t on_row = ay <= 6, on_col = ax <= 6;
            if (!on_row && !on_col) return 0;
            uint16_t a = on_row && on_col ? 0 : on_row ? ax : ay; // distance along the arm
            uint8_t  v = p->s.effect == FX_CROSS ? (a <= r ? 255 : fx_band(p, a - r)) : fx_band(p, a > r ? a - r : r - a);
            return fx_div255((uint32_t)v * e->fade);
        }
        case FX_SHOCKWAVE: {
            uint8_t v;
            if (d > r) {
                v = fx_band(p, d - r);
            } else {
                uint16_t back = r - d; // inside: a trail fading towards the centre
                v             = back >= 3 * p->w ? 0 : (uint8_t)(fx_smooth8((uint8_t)(255 - ((uint32_t)back * p->trail_k >> 16))) * 3 >> 2);
                if (back < p->w) {
                    uint8_t edge = fx_band(p, back);
                    if (edge > v) v = edge;
                }
            }
            return fx_div255((uint32_t)v * e->fade);
        }
        case FX_SPARKS: {
            uint32_t hs = fx_hash32(((uint32_t)lx << 8 | ly5) ^ e->seed);
            if ((hs & 7) || d > r || d < 6) return 0;
            return fx_div255((uint32_t)e->fade * (128 + (hs >> 8 & 127)));
        }
        case FX_DROPLETS: {
            uint16_t rr   = r < p->R3 ? r : p->R3;
            uint8_t  ring = fx_div255((uint32_t)fx_band(p, d > rr ? d - rr : rr - d) * e->fade);
            if (r >= p->R3 && d > p->R3 && d <= 2 * p->R3 && !(fx_hash32(((uint32_t)lx << 8 | ly5) ^ e->seed) & 3)) {
                uint8_t drop = fx_div255((uint32_t)e->fade * 200);
                if (drop > ring) ring = drop;
            }
            return ring;
        }
        case FX_LIGHTNING: {
            if (!e->len || r >= p->R) return 0;
            int16_t  sx = (int16_t)e->tx - e->x, sy = (int16_t)e->ty5 - e->y5;
            int32_t  dot = (int32_t)dx * sx + (int32_t)dy * sy;
            if (dot < 0 || dot > (int32_t)e->len * e->len) return 0;
            uint16_t along = (uint16_t)((uint32_t)dot * e->inv_len >> 16);
            if (along > 3 * r) return 0; // the bolt grows fast
            int32_t  c    = (int32_t)dx * sy - (int32_t)dy * sx;
            int16_t  side = (int16_t)(((uint32_t)(c < 0 ? -c : c) * e->inv_len) >> 16);
            if (c < 0) side = -side;
            int16_t  zig = ((int16_t)fx_sin8((uint8_t)(along * 12 + (e->seed & 255))) - 128) >> 4; // jagged
            int16_t  off = side - zig;
            uint16_t ao  = off < 0 ? -off : off;
            if (ao > 8) return 0;
            uint8_t v     = (uint8_t)(255 - ao * 28);
            uint8_t flick = (e->seed >> ((e->t >> 5) & 15)) & 1 ? 255 : 140;
            return fx_div255((uint32_t)fx_div255((uint32_t)v * flick) * e->fade);
        }
        case FX_TRAIL:
            return d > 8 || e->t >= p->TT ? 0 : fx_smooth8((uint8_t)(255 - ((uint32_t)e->t * p->tt_k >> 16)));
        case FX_HEAT:
            if (d >= p->HR || e->t >= p->HT) return 0;
            return fx_div255((uint32_t)fx_smooth8((uint8_t)(255 - ((uint32_t)d * p->hr_k >> 16))) * fx_smooth8((uint8_t)(255 - ((uint32_t)e->t * p->ht_k >> 16))));
        default:
            return 0;
    }
}

static inline uint8_t fx_intensity(const fx_prep_t *p, const fx_event_t *e, uint8_t lx, uint8_t ly) { return fx_intensity_c(p, e, lx, ly, 0); }

// The overlay's hue at an LED (rainbow: by distance from the press; random: per press).
static inline uint8_t fx_hue(const fx_prep_t *p, const fx_event_t *e, uint8_t base_hue, uint8_t lx, uint8_t ly) {
    switch (p->s.colour) {
        case FX_OPPOSITE:
            return base_hue + 128;
        case FX_RAINBOW: {
            int16_t  dx = (int16_t)lx - e->x, dy = (int16_t)fx_y5(ly) - e->y5;
            uint16_t ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
            return p->s.hue + (uint8_t)(fx_isqrt((uint32_t)ax * ax + (uint32_t)ay * ay) * 2);
        }
        case FX_RANDOM:
            return (uint8_t)(e->seed >> 24);
        default:
            return p->s.hue;
    }
}

// The key-press colour's brightness: its own share of the cap, or the background's setting.
static inline uint8_t fx_overlay_v(uint8_t bright, uint8_t frame_v, uint8_t cap) {
    return bright ? (uint8_t)((uint16_t)(bright > 100 ? 100 : bright) * cap / 100) : frame_v;
}

// The LEDs never go over the brightness cap (Add can sum past it); 8.8.
static inline void fx_cap16(uint16_t rgb[3], uint8_t cap) {
    uint16_t top = (uint16_t)cap << 8;
    for (uint8_t i = 0; i < 3; i++) {
        if (rgb[i] > top) rgb[i] = top;
    }
}

static inline uint8_t fx_sat(const fx_prep_t *p) {
    return p->s.colour == FX_FIXED ? p->s.sat : 255;
}

// Blends the overlay colour c (8-bit) over the background b with strength a
// (V = brightness setting). b is 8.8 fixed point (v23): a fading ring keeps
// its fine steps instead of rounding to the cap's ~100 levels.
static inline void fx_blend16(uint8_t mode, uint16_t b[3], const uint8_t c[3], uint8_t a, uint8_t V) {
    uint16_t top = b[0] > b[1] ? (b[0] > b[2] ? b[0] : b[2]) : (b[1] > b[2] ? b[1] : b[2]);
    uint32_t lum = top > (uint32_t)V * 128 ? top : (uint32_t)V * 128; // tint: the background's brightness, at least half
    for (uint8_t i = 0; i < 3; i++) {
        uint32_t c16 = (uint32_t)c[i] << 8;
        if (mode == FX_ADD) {
            uint32_t v = b[i] + c16 * a / 255;
            b[i]       = v > 65535 ? 65535 : (uint16_t)v;
            continue;
        }
        uint32_t target = mode == FX_TINT && V ? (uint32_t)c[i] * lum / V : c16;
        if (target > 65535) target = 65535; // a background brighter than the setting
        b[i] = target >= b[i] ? (uint16_t)(b[i] + (target - b[i]) * a / 255) : (uint16_t)(b[i] - (b[i] - target) * a / 255);
    }
}

/* ---- our own backgrounds and standalone effects ---- */

typedef struct {
    uint32_t t; // ms, synced clock
    uint8_t  hue, sat, val, spd, wpm;
    uint8_t  n; // presses (typing VU)
    const uint8_t  *hx;
    const uint16_t *ht;
} fx_bg_ctx_t;

typedef struct {
    uint8_t h, s, v;
} fx_hsv_t;

// Typing speed for the typing-speed meter, from the last presses (ms ago): words
// (5 keys) per minute over the recent ones, 0 when typing stopped ~2 s ago.
static inline uint8_t fx_typing_wpm(uint8_t n, const uint16_t *ago) {
    uint16_t newest = 0xFFFF, oldest = 0;
    uint8_t  cnt    = 0;
    for (uint8_t j = 0; j < n; j++) {
        if (ago[j] > 3000) continue;
        cnt++;
        if (ago[j] < newest) newest = ago[j];
        if (ago[j] > oldest) oldest = ago[j];
    }
    if (cnt < 2 || newest > 2000) return 0;
    uint16_t span = oldest - newest < 150 ? 150 : oldest - newest;
    uint32_t wpm  = (uint32_t)(cnt - 1) * 12000 / span;
    return wpm > 255 ? 255 : (uint8_t)wpm;
}

static inline uint8_t fx_scale(uint8_t v, uint8_t k) { return (uint8_t)((uint16_t)v * k >> 8); }

// Colour (HSV) of background `id` at the LED at x, y.
static inline fx_hsv_t fx_background(uint8_t id, const fx_bg_ctx_t *c, uint8_t x, uint8_t y) {
    uint32_t ts = (uint32_t)(c->t * (uint32_t)(c->spd + 32)) >> 7; // effect time, by the speed setting
    uint8_t  y5 = fx_y5(y);
    fx_hsv_t o  = {c->hue, c->sat, c->val};
    switch (id) {
        case FXB_AURORA: {
            uint8_t a = fx_sin8((uint8_t)(x * 3 + (ts >> 3)));
            uint8_t b = fx_sin8((uint8_t)(y5 * 5 - (ts >> 4) + (a >> 2)));
            uint8_t d = fx_sin8((uint8_t)((x + y5) * 2 + (ts >> 5)));
            o.h       = (uint8_t)(c->hue + ((a + d) >> 3) - 32);
            o.v       = fx_scale(c->val, 40 + (b * 215 >> 8));
            break;
        }
        case FXB_FIRE: {
            uint8_t n1   = fx_sin8((uint8_t)(x * 7 + (ts >> 3)));
            uint8_t n2   = fx_sin8((uint8_t)(x * 3 - (ts >> 2) + y5 * 6));
            uint8_t fl   = fx_hash32((ts >> 6) * 31 + x * 7 + y) & 63;
            int16_t heat = (int16_t)((y5 * 3 * (n1 + n2)) >> 8) + fl - 40;
            heat         = heat < 0 ? 0 : heat > 255 ? 255 : heat;
            o.h          = (uint8_t)(heat >> 3);
            o.s          = heat > 200 ? (uint8_t)(255 - (heat - 200) * 2) : 255;
            o.v          = fx_scale(c->val, (uint8_t)heat);
            break;
        }
        case FXB_STARFIELD: {
            uint32_t hs     = fx_hash32((uint32_t)x * 257 + (uint32_t)y * 7 + 1);
            uint32_t period = 1500 + (hs & 2047);
            uint32_t local  = (ts + (hs >> 11)) % period;
            uint8_t  tw     = local >= 400 ? 0 : local < 200 ? (uint8_t)(local * 326 >> 8) : (uint8_t)((400 - local) * 326 >> 8);
            if ((hs >> 24) < 90) {
                o.s       = c->sat >> 2;
                uint8_t v = fx_scale(c->val, tw);
                o.v       = v > (c->val >> 4) ? v : c->val >> 4;
            } else {
                o.v = c->val >> 5;
            }
            break;
        }
        case FXB_OCEAN: {
            uint8_t a = fx_sin8((uint8_t)(x * 4 + y5 * 2 - (ts >> 3)));
            uint8_t b = fx_sin8((uint8_t)(x * 2 - y5 * 3 + (ts >> 4)));
            uint8_t w = (uint8_t)((a + b) >> 1);
            o.h       = (uint8_t)(136 + (w >> 2) - 32);
            o.s       = w > 220 ? (uint8_t)(255 - (w - 220) * 4) : 255;
            o.v       = fx_scale(c->val, 80 + (w * 175 >> 8));
            break;
        }
        case FXB_COMET: {
            uint32_t pos    = (ts >> 2) % 600;
            int16_t  head   = (int16_t)(pos < 300 ? pos : 600 - pos) - 38;
            int16_t  behind = pos < 300 ? head - x : x - head;
            uint8_t  in     = behind < -4 || behind > 70 ? 0 : behind < 0 ? 255 : (uint8_t)(255 - (behind * 933 >> 8));
            o.h             = (uint8_t)(c->hue + (behind > 0 && behind <= 70 ? behind >> 1 : 0));
            uint8_t v       = fx_scale(c->val, in);
            o.v             = v > (c->val >> 5) ? v : c->val >> 5;
            break;
        }
        case FXB_LAVA: {
            uint16_t f = 0;
            for (uint8_t k = 0; k < 3; k++) {
                int16_t  cx = 112 + (((int16_t)fx_sin8((uint8_t)((ts >> 5) + k * 85)) - 128) * 100 >> 7);
                int16_t  cy = 40 + (((int16_t)fx_sin8((uint8_t)((ts >> 6) + k * 70 + 40)) - 128) * 36 >> 7);
                uint16_t d  = (uint16_t)((x > cx ? x - cx : cx - x) + (y5 > cy ? y5 - cy : cy - y5));
                if (d < 60) f += (60 - d) * 4;
            }
            if (f > 255) f = 255;
            o.h = (uint8_t)(c->hue + (f >> 3));
            o.v = fx_scale(c->val, (uint8_t)(30 + (f * 225 >> 8)));
            break;
        }
        case FXB_CANDLE: {
            uint8_t fl   = fx_hash32((ts >> 6) + x * 13 + y * 3) & 63;
            uint8_t slow = fx_sin8((uint8_t)((ts >> 3) + x * 5)) >> 3;
            o.h          = (uint8_t)(18 + (fl >> 4));
            o.s          = 230;
            o.v          = fx_scale(c->val, (uint8_t)(150 + fl + slow));
            break;
        }
        case FXB_SUNRISE: {
            uint8_t ph = fx_sin8((uint8_t)((ts >> 9) + 192));
            o.h        = (uint8_t)(10 + ((uint32_t)(80 - (y5 > 80 ? 80 : y5)) * ph * 7 >> 10)); // warm at the bottom, sky above by day
            o.s        = (uint8_t)(255 - (ph >> 2));
            o.v        = fx_scale(c->val, (uint8_t)(40 + (ph * 215 >> 8)));
            break;
        }
        case FXB_BREATHE_GRADIENT:
            o.h = (uint8_t)(c->hue + (x >> 1));
            o.v = fx_scale(c->val, (uint8_t)(48 + (fx_sin8((uint8_t)(ts >> 3)) * 207 >> 8)));
            break;
        case FXB_CODE_RAIN: {
            uint8_t  col  = (uint8_t)(x * 19 >> 8);
            uint32_t cs   = fx_hash32(col * 977u + 3);
            uint32_t sp   = 20 + (cs & 31);
            int16_t  head = (int16_t)((((uint32_t)(ts * sp) >> 9) + (cs >> 8)) % 120);
            int16_t  back = head - y5;
            if (back < 0 || back >= 30) {
                o.v = 0;
            } else {
                o.s = back < 3 ? c->sat >> 2 : c->sat;
                o.v = fx_scale(c->val, (uint8_t)(255 - (back * 2176 >> 8)));
            }
            break;
        }
        case FXB_TYPING_SPEED: {
            uint8_t wpm = c->wpm > 120 ? 120 : c->wpm;
            o.h         = (uint8_t)(85 - (wpm * 181 >> 8)); // green, to red at 120 wpm
            o.s         = 255;
            o.v         = x <= (wpm * 478 >> 8) ? c->val : c->val >> 5;
            break;
        }
        case FXB_TYPING_VU: {
            uint16_t e = 0;
            for (uint8_t j = 0; j < c->n; j++) {
                uint8_t ax = x > c->hx[j] ? x - c->hx[j] : c->hx[j] - x;
                if (ax < 16 && c->ht[j] < 1200) e += 255 - (c->ht[j] * 218 >> 10);
            }
            uint8_t up = 80 - (y5 > 80 ? 80 : y5); // height from the bottom row
            o.h        = (uint8_t)(up * 272 >> 8 > 85 ? 0 : 85 - (up * 272 >> 8));
            o.s        = 255;
            o.v        = (uint16_t)up * 6 < e ? c->val : c->val >> 5;
            break;
        }
        default:
            o.v = 0;
    }
    return o;
}

/* ---- the pet's LED reactions (v26) ----
 * pet.c starts one (eating, playing, happy, sad, sleeping, hatching, growing
 * up); led_fx_draw.c draws it over everything else for a few seconds, on both
 * halves (led_fx.c syncs the start), in the key-press brightness. It never
 * touches the lighting settings. fx_pet_prepare once per frame, fx_pet_px per
 * LED (no division there). */

enum fx_pet { FXP_NONE, FXP_EAT, FXP_PLAY, FXP_HAPPY, FXP_SAD, FXP_SLEEP, FXP_HATCH, FXP_GROW, FXP_COUNT };
static const uint16_t fx_pet_ms[FXP_COUNT] = {0, 2400, 3000, 1800, 6000, 5000, 1600, 2800};
#define FXP_OLED_X 128 // the right half's OLED in LED space (y5): eating and hatching start there
#define FXP_OLED_Y5 22

typedef struct {
    uint8_t  type, env; // env: the whole reaction's strength now (eases in and out)
    uint16_t t;         // ms since it started
    int16_t  r[3];      // eat: the three pulses' radii (< 0: not yet); hatch: ring, rays; play: sparkle tick
    int16_t  bx, by5;   // play: the ball
    int16_t  sx;        // happy / grow: the sweep's centre
    uint8_t  hue, b;    // play: the ball's colour; sad / sleep: the breath; hatch: the flash
} fx_pet_t;

typedef struct {
    uint8_t a, h, s, v; // strength (before env and the intensity setting), colour; v: share of the brightness
} fx_pet_px_t;

static inline void fx_pet_prepare(fx_pet_t *g, uint8_t type, uint16_t t) {
    uint16_t len = type < FXP_COUNT ? fx_pet_ms[type] : 0;
    g->type = t < len ? type : (uint8_t)FXP_NONE;
    g->t    = t;
    g->env = 0, g->bx = 0, g->by5 = 0, g->sx = 0, g->hue = 0, g->b = 0;
    g->r[0] = g->r[1] = g->r[2] = -1;
    if (!g->type) return;
    uint8_t  in  = t < 120 ? (uint8_t)(t * 17 >> 3) : 255;
    uint32_t rem = (uint32_t)(len - t);
    uint8_t  out = rem * 4 < len ? (uint8_t)(rem * 4 * 255 / len) : 255;
    g->env       = fx_smooth8(in < out ? in : out);
    switch (type) {
        case FXP_EAT: // three warm pulses from the OLED
            for (uint8_t k = 0; k < 3; k++) g->r[k] = t >= k * 500 ? (int16_t)((uint32_t)(t - k * 500) * 3 / 25) : -1;
            break;
        case FXP_PLAY: { // a ball hopping across the keys and back, sparkles around it
            uint16_t p = t % 3000, q = t % 500;
            g->bx      = (int16_t)(8 + (uint32_t)(p < 1500 ? p : 3000 - p) * 208 / 1500);
            g->by5     = fx_y5((uint8_t)(60 - (uint32_t)224 * q * (500 - q) / 250000));
            g->hue     = (uint8_t)((uint32_t)t * 43 >> 8);
            g->r[0]    = (int16_t)(t / 120);
            break;
        }
        case FXP_HAPPY: // a rainbow sweep
            g->sx = (int16_t)(-40 + (int32_t)((uint32_t)t * 304 / len));
            break;
        case FXP_GROW: { // there and back
            uint16_t h = len / 2, q = t < h ? t : len - t;
            g->sx      = (int16_t)(-40 + (int32_t)((uint32_t)q * 304 / h));
            break;
        }
        case FXP_SAD: // two slow breaths (the underglow only)
            g->b = fx_sin8((uint8_t)((uint32_t)t * 512 / len - 64));
            break;
        case FXP_SLEEP: // one, very dim
            g->b = fx_sin8((uint8_t)((uint32_t)t * 256 / len - 64));
            break;
        case FXP_HATCH: // a starburst: flash, ring and rays from the OLED
            g->r[0] = (int16_t)(t * 23 >> 7);
            g->r[1] = (int16_t)(t >> 2 > 200 ? 200 : t >> 2);
            g->b    = t < 200 ? (uint8_t)((uint32_t)(200 - t) * 255 / 200) : 0;
            break;
        default:
            break;
    }
}

static const int8_t fx_pet_dir[8][2] = {{64, 0}, {45, 45}, {0, 64}, {-45, 45}, {-64, 0}, {-45, -45}, {0, -64}, {45, -45}};

// The reaction at the LED at x, y (LED space); under = an underglow LED.
static inline fx_pet_px_t fx_pet_px(const fx_pet_t *g, uint8_t x, uint8_t y, uint8_t under) {
    fx_pet_px_t o   = {0, 0, 255, 255};
    int16_t     ly5 = fx_y5(y);
    switch (g->type) {
        case FXP_EAT: {
            int16_t  dx = (int16_t)x - FXP_OLED_X, dy = ly5 - FXP_OLED_Y5;
            uint16_t d  = fx_isqrt((uint32_t)(dx * dx) + (uint32_t)(dy * dy));
            if (d >= 150) break;
            uint8_t fall = (uint8_t)(255 - (d * 218 >> 8)); // fainter further away (half at the far end)
            for (uint8_t k = 0; k < 3; k++) {
                if (g->r[k] < 0) continue;
                uint16_t off = d > g->r[k] ? d - g->r[k] : g->r[k] - d;
                if (off >= 10) continue;
                uint8_t a = fx_div255((uint32_t)fx_smooth8((uint8_t)(255 - off * 25)) * fall);
                if (a > o.a) o.a = a, o.h = k & 1 ? 40 : 20; // orange, yellow, orange
            }
            break;
        }
        case FXP_PLAY: {
            int16_t  dx = (int16_t)x - g->bx, dy = ly5 - g->by5;
            uint16_t ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
            if (ax < 19 && ay < 19) {
                uint16_t d = fx_isqrt((uint32_t)ax * ax + (uint32_t)ay * ay);
                if (d < 18) o.a = fx_smooth8((uint8_t)(255 - d * 14)), o.h = g->hue;
            }
            if (!o.a && ax < 60) {
                uint32_t hs = fx_hash32(((uint32_t)x << 8 | (uint32_t)ly5) ^ (uint32_t)g->r[0] * 2654435761u);
                if (!(hs & 15)) o.a = (uint8_t)(140 + (hs >> 8 & 63)), o.h = (uint8_t)(hs >> 16), o.s = 200;
            }
            break;
        }
        case FXP_HAPPY:
        case FXP_GROW: {
            int16_t  dd = (int16_t)x - g->sx;
            uint16_t ad = dd < 0 ? -dd : dd;
            if (ad < 40) o.a = fx_smooth8((uint8_t)(255 - ad * 6)), o.h = (uint8_t)(x + (g->t >> 2));
            break;
        }
        case FXP_SAD:
            if (under) o.a = (uint8_t)(g->b * 170 >> 8), o.h = 160;
            break;
        case FXP_SLEEP:
            o.a = (uint8_t)(g->b * 3 >> 2), o.h = 180, o.s = 220, o.v = 64;
            break;
        case FXP_HATCH: {
            int16_t  dx = (int16_t)x - FXP_OLED_X, dy = ly5 - FXP_OLED_Y5;
            uint16_t d  = fx_isqrt((uint32_t)(dx * dx) + (uint32_t)(dy * dy));
            uint16_t off = d > g->r[0] ? d - g->r[0] : g->r[0] - d;
            o.h          = 36; // gold
            if (off < 8) o.a = fx_smooth8((uint8_t)(255 - off * 32)), o.s = 200;
            for (uint8_t k = 0; k < 8; k++) {
                int32_t along = ((int32_t)dx * fx_pet_dir[k][0] + (int32_t)dy * fx_pet_dir[k][1]) >> 6;
                if (along < 0 || along > g->r[1]) continue;
                int32_t  c    = (int32_t)dx * fx_pet_dir[k][1] - (int32_t)dy * fx_pet_dir[k][0];
                uint16_t side = (uint16_t)((c < 0 ? -c : c) >> 6);
                if (side >= 6) continue;
                uint8_t a = fx_smooth8((uint8_t)(255 - side * 42));
                if (a > o.a) o.a = a, o.s = 110;
            }
            if (g->b > o.a) o.a = g->b, o.s = 0; // the first flash: white
            break;
        }
        default:
            break;
    }
    return o;
}

// The strength setting (1-100 %) as a factor for fx_pet_alpha: even a weak
// reaction covers at least ~a quarter of what's under it, so its colour reads
// instead of mixing into grey with the background.
static inline uint16_t fx_pet_k(uint8_t level) {
    return (uint16_t)(64 + (uint16_t)(level > 100 ? 100 : level) * 192 / 100);
}

// Its final strength at an LED: env and the strength setting (k = fx_pet_k).
static inline uint8_t fx_pet_alpha(const fx_pet_t *g, uint8_t a, uint16_t k) {
    return (uint8_t)((uint32_t)fx_div255((uint32_t)a * g->env) * k >> 8);
}
