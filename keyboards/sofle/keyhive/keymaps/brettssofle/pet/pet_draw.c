// Copyright 2026 Brett-D
// SPDX-License-Identifier: GPL-2.0-or-later
//
// The pet page of the right OLED: tall and narrow (portrait, 32 x 128 px,
// 5 characters x 16 lines of text). Only the pet; top to bottom:
//
//   y   0-  7  name (first 5 letters)                         text line 0
//   y   9- 16  mood icon: heart / smile / apple / tear / bolt / poop / moon
//   y  18- 27  effects: zzz, its food and toy, sparkles, stars, "no"
//   y  18- 54  the pet, standing on y 54: egg 16x18, baby 18x16, child 22x20,
//              adult 28x25 (winged dragon 32 wide); poop beside it when dirty
//   y  57      ...........
//   y  60- 87  food / fun / energy / clean: tiny icon + bar
//   y  90      ...........
//   y  92-109  the selected care action as a big icon (boxed, with arrows,
//              while choosing)
//   y 112-119  its name: Feed / Play / Sleep / Wake / Clean / New   line 14
//   y 122-124  which of the four is selected
//
// Redrawn only when something changes (about 4 times a second while idle).

#include "brettssofle.h"
#include "pet.h"
#include "pet_art.h"

#define FLOOR_Y 54 // the pet stands on this line
#define FX_Y 19    // effects band
#define STAT_Y 60
#define ACT_Y 92
#define ANIM_MS 2000

// A pre-rendered frame (pet_art.h), centred, standing on FLOOR_Y.
static void sprite(uint8_t idx, int8_t dx, int8_t dy) {
    const pet_frame_t *f = &pet_frames[idx];
    int8_t             x = (int8_t)((32 - f->w) / 2 + dx), y = (int8_t)(FLOOR_Y + 1 - f->h + dy);
    for (uint8_t r = 0; r < f->h; r++) {
        uint32_t row = pet_rows[f->row + r];
        for (uint8_t c = 0; c < f->w; c++) {
            if (row & (1ul << (f->w - 1 - c)) && x + c >= 0 && x + c < 32 && y + r >= 0) {
                oled_write_pixel(x + c, y + r, true);
            }
        }
    }
}

static void unpaint(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1) {
    for (uint8_t y = y0; y <= y1; y++) {
        for (uint8_t x = x0; x <= x1; x++) {
            oled_write_pixel(x, y, false);
        }
    }
}

static void icon(uint8_t i, int8_t x, int8_t y, uint8_t scale) {
    for (uint8_t r = 0; r < 8 * scale; r++) {
        uint8_t row = pet_icons[i][r / scale];
        for (uint8_t c = 0; c < 8 * scale; c++) {
            if (row & (0x80 >> (c / scale)) && x + c >= 0 && y + r >= 0) {
                oled_write_pixel(x + c, y + r, true);
            }
        }
    }
}

static void box(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1, bool fill) {
    for (uint8_t y = y0; y <= y1; y++) {
        for (uint8_t x = x0; x <= x1; x++) {
            if (fill || y == y0 || y == y1 || x == x0 || x == x1) {
                oled_write_pixel(x, y, true);
            }
        }
    }
}

static void dotted(uint8_t y) {
    for (uint8_t x = 1; x < 32; x += 3) {
        oled_write_pixel(x, y, true);
    }
}

// Text on one 5-character line, centred.
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
                    oled_write_pixel(c, y + r, true);
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
                oled_write_pixel(1 + d, ACT_Y + 10 + h, true);  // <
                oled_write_pixel(30 - d, ACT_Y + 10 + h, true); // >
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
            oled_write_pixel(x + 1, 123, true);
        }
    }
}

void pet_draw(const pet_view_t *v, bool force) {
    static pet_view_t last;
    static uint32_t   anim_t, last_frame = 0xFFFFFFFF;
    static uint8_t    anim_type;
    static bool       started;
    uint32_t          now = timer_read32();
    if (!started || v->anim != last.anim) {
        anim_type = started ? v->anim & 15 : ANIM_NONE; // no replay after a restart
        anim_t    = now;
        started   = true;
    }
    uint16_t anim_ms   = anim_type == ANIM_GROW || anim_type == ANIM_HATCH ? 3000 : ANIM_MS;
    bool     animating = anim_type != ANIM_NONE && timer_elapsed32(anim_t) < anim_ms;
    uint32_t frame     = (now / 250) | (animating ? 0x80000000u : 0);
    if (!force && frame == last_frame && memcmp(v, &last, sizeof(last)) == 0) {
        return;
    }
    last_frame = frame;
    last       = *v;
    oled_clear();
    if (!(v->flags & PV_ON)) {
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

    // Top: name and mood.
    centred(0, v->name, name_len, false);
    bool grew = animating && anim_type == ANIM_GROW;
    icon(grew ? PET_ICON_STAR : mood_icon(v->mood), 12, 9, 1);

    // Middle: the pet and what it's doing.
    int8_t  dx = 0, dy = 0;
    uint8_t f  = (ph / 2) & 1 ? PET_FR_IDLE2 : PET_FR_IDLE; // its idle animation
    if (dead) {
        f  = PET_FR_DEAD;
        dy = ph % 4 < 2 ? -1 : 1; // floats
    } else if (asleep) {
        f = PET_FR_SLEEP;
    } else if (v->food < 20 || v->joy < 20) {
        f = PET_FR_SAD;
    } else if (v->energy < 20) {
        f = PET_FR_TIRED;
    } else if (v->mood == MOOD_HAPPY && ph % 16 >= 12) {
        f = PET_FR_HAPPY;
    }
    if (!dead && !asleep && ph % 16 == 5) {
        f = PET_FR_BLINK;
    }
    bool show_egg = egg;
    if (animating) {
        switch (anim_type) {
            case ANIM_EAT:
                f = ph & 1 ? PET_FR_EAT : PET_FR_IDLE; // chewing
                if (timer_elapsed32(anim_t) < ANIM_MS / 2) {
                    icon(pet_food_icon[sp], 12, FX_Y, 1);
                }
                break;
            case ANIM_PLAY:
                f = PET_FR_HAPPY;
                icon(pet_play_icon[sp], (int8_t)((ph % 4) * 8), ph & 1 ? FX_Y - 1 : FX_Y + 1, 1);
                break;
            case ANIM_CLEAN:
            case ANIM_WAKE:
                icon(PET_ICON_SPARKLE, ph & 1 ? 0 : 24, FX_Y, 1);
                icon(PET_ICON_SPARKLE, ph & 1 ? 24 : 0, FX_Y + 14, 1);
                break;
            case ANIM_NO:
                if (!asleep && !dead && !egg) {
                    f  = PET_FR_SAD;
                    dx = ph & 1 ? 1 : -1; // shakes its head
                }
                icon(PET_ICON_NO, 12, FX_Y, 1);
                break;
            case ANIM_HATCH: // the egg wobbles, then out it comes
                if (timer_elapsed32(anim_t) < 1800) {
                    show_egg = true;
                    dx       = ph & 1 ? 1 : -1;
                } else {
                    f = PET_FR_HAPPY;
                    icon(PET_ICON_SPARKLE, ph & 1 ? 0 : 24, FX_Y, 1);
                }
                break;
            case ANIM_GROW: // grew up a stage (or a new egg arrived)
                if (!egg) f = PET_FR_HAPPY;
                icon(PET_ICON_STAR, ph & 1 ? 0 : 24, ph & 1 ? FX_Y : FX_Y + 6, 1);
                icon(PET_ICON_SPARKLE, ph & 1 ? 24 : 0, ph & 1 ? FX_Y + 8 : FX_Y, 1);
                break;
            default:
                break;
        }
    }
    if (egg && !animating && ph % 12 < 2) {
        dx = ph & 1 ? 1 : -1; // something moves inside
    }
    if (asleep && !(animating && anim_type == ANIM_NO)) {
        icon(PET_ICON_Z, 22, (int8_t)(FX_Y + 6 - (ph % 3) * 3), 1); // zzz floating up
    }
    if (sp == PET_SP_DRAGON && f == PET_FR_HAPPY && !egg && ph & 1) {
        icon(PET_ICON_FLAME, 23, FX_Y + 2, 1); // a happy little puff of flame
    }
    sprite(show_egg ? pet_egg_index[sp] : pet_frame_index[sp][st - 1][f], dx, dy);
    if (poop) {
        unpaint(23, 46, 31, FLOOR_Y);
        icon(PET_ICON_POOP, 24, 47, 1);
    }

    // Below: stats, then the care action.
    dotted(57);
    stats(v);
    dotted(90);
    action(v);
}
