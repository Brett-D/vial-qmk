// Copyright 2026 Brett-D
// SPDX-License-Identifier: GPL-2.0-or-later
//
// The right OLED's page (master) and the OLED_PAGE key (the right knob press).
//
// Every page: a tap (release before 1 s) = next page. A hold of 2 s or more is
// left to the hold-both-knobs config combo.
//
// The pet page: with the care menu closed (it closes 6 s after the last turn)
// the screen shows "Next" and a tap goes to the next page, as everywhere. A
// right-knob turn opens the menu (Feed / Play / Sleep / Clean / Next, wraps);
// a tap then does the shown choice, and "Next" leaves. A 0.5-2 s hold also
// leaves, with or without the menu.

#include "brettssofle.h"
#include "pet.h"
#include "oled_pages.h"

#define TAP_MS 500     // pet page: shorter = tap
#define LEAVE_MS 1000  // other pages: a release before this changes the page
#define COMBO_MS 2000  // from here on it's the config combo, not a page change

static uint8_t page;

__attribute__((weak)) void oled_page_changed(uint8_t p) {} // oled_pages.c saves it

uint8_t oled_page_current(void) {
    return page;
}

void oled_page_set(uint8_t p) {
    page = p < OLED_PAGE_COUNT ? p : 0;
    if (page == OLED_PAGE_PET && !pet_enabled()) {
        page = 0;
    }
}

void oled_page_next(void) {
    page = (page + 1) % OLED_PAGE_COUNT;
    if (page == OLED_PAGE_PET && !pet_enabled()) {
        page = (page + 1) % OLED_PAGE_COUNT; // no pet: skip its page
    }
    pet_menu_close();
    oled_page_changed(page);
}

void oled_page_key(keyrecord_t *record) {
    static uint16_t pressed_at;
    static bool     down;
    if (record->event.pressed) {
        pressed_at = timer_read();
        down       = true;
        return;
    }
    if (!down) {
        return; // a release without its press (e.g. a layer change in between)
    }
    down          = false;
    uint16_t held = timer_elapsed(pressed_at);
    if (held >= COMBO_MS) {
        return;
    }
    if (page == OLED_PAGE_PET && pet_enabled()) {
        if (held < TAP_MS && pet_press()) {
            return; // did the care action shown in the open menu
        }
        oled_page_next(); // tap with the menu closed, "Next", or a hold
    } else if (held < LEAVE_MS) {
        oled_page_next();
    }
}
