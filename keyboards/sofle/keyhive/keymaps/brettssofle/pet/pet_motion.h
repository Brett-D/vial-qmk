// Copyright 2026 Brett-D
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Where the pet is and what it does on its page, from a "motion clock" and a
// seed (species + name): a pure function, so the right half needs no extra
// sync and the config page (page/pet_core.js, the same integer maths) shows
// the same motion. pet_draw.c runs the clock: it stops while the menu is open,
// during care animations and in sleep, and runs faster while you type fast.
//
// Time is cut into 2 s segments. Every segment is a move (to a new spot,
// picked by hashing the seed and the segment number) or a rest (an idle
// action: look around, yawn, stretch, sit, scratch, sneeze, now and then a
// surprise). Every third segment is a move, so a rest never lasts long.
// Ground pets wander a little meadow (left/right, and up the screen = further
// away); owls and the adult dragon fly anywhere in the band, hover and perch.

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "pet_ids.h"

#define PETM_FLOOR 55      // lowest floor (the front of the meadow)
#define PETM_FLOOR_BIG 78  // v26 big pet, menu closed: the stats are slim, the meadow goes down to here
#define PETM_TOP 18        // ground pets stay below the mood icon
#define PETM_AIR_TOP 9     // flyers may cover it (it hides)
#define PETM_SEG_MS 2000   // one move or rest
#define PETM_TRAVEL_MS 1400 // a move's travel; then it stands

enum { PETM_HAPPY = 1, PETM_SAD = 2, PETM_TIRED = 4, PETM_EXCITED = 8, PETM_ASLEEP = 16 }; // mood input
enum { PETP_MIRROR = 1, PETP_AIR = 2, PETP_MOVING = 4 };                                    // pose flags
enum { PETFX_NONE, PETFX_SNEEZE, PETFX_EXCLAIM, PETFX_STAR, PETFX_FLAME };                  // little extras

typedef struct {
    int8_t  x;     // left of the species' box (pet_box)
    int8_t  dx;    // jitter (scratching)
    int8_t  dy;    // hop / bob / droop
    uint8_t floor; // the box's bottom row, before dy
    uint8_t frame; // PET_FR_*
    uint8_t flags; // PETP_*
    uint8_t fx;    // PETFX_*
    uint8_t sy;    // height in 16ths: 18 = stretching up, 14 = sitting / crouching (petm_row)
} pet_pose_t;

// Drawing a frame sy/16 as tall: output row r of rows_out shows source row
// petm_row(r) (the feet stay the feet).
static inline uint8_t petm_rows(uint8_t h, uint8_t sy) {
    return (uint8_t)((h * sy + 8) / 16);
}
static inline uint8_t petm_row(uint8_t r, uint8_t h, uint8_t out) {
    return (uint8_t)((uint16_t)r * h / out);
}

static inline uint32_t petm_hash(uint32_t a) {
    a ^= a >> 16;
    a *= 0x7FEB352Du;
    a ^= a >> 15;
    a *= 0x846CA68Bu;
    a ^= a >> 16;
    return a;
}

static inline uint32_t petm_seg(uint16_t seed, uint32_t k) {
    return petm_hash(((uint32_t)seed * 0x10001u) ^ (k * 0x9E3779B1u));
}

// A hop: 0 at both ends, h in the middle.
static inline int8_t petm_arc(uint32_t p, uint32_t len, uint8_t h) {
    return p >= len ? 0 : (int8_t)(4u * h * p * (len - p) / (len * len));
}

typedef struct {
    uint8_t bw, bh, fly, xr, gmin, amin, fl; // box, can fly, x range, floor ranges (ground, air), the lowest floor
} petm_area_t;

// fl: the lowest floor, PETM_FLOOR or (big pet, menu closed) PETM_FLOOR_BIG.
static inline void petm_area(petm_area_t *a, uint8_t bw, uint8_t bh, bool fly, uint8_t stage, uint8_t fl) {
    static const uint8_t depth[3] = {12, 10, 8}, air[3] = {14, 46, 46}; // babies stay low
    uint8_t s = stage >= 1 && stage <= 3 ? stage - 1 : 2;
    a->bw = bw, a->bh = bh, a->fly = fly, a->fl = fl;
    a->xr       = bw < 32 ? 32 - bw : 0;
    int16_t g   = PETM_TOP - 1 + bh, ga = PETM_FLOOR - depth[s]; // big pet: the whole extra room is meadow too
    a->gmin     = (uint8_t)(g > ga ? (g > fl ? fl : g) : ga);
    int16_t m   = PETM_AIR_TOP - 1 + bh, ma = fl - air[s];
    a->amin     = (uint8_t)(m > ma ? (m > fl ? fl : m) : ma);
}

static inline bool petm_moves(uint16_t seed, uint32_t k, uint8_t stage) {
    static const uint8_t chance[3] = {3, 4, 5}; // of 8: babies rest more
    uint8_t s = stage >= 1 && stage <= 3 ? stage - 1 : 2;
    return k % 3 == 0 || (petm_seg(seed, k) & 7) < chance[s];
}

static inline uint32_t petm_last_move(uint16_t seed, uint32_t k, uint8_t stage) {
    while (!petm_moves(seed, k, stage)) k--; // at most 2 steps (every third segment moves)
    return k;
}

// The spot a move segment goes to.
static inline void petm_point(const petm_area_t *a, uint16_t seed, uint32_t k, int8_t *x, uint8_t *fl) {
    uint32_t r = petm_seg(seed, k);
    *x         = (int8_t)(((r >> 8) & 0xFF) % (a->xr + 1u));
    uint8_t lo = a->fly && ((r >> 24) & 3) != 0 ? a->amin : a->gmin; // flyers: in the air 3 times in 4
    *fl        = (uint8_t)(lo + ((r >> 16) & 0xFF) % (a->fl - lo + 1u));
}

static inline void petm_pose(pet_pose_t *o, const petm_area_t *a, uint8_t species, uint8_t stage, uint16_t seed, uint32_t mt, uint8_t mood) {
    uint32_t k = mt / PETM_SEG_MS, u = mt % PETM_SEG_MS;
    uint32_t j = petm_last_move(seed, k, stage), rj = petm_seg(seed, j), r = petm_seg(seed, k);
    int8_t   ex, sx;
    uint8_t  ef, sf;
    petm_point(a, seed, j, &ex, &ef);
    if (j > 0) {
        petm_point(a, seed, petm_last_move(seed, j - 1, stage), &sx, &sf);
    } else {
        sx = ex, sf = ef;
    }
    int8_t  ddx = (int8_t)(ex - sx);
    bool    mir = ddx > 0 || (ddx == 0 && ((rj >> 26) & 1)); // the art faces left
    bool    moving = j == k && u < PETM_TRAVEL_MS && (ddx != 0 || ef != sf) && !(mood & PETM_ASLEEP);
    int32_t e      = 256;
    if (j == k && u < PETM_TRAVEL_MS) {
        int32_t f = (int32_t)(u * 256 / PETM_TRAVEL_MS);
        e         = (f * f * (768 - 2 * f)) >> 16; // smoothstep
    }
    o->x     = (int8_t)(sx + ((ddx * e) >> 8));
    o->floor = (uint8_t)(sf + (((int32_t)ef - sf) * e >> 8));
    o->dx = 0, o->dy = 0, o->fx = PETFX_NONE, o->sy = 16;
    o->flags = mir ? PETP_MIRROR : 0;
    uint8_t big = stage >= 3 ? 2 : stage == 2 ? 1 : 0;

    if (mood & PETM_ASLEEP) { // asleep: where it was, on the ground
        if (o->floor < a->gmin) o->floor = a->gmin;
        o->frame = PET_FR_SLEEP;
        return;
    }
    if (moving) {
        o->flags |= PETP_MOVING;
        if (a->fly) { // flapping; a short hop between two spots on the ground
            o->flags |= PETP_AIR;
            o->frame = (u / 120) & 1 ? PET_FR_FLY2 : PET_FR_FLY1;
            o->dy    = (int8_t)(((u / 240) & 1) - (sf >= a->gmin && ef >= a->gmin ? petm_arc(u, PETM_TRAVEL_MS, 6) : 0));
            return;
        }
        switch (species) {
            case PET_SP_PIP: { // hops
                int8_t l = petm_arc(u % 350, 350, (uint8_t)(3 + big));
                o->dy    = (int8_t)-l;
                o->frame = l > 1 ? PET_FR_IDLE2 : PET_FR_WALK;
                break;
            }
            case PET_SP_BLOB: { // squish, then a bounce
                uint32_t p = u % 400;
                o->frame   = p < 150 ? PET_FR_WALK : PET_FR_IDLE;
                o->dy      = p < 150 ? 0 : (int8_t)-petm_arc(p - 150, 250, 4);
                break;
            }
            case PET_SP_AXOLOTL: // swims: gills and tail wiggle, drifting
                o->frame = (u / 200) & 1 ? PET_FR_WALK : PET_FR_IDLE;
                o->dy    = (u / 300) & 1 ? -1 : 0;
                break;
            case PET_SP_DOG: // trots, bouncing, tail wagging
                o->frame = (u / 150) & 1 ? PET_FR_WALK : PET_FR_IDLE;
                o->dy    = (u / 150) & 1 ? -1 : 0;
                break;
            case PET_SP_CAT: // walks; sometimes the end of it is a pounce
                o->frame = (u / 150) & 1 ? PET_FR_WALK : PET_FR_IDLE;
                if (((rj >> 27) & 1) && u + 450 >= PETM_TRAVEL_MS) {
                    o->frame = PET_FR_HAPPY;
                    o->sy    = 18;
                    o->dy    = (int8_t)-petm_arc(u + 450 - PETM_TRAVEL_MS, 450, 6);
                }
                break;
            default:
                o->frame = (u / 150) & 1 ? PET_FR_WALK : PET_FR_IDLE;
                break;
        }
        return;
    }

    // Standing (or hovering): a rest segment, or the end of a move.
    uint32_t t   = u;
    bool     air = a->fly && o->floor < a->gmin;
    if (air) { // hovering: it can still look around, and surprise you up there
        o->flags |= PETP_AIR;
        o->frame = (t / 220) & 1 ? PET_FR_FLY2 : PET_FR_FLY1;
        o->dy    = (t / 440) & 1 ? 1 : 0;
        uint8_t act = (r >> 3) & 31;
        if (j == k || (mood & (PETM_SAD | PETM_TIRED | PETM_EXCITED))) return;
        if (act >= 10 && act <= 12 && ((t / 700) & 1)) o->flags ^= PETP_MIRROR;
        if (act == 31 && ((r >> 28) & 1) == 0 && t >= 300 && t < 1500) {
            if (species == PET_SP_DRAGON) {
                o->fx = PETFX_FLAME;
            } else {
                if ((t / 100) & 1) o->flags ^= PETP_MIRROR; // a spin in the air
                o->fx = PETFX_STAR;
            }
        }
        return;
    }
    uint8_t ph = (uint8_t)(t / 250);
    o->frame   = (ph >> 1) & 1 ? PET_FR_IDLE2 : PET_FR_IDLE;
    if (mood & PETM_SAD) { // droops
        o->frame = PET_FR_SAD;
        o->dy    = 1;
        return;
    }
    if (mood & PETM_TIRED) { // nodding off
        o->frame = (t / 500) & 1 ? PET_FR_BLINK : PET_FR_TIRED;
        o->dy    = (t / 500) & 1 ? 1 : 0;
        return;
    }
    if (mood & PETM_EXCITED) { // you're typing fast: bouncing with joy
        o->frame = PET_FR_HAPPY;
        o->dy    = (int8_t)-petm_arc(t % 300, 300, 3);
        return;
    }
    if ((mood & PETM_HAPPY) && ph % 16 >= 12) o->frame = PET_FR_HAPPY;
    if (ph % 16 == 5) o->frame = PET_FR_BLINK;
    if (j == k) return; // just arrived
    uint8_t act = (r >> 3) & 31;
    if (act >= 10 && act <= 12) { // looks around
        o->frame = PET_FR_LOOK;
        if ((t / 700) & 1) o->flags ^= PETP_MIRROR;
    } else if (act <= 14 && act >= 13) {
        if (t >= 400 && t < 1300) o->frame = PET_FR_YAWN;
    } else if (act <= 16 && act >= 15) {
        if (t >= 300 && t < 1100) o->frame = PET_FR_HAPPY, o->sy = 18;
    } else if (act <= 19 && act >= 17) {
        if (t >= 200) o->sy = 14;
    } else if (act <= 21 && act >= 20) {
        if (t >= 200 && t < 1400) {
            o->frame = PET_FR_WALK;
            o->dx    = (int8_t)((t / 80) & 1);
        }
    } else if (act <= 23 && act >= 22) {
        if (t >= 400 && t < 700) o->frame = PET_FR_SNEEZE; // building up...
        if (t >= 700 && t < 950) {                         // ...achoo
            o->frame = PET_FR_SNEEZE;
            o->sy    = 14;
            o->fx    = PETFX_SNEEZE;
        }
    } else if (act == 31 && ((r >> 28) & 1) == 0) { // now and then, a surprise
        if (species == PET_SP_DRAGON) { // fire!
            if (t >= 300 && t < 1500) {
                o->frame = PET_FR_HAPPY;
                o->fx    = PETFX_FLAME;
            }
        } else if (species == PET_SP_OWL || species == PET_SP_CAT || species == PET_SP_DOG) { // a spin
            if (t >= 200 && t < 1200) {
                o->frame = PET_FR_HAPPY;
                if ((t / 100) & 1) o->flags ^= PETP_MIRROR;
                o->dy = (int8_t)-petm_arc(t - 200, 1000, 6);
                o->fx = PETFX_STAR;
            }
        } else if (t >= 200 && t < 1200) { // jumps: "!"
            o->frame = PET_FR_SURPRISE;
            o->dy    = (int8_t)-petm_arc(t > 300 ? t - 300 : 0, 600, 8);
            o->fx    = PETFX_EXCLAIM;
        }
    }
}
