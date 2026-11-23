// Copyright 2026 Brett-D
// SPDX-License-Identifier: GPL-2.0-or-later
//
// The pet page of the right OLED (portrait, 32 x 128 pixels): only the pet.
//
//   y   0- 27  effects: zzz, hearts, ball, food, sparkles
//   y  30- 53  Pip (24 x 24, bobbing)        poop beside it when dirty
//   y  64- 92  the care menu while choosing: big icon + one dot per choice
//   y 101-126  food / joy / energy / clean bars with tiny icons
//
// Redrawn only when something changes (about 4 times a second while idle).

#include "brettssofle.h"
#include "pet.h"
#include "pet_art.h"

#define PET_X 4
#define PET_Y 30
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

static void stats(const pet_view_t *v) {
    const uint8_t *s = &v->food;
    for (uint8_t i = 0; i < 4; i++) {
        uint8_t y = 101 + i * 7;
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

static uint8_t action_icon(uint8_t action, bool asleep) {
    switch (action) {
        case PET_FEED:
            return PET_ICON_APPLE;
        case PET_PLAY:
            return PET_ICON_BALL;
        case PET_SLEEP:
            return asleep ? PET_ICON_SPARKLE : PET_ICON_MOON; // wake up / go to sleep
        case PET_CLEAN:
            return PET_ICON_SPARKLE;
        default:
            return PET_ICON_EGG;
    }
}

static void menu(const pet_view_t *v) {
    box(6, 64, 25, 83, false);
    icon(action_icon(v->sel, v->flags & PV_ASLEEP), 8, 66, 2);
    if (v->flags & PV_DEAD) {
        return; // the only choice: a new egg
    }
    for (uint8_t i = 0; i < 4; i++) {
        uint8_t x = 6 + i * 6;
        if (i + 1 == v->sel) {
            box(x, 88, x + 2, 90, true);
        } else {
            oled_write_pixel(x + 1, 89, true);
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
    int8_t  y      = PET_Y + ((now / 500) & 1); // bob
    uint8_t face   = PET_SPR_IDLE;
    uint8_t avg    = (v->food + v->joy + v->energy + v->clean) / 4;
    if (dead) {
        face = PET_SPR_DEAD;
        y    = PET_Y - 2 + (ph % 4 < 2 ? 0 : 2); // floats
    } else if (asleep) {
        face = PET_SPR_SLEEP;
        y    = PET_Y + 1;
    } else if (v->food < 20 || v->joy < 20) {
        face = PET_SPR_SAD;
    } else if (v->energy < 20) {
        face = PET_SPR_TIRED;
    } else if (avg >= 70 && ph % 16 >= 12) {
        face = PET_SPR_HAPPY;
    }
    if (!dead && !asleep && ph % 16 == 5) {
        face = PET_SPR_BLINK;
    }
    int8_t x = PET_X;
    if (animating) {
        switch (anim_type) {
            case ANIM_EAT:
                face = ph & 1 ? PET_SPR_EAT : PET_SPR_IDLE; // chewing
                if (timer_elapsed32(anim_t) < ANIM_MS / 2) {
                    icon(PET_ICON_APPLE, 12, 18, 1);
                }
                break;
            case ANIM_PLAY:
                face = PET_SPR_HAPPY;
                icon(PET_ICON_BALL, (int8_t)(1 + (ph % 4) * 7), ph & 1 ? 8 : 16, 1);
                icon(PET_ICON_HEART, 22, 2, 1);
                break;
            case ANIM_CLEAN:
            case ANIM_WAKE:
                icon(PET_ICON_SPARKLE, ph & 1 ? 2 : 22, ph & 1 ? 14 : 6, 1);
                icon(PET_ICON_SPARKLE, ph & 1 ? 22 : 4, ph & 1 ? 20 : 22, 1);
                break;
            case ANIM_NO:
                if (!asleep && !dead) {
                    face = PET_SPR_SAD;
                    x    = PET_X + (ph & 1 ? 1 : -1); // shakes its head
                }
                icon(PET_ICON_NO, 12, 14, 1);
                break;
            case ANIM_HATCH:
                face = timer_elapsed32(anim_t) < ANIM_MS * 3 / 4 ? PET_SPR_EGG : PET_SPR_HAPPY;
                x    = PET_X + (face == PET_SPR_EGG ? (ph & 1 ? 1 : -1) : 0);
                break;
            default:
                break;
        }
    }
    if (asleep) {
        icon(PET_ICON_Z, 20, (int8_t)(20 - (ph % 3) * 6), 1); // zzz floating up
    } else if (!animating && !dead && avg >= 80 && ph % 16 >= 12) {
        icon(PET_ICON_HEART, 22, 12, 1);
    }
    sprite(face, x, y);
    if (!dead && v->clean < 40) {
        icon(PET_ICON_POOP, 23, 55, 1);
    }
    if (v->flags & PV_MENU) {
        menu(v);
    }
    stats(v);
}
