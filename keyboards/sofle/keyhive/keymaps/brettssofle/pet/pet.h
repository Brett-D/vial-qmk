// Copyright 2026 Brett-D
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Pip, the OLED pet (pet.c: care and stats on the master; pet_draw.c: the
// right OLED's pet page).

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "action.h"
#include "pet_ids.h"

enum pet_action { PET_NONE, PET_FEED, PET_PLAY, PET_SLEEP, PET_CLEAN, PET_NEW, PET_NEXT, PET_ACTION_COUNT }; // NEXT: leave the page
// What the right OLED animates for a moment (pet view anim & 15).
enum pet_anim { ANIM_NONE, ANIM_EAT, ANIM_PLAY, ANIM_SLEEP, ANIM_WAKE, ANIM_CLEAN, ANIM_NO, ANIM_HATCH, ANIM_GROW };
enum pet_mood { MOOD_HAPPY, MOOD_OK, MOOD_HUNGRY, MOOD_SAD, MOOD_TIRED, MOOD_DIRTY, MOOD_ASLEEP, MOOD_DEAD, MOOD_OFF, MOOD_EGG };

// Synced to the right half inside the OLED view (oled_pages.c).
typedef struct {
    uint8_t flags; // PV_*
    uint8_t food, joy, energy, clean;
    uint8_t sel;     // what a tap does now (enum pet_action; PET_NEXT while the menu is closed)
    uint8_t anim;    // (seq << 4) | enum pet_anim: a change starts the animation
    uint8_t mood;    // enum pet_mood
    char    name[5]; // the first 5 letters (one OLED line), not terminated
    uint8_t species; // PET_SP_* (pet_art.h)
    uint8_t stage;   // PET_STAGE_*: egg, baby, child, adult
} pet_view_t;
enum { PV_ON = 1, PV_ASLEEP = 2, PV_DEAD = 4, PV_MENU = 8, PV_BIG = 16 }; // PV_BIG: v26 big pet (slim stats while the menu is closed)

void pet_init(void); // master
void pet_task(void); // master: time, saving
bool pet_enabled(void);
void pet_view(pet_view_t *v);
// Master input while the pet page is shown (right knob): a turn opens the care
// menu and chooses (Feed / Play / Sleep / Clean / Next, wraps). Returns true
// when the event was used.
bool pet_process(keyrecord_t *record, bool pet_page);
// A tap on the pet page: true = did the care action shown in the open menu;
// false = leave the page (menu closed, or "Next" chosen).
bool pet_press(void);
void pet_menu_close(void);
// Raw HID 0xF6 0x18 (get) / 0x19 (action; new pet: species in data[3]) /
// 0x1A (settings) / 0x1B (species + stage, for restoring a backup) / 0x1D
// (v26: LED reactions, big pet); writes need `allowed`.
void pet_raw_hid(uint8_t *data, bool allowed);
// Backup: settings + name.

void pet_draw(const pet_view_t *v, bool force); // right half
uint16_t pet_seed(uint8_t species, const char *name); // its motion's seed (pet_motion.h)
uint8_t  pet_typing_wpm(void); // this half's typing speed (led_fx_draw.c; 0 without RGB)
