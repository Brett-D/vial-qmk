// Copyright 2026 Brett-D
// SPDX-License-Identifier: GPL-2.0-or-later
//
// v27 LED screensaver (screensaver.c).

#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint8_t after;  // index: Off, 1, 2, 5, 10, 15, 30 min (kb_config.h)
    uint8_t effect; // our own background, 0-9
    bool    pet;    // pet LED reactions still play over it
    uint8_t hue;    // multiple of 8
    uint8_t sat, val, speed; // level indexes (kb_config.h)
} ss_settings_t;

void screensaver_load(void); // master: the saved settings (init, revert)
void screensaver_save(void); // master: with the lighting (led_fx_save)
void screensaver_get(ss_settings_t *s);
bool screensaver_set(const ss_settings_t *s); // master, preview (saved by led_fx_save)
void screensaver_preview(bool on);            // master: show it now (until a key, knob or trackpad, or 10 s)
void screensaver_input(void);                 // master: a key / knob event (ends a preview)
bool screensaver_wanted(void);                // master: it should be on now

// The lighting RPC (led_fx.c): master packs, right half unpacks.
#define SS_RPC_LEN 7
void screensaver_pack(uint8_t p[SS_RPC_LEN]);
void screensaver_unpack(const uint8_t p[SS_RPC_LEN]);

// Both halves, at flush (rgb_driver.c): 0 = not shown, else its share (1-256, eased).
uint16_t screensaver_frame_begin(bool calibrating);
void     screensaver_pixel(uint8_t led, uint8_t rgb[3], uint16_t mix);
void     screensaver_blank(void); // the LEDs went dark: start from the normal lighting next time
bool     screensaver_pet_ok(void); // pet reactions may play now
