// Copyright 2026 Brett-D
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Every lighting effect's kind, in one place: the firmware validates
// combinations with it (led_fx.c) and tools/gen_config.py reads
// this file to group, disable and warn on the config page. One row per
// effect: X(id, NAME, "Name", kind flags, main hue).
//
//   FXK_BG       a whole-keyboard effect (background)
//   FXK_OVERLAY  a key-press effect drawn over a background
//   FXK_SOLO     standalone: reacts to keys itself, so no key-press effect
//                can go with it ("used on its own")
//   FXK_DARK     dark or sparse: a key-press effect close to its main colour,
//                or tinted, is hard to see (the page warns, it doesn't block)
//   FXK_OWNHUE   ignores the colour setting (fire, ocean, candle, ...)
//   main hue     its dominant colour (QMK hue), 255 = the colour setting

#pragma once

#define FXK_BG 1
#define FXK_OVERLAY 2
#define FXK_SOLO 4
#define FXK_DARK 8
#define FXK_OWNHUE 16

// Our own backgrounds (led_fx_core.h): background id = FX_OWN_BG + id.
#define FX_OWN_BG 0xC0
#define FX_OWN_BACKGROUNDS(X)                                   \
    X(0, AURORA, "Aurora", FXK_BG, 255)                        \
    X(1, FIRE, "Fire (rising embers)", FXK_BG | FXK_OWNHUE, 10) \
    X(2, STARFIELD, "Starfield", FXK_BG | FXK_DARK, 255)        \
    X(3, OCEAN, "Ocean waves", FXK_BG | FXK_OWNHUE, 140)        \
    X(4, COMET, "Comet sweep", FXK_BG | FXK_DARK, 255)          \
    X(5, LAVA, "Lava lamp", FXK_BG, 255)                        \
    X(6, CANDLE, "Candle", FXK_BG | FXK_OWNHUE, 20)             \
    X(7, SUNRISE, "Sunrise", FXK_BG | FXK_OWNHUE, 20)           \
    X(8, BREATHE_GRADIENT, "Breathing gradient", FXK_BG, 255)   \
    X(9, CODE_RAIN, "Code rain", FXK_BG | FXK_DARK, 255)

// Our own standalone effects: background id = FX_OWN_SOLO + id.
#define FX_OWN_SOLO 0xD0
#define FX_OWN_SOLOS(X)                                    \
    X(0, TYPING_SPEED, "Typing speed meter", FXK_SOLO, 85) \
    X(1, TYPING_VU, "Typing VU meter", FXK_SOLO, 85)

// Key-press effects (fx_settings_t.effect; 0 = none).
#define FX_OVERLAYS(X)                                \
    X(1, RING, "Ring ripple", FXK_OVERLAY, 255)       \
    X(2, WAVE, "Echo rings", FXK_OVERLAY, 255)        \
    X(3, SPLASH, "Splash (filled)", FXK_OVERLAY, 255) \
    X(4, GLOW, "Key glow", FXK_OVERLAY, 255)          \
    X(5, CROSS, "Cross (row + column)", FXK_OVERLAY, 255) \
    X(6, NEXUS, "Nexus (dots along row + column)", FXK_OVERLAY, 255) \
    X(7, SHOCKWAVE, "Shockwave with trail", FXK_OVERLAY, 255) \
    X(8, SPARKS, "Sparks", FXK_OVERLAY, 255)          \
    X(9, LIGHTNING, "Lightning", FXK_OVERLAY, 255)    \
    X(10, DROPLETS, "Rain splash", FXK_OVERLAY, 255)  \
    X(11, TRAIL, "Key trail (afterglow)", FXK_OVERLAY, 255) \
    X(12, HEAT, "Heat (spreads, cools down)", FXK_OVERLAY, 255)

// QMK's effects that need a flag (by VialRGB id, and NAME = its rgb_matrix
// mode; all are enabled in config.h): dark/sparse ones and the ones that
// react to keys themselves. QMK's other key-press effects (reactive, splash,
// ...) are replaced by the overlays above; the rest are plain backgrounds.
#define FX_QMK_FLAGS(X)                                               \
    X(30, DIGITAL_RAIN, "Digital rain", FXK_BG | FXK_DARK | FXK_OWNHUE, 85) \
    X(24, RAINDROPS, "Raindrops", FXK_BG | FXK_DARK, 255)             \
    X(25, JELLYBEAN_RAINDROPS, "Jellybean raindrops", FXK_BG | FXK_DARK | FXK_OWNHUE, 255) \
    X(43, PIXEL_RAIN, "Pixel rain", FXK_BG | FXK_DARK | FXK_OWNHUE, 255) \
    X(29, TYPING_HEATMAP, "Typing heatmap", FXK_SOLO, 0)              \
    X(31, SOLID_REACTIVE_SIMPLE, "Reactive simple", FXK_OVERLAY, 255) \
    X(32, SOLID_REACTIVE, "Reactive (whole keyboard)", FXK_SOLO, 255)
