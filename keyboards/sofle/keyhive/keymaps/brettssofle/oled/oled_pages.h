// Copyright 2026 Brett-D
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Right OLED: pages (oled_pages.c), the OLED_PAGE key (page_key.c), the
// left/right OLED drawing (oled.c).

#pragma once

#include QMK_KEYBOARD_H

enum oled_pages { OLED_PAGE_STATUS, OLED_PAGE_TRACKPAD, OLED_PAGE_LEDS, OLED_PAGE_INFO, OLED_PAGE_PET, OLED_PAGE_COUNT };
void    oled_pages_init(void);              // keyboard_post_init_user, both halves
void    oled_page_next(void);               // master (page_key.c)
uint8_t oled_page_current(void);            // master: the right OLED's page
void    oled_page_set(uint8_t page);        // master: at start (saved page)
void    oled_page_changed(uint8_t page);    // oled_pages.c: saves it
void    oled_page_key(keyrecord_t *record); // the OLED_PAGE key (page_key.c)
void    oled_pages_task(void);              // housekeeping, master syncs the right half
bool    net_addr_enter(void);               // v26: the NET screen's knob presses Enter after the address (saved)
void    net_addr_enter_set(bool on);
#ifdef OLED_ENABLE
void oled_right_init(void);
bool oled_pages_render(void); // right half: false = status page
void oled_pages_invalidate(void); // v29: the screen was cleared under it: start the page afresh
uint16_t oled_i2c_recoveries(void); // v29 oled.c: times the right OLED's I2C bus was cleared and the OLED re-initialised
void oled_pages_status_footer(void);
#endif
