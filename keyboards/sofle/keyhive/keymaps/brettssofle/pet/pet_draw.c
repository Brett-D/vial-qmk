// Copyright 2026 Brett-D
// SPDX-License-Identifier: GPL-2.0-or-later
//
// The pet page of the right OLED: tall and narrow (portrait, 32 x 128 px,
// 5 characters x 16 lines of text). Top to bottom:
//
//   y   0-  7  name (first 5 letters)                         text line 0
//   y   9- 16  mood icon (hidden while the pet or an effect is up there)
//   y   9- 56  the pet's play area: it wanders the meadow (pet_motion.h);
//              ground pets stay below the mood icon, flyers use all of it;
//              poop at the front right when dirty
//   y  57      ...........
//   y  60- 87  food / fun / energy / clean: tiny icon + bar
//   y  90      ...........
//   y  92-109  the selected care action as a big icon (boxed, with arrows,
//              while choosing)
//   y 112-119  its name: Feed / Play / Sleep / Wake / Clean / New   line 14
//   y 122-124  which of the four is selected
//
// Drawn into a local frame buffer every 100 ms; only pixels that changed go
// to the OLED (the driver sends dirty blocks), so the motion is cheap on I2C.

#include "brettssofle.h"
#include "pet.h"
#include "pet_art.h"
#include "pet_motion.h"

#define STAT_Y 60
#define ACT_Y 92
#define ANIM_MS 2000
#define PLAY_MS 3000
#define FRAME_MS 100
#define MAX_STEP 4 // px per frame when the spot jumps (falling asleep, after the menu)

// Typing speed (words per minute) as this half sees it; led_fx_draw.c has the real one.
__attribute__((weak)) uint8_t pet_typing_wpm(void) {
    return 0;
}

static uint8_t fb[32 * 16]; // OLED layout: byte x + 32 * (y / 8), bit y % 8

static void px(int16_t x, int16_t y, bool on) {
    if (x < 0 || x >= 32 || y < 0 || y >= 128) {
        return;
    }
    uint8_t *b = &fb[x + 32 * (y >> 3)], m = (uint8_t)(1 << (y & 7));
    *b = on ? *b | m : *b & ~m;
}

// A pre-rendered frame (pet_art.h): left x, bottom row y, mirrored or not,
// sy/16 as tall (stretching, sitting: pet_motion.h).
static void sprite(uint16_t idx, int16_t x, int16_t bottom, bool mirror, uint8_t sy) {
    const pet_frame_t *f   = &pet_frames[idx];
    uint8_t            out = petm_rows(f->h, sy);
    int16_t            y   = bottom + 1 - out;
    for (uint8_t r = 0; r < out; r++) {
        uint32_t row = pet_rows[f->row + petm_row(r, f->h, out)];
        for (uint8_t c = 0; c < f->w; c++) {
            if (row & (1ul << (f->w - 1 - c))) {
                px(x + (mirror ? f->w - 1 - c : c), y + r, true);
            }
        }
    }
}

static void unpaint(int16_t x0, int16_t y0, int16_t x1, int16_t y1) {
    for (int16_t y = y0; y <= y1; y++) {
        for (int16_t x = x0; x <= x1; x++) {
            px(x, y, false);
        }
    }
}

// 8x8 icon; cols = which of its columns (bit 7 = leftmost), e.g. a bitten apple.
static void icon_cols(uint8_t i, int16_t x, int16_t y, uint8_t scale, uint8_t cols) {
    for (uint8_t r = 0; r < 8 * scale; r++) {
        uint8_t row = pet_icons[i][r / scale] & cols;
        for (uint8_t c = 0; c < 8 * scale; c++) {
            if (row & (0x80 >> (c / scale))) {
                px(x + c, y + r, true);
            }
        }
    }
}

static void icon(uint8_t i, int16_t x, int16_t y, uint8_t scale) {
    icon_cols(i, x, y, scale, 0xFF);
}

static void box(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1, bool fill) {
    for (uint8_t y = y0; y <= y1; y++) {
        for (uint8_t x = x0; x <= x1; x++) {
            if (fill || y == y0 || y == y1 || x == x0 || x == x1) {
                px(x, y, true);
            }
        }
    }
}

static void dotted(uint8_t y) {
    for (uint8_t x = 1; x < 32; x += 3) {
        px(x, y, true);
    }
}

// Text on one 5-character line, centred (the OLED's own font; only lines 0 and 14).
static void centred(uint8_t line, const char *s, uint8_t len, bool invert) {
    char buf[6] = "     ";
    if (len > 5) len = 5;
    memcpy(buf + (5 - len) / 2, s, len);
    oled_set_cursor(0, line);
    oled_write(buf, invert);
}

static void stats(const pet_view_t *v) {
    const uint8_t *s = &v->food;
    for (uint8_t i = 0; i < 4; i++) {
        uint8_t y = STAT_Y + i * 7;
        for (uint8_t r = 0; r < 5; r++) {
            for (uint8_t c = 0; c < 5; c++) {
                if (pet_stat_icons[i][r] & (0x10 >> c)) {
                    px(c, y + r, true);
                }
            }
        }
        box(7, y, 31, y + 4, false);
        uint8_t w = (uint8_t)((s[i] * 23 + 50) / 100);
        if (w) {
            box(8, y + 1, 7 + w, y + 3, true);
        }
    }
}

static uint8_t mood_icon(uint8_t mood) {
    switch (mood) {
        case MOOD_HAPPY:
            return PET_ICON_HEART;
        case MOOD_HUNGRY:
            return PET_ICON_APPLE;
        case MOOD_SAD:
            return PET_ICON_TEAR;
        case MOOD_TIRED:
            return PET_ICON_BOLT;
        case MOOD_DIRTY:
            return PET_ICON_POOP;
        case MOOD_ASLEEP:
            return PET_ICON_MOON;
        case MOOD_DEAD:
            return PET_ICON_NO;
        case MOOD_EGG:
            return PET_ICON_EGG;
        default:
            return PET_ICON_SMILE;
    }
}

// The selected care action: big icon, label, position dots.
static void action(const pet_view_t *v) {
    static const char *const labels[] = {"", "Feed", "Play", "Sleep", "Clean", "New", "Next"};
    bool    asleep = v->flags & PV_ASLEEP, choosing = v->flags & PV_MENU;
    uint8_t a      = v->sel < PET_ACTION_COUNT ? v->sel : PET_FEED;
    uint8_t sp     = v->species < PET_SPECIES_COUNT ? v->species : 0;
    uint8_t i      = a == PET_FEED ? pet_food_icon[sp] : a == PET_PLAY ? pet_play_icon[sp] : a == PET_SLEEP ? (asleep ? PET_ICON_SPARKLE : PET_ICON_MOON) : a == PET_CLEAN ? PET_ICON_SPARKLE : a == PET_NEXT ? PET_ICON_NEXT : PET_ICON_EGG;
    const char *label = a == PET_SLEEP && asleep ? "Wake" : labels[a];
    icon(i, 8, ACT_Y + 2, 2);
    if (choosing) {
        // Boxed, with arrows: turn the knob to change it, tap to do it.
        box(5, ACT_Y, 26, ACT_Y + 19, false);
        for (uint8_t d = 0; d < 3; d++) {
            for (int8_t h = -d; h <= d; h++) {
                px(1 + d, ACT_Y + 10 + h, true);  // <
                px(30 - d, ACT_Y + 10 + h, true); // >
            }
        }
    }
    centred(14, label, strlen(label), false);
    if (!choosing) {
        return; // "Next": a tap goes to the next page
    }
    // Which menu item: the same lists as pet.c.
    static const uint8_t alive[] = {PET_FEED, PET_PLAY, PET_SLEEP, PET_CLEAN, PET_NEXT};
    static const uint8_t ghost[] = {PET_NEW, PET_NEXT};
    const uint8_t       *items   = v->flags & PV_DEAD ? ghost : alive;
    uint8_t              n       = v->flags & PV_DEAD ? 2 : v->stage == PET_STAGE_EGG ? 1 : 5;
    if (n < 2) {
        return;
    }
    uint8_t x0 = (32 - (n * 6 - 3)) / 2;
    for (uint8_t k = 0; k < n; k++) {
        uint8_t x = x0 + k * 6;
        if (n == 1 || items[k] == a) {
            box(x, 122, x + 2, 124, true);
        } else {
            px(x + 1, 123, true);
        }
    }
}

// Only the pixels that changed reach the OLED (text lines 0 and 14 are the OLED's own).
static void flush(void) {
    for (uint8_t y = 8; y < 128; y++) {
        if (y >= 112 && y < 120) {
            continue;
        }
        for (uint8_t x = 0; x < 32; x++) {
            oled_write_pixel(x, y, fb[x + 32 * (y >> 3)] & (1 << (y & 7)));
        }
    }
}

static int16_t clampi(int16_t v, int16_t lo, int16_t hi) {
    return v < lo ? lo : v > hi ? hi : v;
}

static uint16_t tri(uint32_t t, uint16_t period, uint16_t span) { // 0 -> span -> 0
    uint32_t p = t % period, h = period / 2;
    return (uint16_t)((p < h ? p : period - p) * span / h);
}

uint16_t pet_seed(uint8_t species, const char *name) { // the same as pet_core.js seedOf
    uint16_t s = (uint16_t)(species * 40503u + 1);
    for (uint8_t i = 0; i < 5 && name[i]; i++) s = (uint16_t)(s * 31u + (uint8_t)name[i]);
    return s;
}

void pet_draw(const pet_view_t *v, bool force) {
    static pet_view_t last;
    static uint32_t   anim_t, last_frame = 0xFFFFFFFF, last_ms, mt4;
    static uint8_t    anim_type, cur_floor;
    static int8_t     cur_x;
    static bool       started, placed;
    uint32_t          now = timer_read32();
    if (!started || v->anim != last.anim) {
        anim_type = started ? v->anim & 15 : ANIM_NONE; // no replay after a restart
        anim_t    = now;
        started   = true;
    }
    uint16_t anim_ms   = anim_type == ANIM_GROW || anim_type == ANIM_HATCH ? 3000 : anim_type == ANIM_PLAY ? PLAY_MS : ANIM_MS;
    uint32_t el        = timer_elapsed32(anim_t);
    bool     animating = anim_type != ANIM_NONE && el < anim_ms;
    uint32_t frame     = (now / FRAME_MS) | (animating ? 0x80000000u : 0);
    if (!force && frame == last_frame && memcmp(v, &last, sizeof(last)) == 0) {
        return;
    }
    last_frame = frame;
    last       = *v;
    if (force) {
        oled_clear(); // coming from another page
    }
    memset(fb, 0, sizeof(fb));
    if (!(v->flags & PV_ON)) {
        centred(0, "", 0, false);
        centred(14, "", 0, false);
        flush();
        return;
    }
    uint8_t ph = (uint8_t)(now / 250);
    uint8_t sp = v->species < PET_SPECIES_COUNT ? v->species : 0;
    uint8_t st = v->stage < PET_STAGE_COUNT ? v->stage : PET_STAGE_ADULT;
    bool    asleep = v->flags & PV_ASLEEP, dead = v->flags & PV_DEAD, egg = st == PET_STAGE_EGG;
    bool    poop   = !dead && !egg && v->clean < 40;
    uint8_t name_len = 0;
    while (name_len < 5 && v->name[name_len]) {
        name_len++;
    }
    centred(0, v->name, name_len, false);

    // Where it is and what it does (pet_motion.h).
    uint8_t     wpm  = pet_typing_wpm();
    bool        sad = v->food < 20 || v->joy < 20, tired = v->energy < 20;
    uint8_t     mood = (v->mood == MOOD_HAPPY ? PETM_HAPPY : 0) | (sad ? PETM_SAD : 0) | (tired ? PETM_TIRED : 0) | (asleep ? PETM_ASLEEP : 0) | (!sad && !tired && wpm >= 40 ? PETM_EXCITED : 0);
    uint8_t     speed = sad || tired ? 2 : wpm >= 80 ? 8 : wpm >= 40 ? 6 : st == PET_STAGE_BABY ? 3 : 4; // quarters
    bool        frozen = egg || dead || asleep || animating || (v->flags & PV_MENU);
    uint32_t    dt     = last_ms ? timer_elapsed32(last_ms) : 0;
    last_ms            = now;
    if (!frozen) mt4 += (dt > 250 ? 250 : dt) * speed;
    pet_pose_t  p = {0};
    petm_area_t area;
    static const uint8_t egg_box[2] = {16, 18};
    const uint8_t       *bx = egg ? egg_box : pet_box[sp][st - 1];
    petm_area(&area, bx[0], bx[1], !egg && (pet_flyer[sp] >> (st - 1)) & 1, st);
    if (egg) {
        p.x = (32 - 16) / 2, p.floor = PETM_FLOOR;
    } else {
        petm_pose(&p, &area, sp, st, pet_seed(sp, v->name), mt4 / 4, mood);
    }

    // Care animations: it stays where it is (the clock is frozen) and the effect comes to it.
    int16_t want_x = p.x;
    bool    mir    = p.flags & PETP_MIRROR;
    uint8_t f      = p.frame, sy = p.sy;
    int16_t dx = p.dx, dy = p.dy;
    if (dead) {
        f  = PET_FR_DEAD;
        dy = ph % 4 < 2 ? -1 : 1; // floats
    }
    uint8_t toy = pet_play_icon[sp];
    if (animating && anim_type == ANIM_PLAY && (toy == PET_ICON_BALL || toy == PET_ICON_YARN)) {
        uint16_t tx = tri(el, toy == PET_ICON_BALL ? 1500 : 2000, 24);
        want_x      = clampi((int16_t)tx + 4 - bx[0] / 2, 0, area.xr); // chases it
        mir         = (el % (toy == PET_ICON_BALL ? 1500 : 2000)) < (toy == PET_ICON_BALL ? 750u : 1000u);
    }
    if (!placed || force) {
        cur_x = (int8_t)want_x, cur_floor = p.floor, placed = true;
    } else {
        cur_x     = (int8_t)(cur_x + clampi(want_x - cur_x, -MAX_STEP, MAX_STEP));
        cur_floor = (uint8_t)(cur_floor + clampi((int16_t)p.floor - cur_floor, -MAX_STEP, MAX_STEP));
    }

    bool show_egg = egg;
    if (animating) {
        f = asleep ? PET_FR_SLEEP : dead ? PET_FR_DEAD : PET_FR_HAPPY;
        dx = 0, dy = 0, sy = 16;
        switch (anim_type) {
            case ANIM_EAT:
                f = (el / 200) & 1 ? PET_FR_HAPPY : PET_FR_EAT; // chomp, chew
                break;
            case ANIM_PLAY:
                if (toy == PET_ICON_BALL) dy = (int16_t)-petm_arc(el % 400, 400, 3);
                if (toy == PET_ICON_YARN) {
                    dy = (int16_t)-petm_arc(el % 600, 600, 5); // pounces
                    sy = (el / 300) & 1 ? 18 : 16; // stretching for it
                }
                if (toy == PET_ICON_FEATHER) f = (el / 120) & 1 ? PET_FR_FLY2 : PET_FR_FLY1;
                break;
            case ANIM_NO:
                if (!asleep && !dead && !egg) {
                    f  = PET_FR_SAD;
                    dx = ph & 1 ? 1 : -1; // shakes its head
                }
                break;
            case ANIM_HATCH: // the egg wobbles, then out it comes
                if (el < 1800) {
                    show_egg = true;
                    dx       = ph & 1 ? 1 : -1;
                }
                break;
            default:
                break;
        }
    }
    if (egg && !animating && ph % 12 < 2) {
        dx = ph & 1 ? 1 : -1; // something moves inside
    }

    // The pet.
    uint16_t fi  = show_egg ? pet_egg_index[sp] : pet_frame_index[sp][st - 1][f];
    uint8_t  fw  = pet_frames[fi].w, fh = petm_rows(pet_frames[fi].h, show_egg ? 16 : sy);
    int16_t  left = show_egg ? (32 - fw) / 2 + dx : cur_x + (bx[0] - fw) / 2 + dx;
    int16_t  bot  = (show_egg ? PETM_FLOOR : cur_floor) + dy;
    int16_t  top  = bot + 1 - fh, cx = left + fw / 2;
    bool     food_above = false;
    if (animating && anim_type == ANIM_EAT && !show_egg) { // turn to where the food fits; no room either side: above
        bool room_l = left >= 8, room_r = left + fw <= 24;
        if (mir ? !room_r : !room_l) {
            if (mir ? room_l : room_r) mir = !mir;
            else food_above = true;
        }
    }
    bool     grew = animating && anim_type == ANIM_GROW;
    if (!animating && top >= 17) {
        icon(grew ? PET_ICON_STAR : mood_icon(v->mood), 12, 9, 1);
    }
    sprite(fi, left, bot, mir && !show_egg, show_egg ? 16 : sy);

    // What it's doing, around it.
    int16_t fy = clampi(top - 9, 9, 48), side = mir ? left + fw : left - 8; // above it / in front of its face (mir as drawn)
    if (animating) {
        switch (anim_type) {
            case ANIM_EAT: { // bite by bite
                // The mouth side goes first: facing left, the food is on its left and loses its right columns.
                uint8_t b = (uint8_t)(el * 9 / ANIM_MS), keep = b >= 8 ? 0 : mir ? (uint8_t)(0xFF >> b) : (uint8_t)(0xFF << b);
                int16_t ix = food_above ? cx - 4 : side, iy = food_above ? clampi(top - 9, 9, 48) : clampi(top + fh / 3, 9, 48);
                if (food_above) keep = b >= 8 ? 0 : (uint8_t)(0xFF << b);
                unpaint(ix, iy, ix + 7, iy + 7);
                icon_cols(pet_food_icon[sp], ix, iy, 1, keep);
                break;
            }
            case ANIM_PLAY:
                if (toy == PET_ICON_BALL || toy == PET_ICON_YARN) {
                    int16_t tx = tri(el, toy == PET_ICON_BALL ? 1500 : 2000, 24);
                    int16_t ty = toy == PET_ICON_BALL ? 47 - petm_arc(el % 500, 500, 14) : 48;
                    unpaint(tx, ty, tx + 7, ty + 7);
                    icon(toy, tx, ty, 1);
                } else if (toy == PET_ICON_FEATHER) { // floats down, side to side
                    icon(toy, 4 + tri(el, 1200, 18), 9 + (int16_t)(el * 36 / PLAY_MS), 1);
                } else if (toy == PET_ICON_FLAME) { // puffs of fire, rising
                    icon(toy, cx - 4 + ((el / 200) & 1 ? 3 : -3), clampi(fy - (int16_t)((el / 100) % 4) * 2, 9, 48), 1);
                } else { // bubbles rise
                    for (uint8_t k = 0; k < 2; k++) {
                        uint32_t q = (el + k * 500) % 1000;
                        icon(toy, cx - 4 + (k ? 7 : -7) + ((q / 250) & 1), clampi(top - 6 - (int16_t)(q * 20 / 1000), 9, 50), 1);
                    }
                }
                break;
            case ANIM_CLEAN: { // a sparkle sweeps over it, head then body
                int16_t sx = -8 + (int16_t)((el % 1000) * 40 / 1000), sy = el < 1000 ? top : top + fh / 2;
                icon(PET_ICON_SPARKLE, sx, sy, 1);
                icon(PET_ICON_SPARKLE, sx - 14, sy + 6, 1);
                break;
            }
            case ANIM_WAKE:
                icon(PET_ICON_SPARKLE, ph & 1 ? 0 : 24, fy, 1);
                icon(PET_ICON_SPARKLE, ph & 1 ? 24 : 0, fy + 10, 1);
                break;
            case ANIM_NO:
                icon(PET_ICON_NO, cx - 4, fy, 1);
                break;
            case ANIM_HATCH:
                if (el >= 1800) icon(PET_ICON_SPARKLE, ph & 1 ? 0 : 24, fy, 1);
                break;
            case ANIM_GROW: // grew up a stage (or a new egg arrived)
                icon(PET_ICON_STAR, ph & 1 ? 0 : 24, ph & 1 ? fy : fy + 6, 1);
                icon(PET_ICON_SPARKLE, ph & 1 ? 24 : 0, ph & 1 ? fy + 8 : fy, 1);
                break;
            default:
                break;
        }
    } else if (!dead && !egg) {
        switch (p.fx) {
            case PETFX_SNEEZE: // achoo
                for (uint8_t k = 0; k < 3; k++) px(mir ? left + fw + 1 + k * 2 : left - 2 - k * 2, top + fh / 2 - 1 + (k & 1) * 2, true);
                break;
            case PETFX_EXCLAIM:
                for (int16_t y = fy + 1; y < fy + 6; y++) px(cx, y, true);
                px(cx, fy + 7, true);
                break;
            case PETFX_STAR:
                icon(PET_ICON_STAR, cx - 4, fy, 1);
                break;
            case PETFX_FLAME: // fire! rising from its mouth (a dragon is too wide for fire beside it)
                icon(PET_ICON_FLAME, cx - 4, fy, 1);
                icon(PET_ICON_FLAME, cx - 4 + ((now / 150) & 1 ? 5 : -5), clampi(fy - 7, 9, 48), 1);
                break;
            default:
                break;
        }
        if (sp == PET_SP_DRAGON && f == PET_FR_HAPPY && p.fx == PETFX_NONE && ph & 1) {
            icon(PET_ICON_FLAME, cx - 4, fy, 1); // a happy little puff of flame
        }
    }
    if (asleep && !(animating && anim_type == ANIM_NO)) {
        icon(PET_ICON_Z, clampi(left + fw - 4, 0, 24), clampi(top - 6 - (ph % 3) * 3, 9, 48), 1); // zzz floating up
    }
    if (poop) {
        unpaint(23, 47, 31, 56);
        icon(PET_ICON_POOP, 24, 48, 1);
    }

    // Below: stats, then the care action.
    dotted(57);
    stats(v);
    dotted(90);
    action(v);
    flush();
}
