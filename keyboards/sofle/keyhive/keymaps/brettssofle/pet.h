// Copyright 2026 Brett-D
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Pip, the OLED pet (pet.c: care and stats on the master; pet_draw.c: the
// right OLED's pet page).

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "action.h"

enum pet_action { PET_NONE, PET_FEED, PET_PLAY, PET_SLEEP, PET_CLEAN, PET_NEW, PET_ACTION_COUNT };
// What the right OLED animates for a moment (pet view anim & 7).
enum pet_anim { ANIM_NONE, ANIM_EAT, ANIM_PLAY, ANIM_SLEEP, ANIM_WAKE, ANIM_CLEAN, ANIM_NO, ANIM_HATCH };
enum pet_mood { MOOD_HAPPY, MOOD_OK, MOOD_HUNGRY, MOOD_SAD, MOOD_TIRED, MOOD_DIRTY, MOOD_ASLEEP, MOOD_DEAD, MOOD_OFF };

// Synced to the right half inside the OLED view (oled_pages.c).
typedef struct {
    uint8_t flags; // PV_*
    uint8_t food, joy, energy, clean;
    uint8_t sel;     // the selected care action (enum pet_action)
    uint8_t anim;    // (seq << 3) | enum pet_anim: a change starts the animation
    uint8_t mood;    // enum pet_mood
    char    name[5]; // the first 5 letters (one OLED line), not terminated
} pet_view_t;
enum { PV_ON = 1, PV_ASLEEP = 2, PV_DEAD = 4, PV_MENU = 8 };

void pet_init(void); // master
void pet_task(void); // master: time, saving
bool pet_enabled(void);
void pet_view(pet_view_t *v);
// Master input while the pet page is shown (right knob): turn = choose, short
// press = do it. Returns true when the event was used.
bool pet_process(keyrecord_t *record, bool pet_page);
void pet_press(void);
// Raw HID 0xF6 0x18 (get) / 0x19 (action) / 0x1A (settings); writes need `allowed`.
void pet_raw_hid(uint8_t *data, bool allowed);
// Backup: settings + name.

void pet_draw(const pet_view_t *v, bool force); // right half
