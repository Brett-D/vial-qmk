// Copyright 2026 Brett-D
// SPDX-License-Identifier: GPL-2.0-or-later
//
// The pet page of the right OLED: tall and narrow (portrait, 32 x 128 px,
// 5 characters x 16 lines of text). Only the pet; top to bottom:
//
//   y   0-  7  name (first 5 letters)                         text line 0
//   y   9- 16  mood icon: heart / smile / apple / tear / bolt / poop / moon
//   y  18- 27  effects: zzz, ball, apple, sparkles, "no"
//   y  28- 52  Pip, 24 x 24, bobbing; poop beside it when dirty
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

#define PET_X 4
#define PET_Y 28
#define FX_Y 19 // effects band
#define STAT_Y 60
#define ACT_Y 92
#define ANIM_MS 2000

static void sprite(uint8_t spr, int8_t x, int8_t y) {
    for (uint8_t r = 0; r < 24; r++) {
        uint32_t row = pet_sprites[spr][r];
        for (uint8_t c = 0; c < 24; c++) {
            if (row & (1ul << (23 - c)) && x + c >= 0 && y + r >= 0) {
                oled_write_pixel(x + c, y + r, true);
            }
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
        default:
            return PET_ICON_SMILE;
    }
}

// The selected care action: big icon, label, position dots.
static void action(const pet_view_t *v) {
    static const char *const labels[] = {"", "Feed", "Play", "Sleep", "Clean", "New"};
    bool    asleep = v->flags & PV_ASLEEP, choosing = v->flags & PV_MENU;
    uint8_t a      = v->sel < PET_ACTION_COUNT ? v->sel : PET_FEED;
    uint8_t i      = a == PET_FEED ? PET_ICON_APPLE : a == PET_PLAY ? PET_ICON_BALL : a == PET_SLEEP ? (asleep ? PET_ICON_SPARKLE : PET_ICON_MOON) : a == PET_CLEAN ? PET_ICON_SPARKLE : PET_ICON_EGG;
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
    if (v->flags & PV_DEAD) {
        return; // the only choice: a new egg
    }
    for (uint8_t n = 0; n < 4; n++) {
        uint8_t x = 6 + n * 6;
        if (n + 1 == a) {
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
        anim_type = started ? v->anim & 7 : ANIM_NONE; // no replay after a restart
        anim_t    = now;
        started   = true;
    }
    bool     animating = anim_type != ANIM_NONE && timer_elapsed32(anim_t) < ANIM_MS;
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
    uint8_t ph     = (uint8_t)(now / 250);
    bool    asleep = v->flags & PV_ASLEEP, dead = v->flags & PV_DEAD;
    bool    poop   = !dead && v->clean < 40;
    uint8_t name_len = 0;
    while (name_len < 5 && v->name[name_len]) {
        name_len++;
    }

    // Top: name and mood.
    centred(0, v->name, name_len, false);
    icon(mood_icon(v->mood), 12, 9, 1);

    // Middle: the pet and what it's doing.
    int8_t  x    = poop ? 0 : PET_X; // makes room for the poop
    int8_t  y    = PET_Y + ((now / 500) & 1); // bob
    uint8_t face = PET_SPR_IDLE;
    if (dead) {
        face = PET_SPR_DEAD;
        y    = PET_Y - 1 + (ph % 4 < 2 ? 0 : 2); // floats
    } else if (asleep) {
        face = PET_SPR_SLEEP;
        y    = PET_Y + 1;
    } else if (v->food < 20 || v->joy < 20) {
        face = PET_SPR_SAD;
    } else if (v->energy < 20) {
        face = PET_SPR_TIRED;
    } else if (v->mood == MOOD_HAPPY && ph % 16 >= 12) {
        face = PET_SPR_HAPPY;
    }
    if (!dead && !asleep && ph % 16 == 5) {
        face = PET_SPR_BLINK;
    }
    if (animating) {
        switch (anim_type) {
            case ANIM_EAT:
                face = ph & 1 ? PET_SPR_EAT : PET_SPR_IDLE; // chewing
                if (timer_elapsed32(anim_t) < ANIM_MS / 2) {
                    icon(PET_ICON_APPLE, x + 8, FX_Y, 1);
                }
                break;
            case ANIM_PLAY:
                face = PET_SPR_HAPPY;
                icon(PET_ICON_BALL, (int8_t)((ph % 4) * 8), ph & 1 ? FX_Y - 1 : FX_Y + 1, 1);
                break;
            case ANIM_CLEAN:
            case ANIM_WAKE:
                icon(PET_ICON_SPARKLE, ph & 1 ? 0 : 24, FX_Y, 1);
                icon(PET_ICON_SPARKLE, ph & 1 ? 24 : 0, FX_Y + 16, 1);
                break;
            case ANIM_NO:
                if (!asleep && !dead) {
                    face = PET_SPR_SAD;
                    x += ph & 1 ? 1 : -1; // shakes its head
                }
                icon(PET_ICON_NO, 12, FX_Y, 1);
                break;
            case ANIM_HATCH:
                face = timer_elapsed32(anim_t) < ANIM_MS * 3 / 4 ? PET_SPR_EGG : PET_SPR_HAPPY;
                if (face == PET_SPR_EGG) {
                    x += ph & 1 ? 1 : -1; // wobbles
                }
                break;
            default:
                break;
        }
    }
    if (asleep && !(animating && anim_type == ANIM_NO)) {
        icon(PET_ICON_Z, 22, (int8_t)(FX_Y + 6 - (ph % 3) * 3), 1); // zzz floating up
    }
    sprite(face, x, y);
    if (poop) {
        icon(PET_ICON_POOP, 24, 45, 1);
    }

    // Below: stats, then the care action.
    dotted(57);
    stats(v);
    dotted(90);
    action(v);
}
