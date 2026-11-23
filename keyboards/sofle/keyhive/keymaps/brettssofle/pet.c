// Copyright 2026 Brett-D
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Pip, the OLED pet: care and stats (master half).
//
// Stats 0-100: food, joy, energy, clean. They drift slowly while the keyboard
// is powered (per hour at "normal": food -5, joy -4, energy -3, clean -3, so
// a full stat lasts about a day; asleep: energy +25, joy -1; "slow" halves,
// "fast" doubles). Typing counts
// as exercise: every 40 key presses, joy +1 and energy -1. It's forgiving: it
// only gets hungry, sad, tired or dirty, unless "can die" is on (then 24 h
// with no food and no joy at all turns it into a ghost; "new pet" hatches a
// new egg).
//
// On the pet page (right OLED): right knob turn = choose Feed / Play / Sleep /
// Clean, short press = do it, hold 0.5-2 s = next OLED page.
//
// Saved in the last 32 bytes of the EEPROM (taken off the end of Vial's macro
// buffer, so nothing else moves): every 10 min if something changed, and
// 15 s after care or a settings change (so a burst of clicks is one write).

#include "brettssofle.h"
#include "eeprom.h"
#include "pet.h"

#define PET_EE_ADDR ((void *)(uintptr_t)(DYNAMIC_KEYMAP_EEPROM_MAX_ADDR + 1))
#define PET_EE_SIZE 32
#define PET_MAGIC 0xB7
#define PET_VERSION 1
#define PET_NAME_LEN 10
#ifndef PET_SAVE_PERIOD_MS
#    define PET_SAVE_PERIOD_MS 600000
#endif
#define PET_SAVE_SOON_MS 15000
#define PET_MENU_MS 6000
#define PET_TYPING_STEP 40
#define PET_GET_MARK 0x9E

enum { F_ON = 1, F_CAN_DIE = 2, F_ASLEEP = 4, F_DEAD = 8 };
enum { S_FOOD, S_JOY, S_ENERGY, S_CLEAN };

typedef struct __attribute__((packed)) {
    uint8_t  magic, version, flags;
    uint8_t  stat[4];   // food, joy, energy, clean
    uint8_t  decay;     // 0 slow, 1 normal, 2 fast
    uint8_t  neglect_h; // hours with no food and no joy
    uint32_t age_min;
    char     name[PET_NAME_LEN];
    uint8_t  sum;
} pet_store_t;
_Static_assert(sizeof(pet_store_t) <= PET_EE_SIZE, "pet record too big");
#ifdef TOTAL_EEPROM_BYTE_COUNT
_Static_assert(DYNAMIC_KEYMAP_EEPROM_MAX_ADDR + 1 + PET_EE_SIZE <= TOTAL_EEPROM_BYTE_COUNT, "the pet needs the last 32 EEPROM bytes (config.h)");
#endif

static pet_store_t p;
static bool        dirty;
static uint32_t    minute_t, save_t, soon_t; // soon_t: 0 = no early save pending
static uint16_t    acc[4], keys, neglect_min;
static uint8_t     sel, anim, anim_seq;
static bool        menu_on;
static uint32_t    menu_t;
uint16_t           pet_saves; // EEPROM writes since boot (tests / diagnostics)

static uint8_t checksum(const pet_store_t *s) {
    const uint8_t *b   = (const uint8_t *)s;
    uint8_t        sum = 0x5A;
    for (uint8_t i = 0; i < offsetof(pet_store_t, sum); i++) {
        sum = (uint8_t)(sum * 31 + b[i]);
    }
    return sum;
}

static void hatch(void) {
    p.stat[S_FOOD] = p.stat[S_JOY] = p.stat[S_ENERGY] = 80;
    p.stat[S_CLEAN]                                   = 100;
    p.flags &= (uint8_t)~(F_DEAD | F_ASLEEP);
    p.age_min   = 0;
    p.neglect_h = 0;
    neglect_min = 0;
}

static void defaults(void) {
    memset(&p, 0, sizeof(p));
    p.magic   = PET_MAGIC;
    p.version = PET_VERSION;
    p.flags   = F_ON;
    p.decay   = 1;
    memcpy(p.name, "Pip", 4);
    hatch();
}

static void save_soon(void) {
    dirty  = true;
    soon_t = timer_stamp32();
}

void pet_init(void) {
    eeprom_read_block(&p, PET_EE_ADDR, sizeof(p));
    if (p.magic != PET_MAGIC || p.version != PET_VERSION || p.sum != checksum(&p) || p.decay > 2) {
        defaults(); // first start (or the macro area's old bytes): a new egg
        save_soon();
    }
    p.name[PET_NAME_LEN - 1] = 0;
    minute_t = save_t = timer_read32();
}

bool pet_enabled(void) {
    return p.flags & F_ON;
}

static void add(uint8_t s, int8_t d) {
    int16_t v    = p.stat[s] + d;
    p.stat[s]    = v < 0 ? 0 : v > 100 ? 100 : v;
}

static void animate(uint8_t type) {
    anim_seq = (anim_seq + 1) & 31;
    anim     = anim_seq << 3 | type;
}

static uint8_t mood(void) {
    if (!(p.flags & F_ON)) return MOOD_OFF;
    if (p.flags & F_DEAD) return MOOD_DEAD;
    if (p.flags & F_ASLEEP) return MOOD_ASLEEP;
    if (p.stat[S_FOOD] < 20) return MOOD_HUNGRY;
    if (p.stat[S_JOY] < 20) return MOOD_SAD;
    if (p.stat[S_ENERGY] < 20) return MOOD_TIRED;
    if (p.stat[S_CLEAN] < 25) return MOOD_DIRTY;
    return p.stat[S_FOOD] + p.stat[S_JOY] + p.stat[S_ENERGY] + p.stat[S_CLEAN] >= 280 ? MOOD_HAPPY : MOOD_OK;
}

// One minute of life.
static void tick(void) {
    if (!(p.flags & F_ON) || (p.flags & F_DEAD)) {
        return;
    }
    bool          asleep = p.flags & F_ASLEEP;
    const uint8_t m2     = p.decay == 0 ? 1 : p.decay == 2 ? 4 : 2; // speed x2
    const int8_t  per_h[4] = {-5, asleep ? -1 : -4, asleep ? 25 : -3, -3};
    p.age_min++;
    for (uint8_t s = 0; s < 4; s++) {
        int8_t r = per_h[s];
        acc[s] += (uint16_t)((r < 0 ? -r : r) * m2);
        while (acc[s] >= 120) { // 120 = one point at x1 speed per hour
            acc[s] -= 120;
            add(s, r < 0 ? -1 : 1);
        }
    }
    if (asleep && p.stat[S_ENERGY] >= 100) {
        p.flags &= (uint8_t)~F_ASLEEP; // rested: wakes up by itself
        animate(ANIM_WAKE);
    }
    if (p.stat[S_FOOD] == 0 && p.stat[S_JOY] == 0) {
        if (++neglect_min >= 60) {
            neglect_min = 0;
            if (p.neglect_h < 255) p.neglect_h++;
        }
        if ((p.flags & F_CAN_DIE) && p.neglect_h >= 24) {
            p.flags |= F_DEAD;
            p.flags &= (uint8_t)~F_ASLEEP;
            save_soon();
        }
    } else {
        neglect_min = 0;
        p.neglect_h = 0;
    }
    dirty = true;
}

// Care. Returns false when the pet won't (asleep, full, too tired...).
static bool act(uint8_t a) {
    if (!(p.flags & F_ON)) return false;
    bool ok = true;
    if (p.flags & F_DEAD) {
        ok = a == PET_NEW;
    } else if ((p.flags & F_ASLEEP) && (a == PET_FEED || a == PET_PLAY)) {
        ok = false;
    }
    if (ok) {
        switch (a) {
            case PET_FEED:
                if (p.stat[S_FOOD] >= 95) {
                    add(S_JOY, -2); // overfed
                    ok = false;
                    break;
                }
                add(S_FOOD, 25);
                add(S_CLEAN, -3);
                animate(ANIM_EAT);
                break;
            case PET_PLAY:
                if (p.stat[S_ENERGY] < 10) {
                    ok = false; // too tired
                    break;
                }
                add(S_JOY, 20);
                add(S_ENERGY, -8);
                add(S_FOOD, -4);
                animate(ANIM_PLAY);
                break;
            case PET_SLEEP:
                p.flags ^= F_ASLEEP;
                animate(p.flags & F_ASLEEP ? ANIM_SLEEP : ANIM_WAKE);
                break;
            case PET_CLEAN:
                p.stat[S_CLEAN] = 100;
                add(S_JOY, 3);
                animate(ANIM_CLEAN);
                break;
            case PET_NEW:
                hatch();
                animate(ANIM_HATCH);
                break;
            default:
                ok = false;
        }
    }
    if (!ok) {
        animate(ANIM_NO);
    }
    save_soon();
    return ok;
}

static uint8_t menu_count(void) {
    return p.flags & F_DEAD ? 1 : 4;
}

static uint8_t menu_action(uint8_t i) {
    static const uint8_t items[4] = {PET_FEED, PET_PLAY, PET_SLEEP, PET_CLEAN};
    return p.flags & F_DEAD ? PET_NEW : items[i % 4];
}

bool pet_process(keyrecord_t *record, bool pet_page) {
    keyevent_t *ev = &record->event;
    if (!(p.flags & F_ON) || !ev->pressed) {
        return false;
    }
    if (ev->type == KEY_EVENT) {
        if (!(p.flags & (F_ASLEEP | F_DEAD)) && ++keys >= PET_TYPING_STEP) {
            keys = 0; // typing = exercise
            add(S_JOY, 1);
            if (p.stat[S_ENERGY] > 10) add(S_ENERGY, -1);
            dirty = true;
        }
        return false;
    }
    if (pet_page && IS_ENCODEREVENT(*ev) && ev->key.col == 1) {
        int8_t  dir = ev->type == ENCODER_CW_EVENT ? 1 : -1;
        uint8_t n   = menu_count();
        if (menu_on) {
            sel = (uint8_t)((sel + n + dir) % n);
        } else {
            sel %= n;
        }
        menu_on = true;
        menu_t  = timer_read32();
        return true; // the right knob chooses care while the pet is shown
    }
    return false;
}

void pet_press(void) {
    if (!(p.flags & F_ON)) {
        return;
    }
    if (menu_on) {
        act(menu_action(sel));
    }
    sel %= menu_count();
    menu_on = true; // the first press only shows the menu
    menu_t  = timer_read32();
}

void pet_task(void) {
    if (!is_keyboard_master()) {
        return;
    }
    if (timer_elapsed32(minute_t) >= 60000) {
        minute_t += 60000;
        tick();
    }
    if (menu_on && timer_elapsed32(menu_t) > PET_MENU_MS) {
        menu_on = false;
    }
    if (dirty && ((soon_t && timer_elapsed32(soon_t) >= PET_SAVE_SOON_MS) || timer_elapsed32(save_t) >= PET_SAVE_PERIOD_MS)) {
        p.sum = checksum(&p);
        eeprom_update_block(&p, PET_EE_ADDR, sizeof(p));
        pet_saves++;
        dirty  = false;
        soon_t = 0;
        save_t = timer_read32();
    }
}

void pet_view(pet_view_t *v) {
    v->flags = (p.flags & F_ON ? PV_ON : 0) | (p.flags & F_ASLEEP ? PV_ASLEEP : 0) | (p.flags & F_DEAD ? PV_DEAD : 0) | (menu_on ? PV_MENU : 0);
    memcpy(&v->food, p.stat, 4);
    v->sel  = menu_action(sel);
    v->anim = anim;
}

/* ---- config page: 0xF6 0x18 get, 0x19 care, 0x1A settings ---- */

void pet_raw_hid(uint8_t *d, bool allowed) {
    uint8_t result = 0;
    if (d[1] == 0x19 && allowed) {
        result = act(d[2]) ? 1 : 2;
    } else if (d[1] == 0x1A && allowed) {
        p.flags = (p.flags & (uint8_t)~(F_ON | F_CAN_DIE)) | (d[2] & 1 ? F_ON : 0) | (d[2] & 2 ? F_CAN_DIE : 0);
        p.decay = d[3] > 2 ? 1 : d[3];
        if (d[4] && d[4] != 0xFF) {
            memset(p.name, 0, sizeof(p.name));
            for (uint8_t i = 0; i < PET_NAME_LEN - 1 && d[4 + i] >= 0x20 && d[4 + i] < 0x7F; i++) {
                p.name[i] = d[4 + i];
            }
        }
        save_soon();
        result = 1;
    } else if (d[1] != 0x18) {
        result = 3; // refused: no confirmed session
    }
    memset(d + 2, 0, 30);
    d[2] = PET_GET_MARK;
    d[3] = p.flags;
    memcpy(d + 4, p.stat, 4);
    d[8]  = p.decay;
    d[9]  = p.age_min;
    d[10] = p.age_min >> 8;
    d[11] = p.age_min >> 16;
    d[12] = p.age_min >> 24;
    d[13] = mood();
    memcpy(d + 14, p.name, PET_NAME_LEN);
    d[24] = p.neglect_h;
    d[25] = anim & 7;
    d[26] = result; // 0 read, 1 done, 2 the pet won't, 3 refused
    d[27] = menu_action(sel);
}
