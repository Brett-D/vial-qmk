// Copyright 2026 Brett-D
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Layered lighting: a whole-keyboard effect (static colour, rainbow, ...)
// with a key-press effect (reactive, splash, ...) drawn on top in its own
// colour. Presses light up and fade back into the whole-keyboard effect. The
// LAYERED effect (rgb_matrix_user.inc) draws both; rgb_driver.c blends them.
//
// The choice is made on the config page: either layer can be "none", which
// selects the other one as a plain rgb_matrix effect, so Vial keeps working
// for single effects. The pair and the key-press colour are saved in the
// EEPROM keyboard datablock (on the master) and synced to the right half with
// the config-mode state (config_mode.c).

#include "brettssofle.h"
#include "led_layers.h"

#ifdef RGB_MATRIX_ENABLE

#    define LAYERS_EE_OFFSET 80 // after the LED map (0-75) and the OLED page (78-79)
#    define LAYERS_EE_MAGIC 0x4C

typedef struct {
    uint8_t magic, base, react, hue, sat, sum;
} layers_store_t;
_Static_assert(LAYERS_EE_OFFSET + sizeof(layers_store_t) <= EECONFIG_KB_DATA_SIZE, "raise EECONFIG_KB_DATA_SIZE");

static uint8_t base_mode, react_mode, react_hue, react_sat;

bool led_layers_is_react(uint8_t mode) {
    switch (mode) {
#    ifdef ENABLE_RGB_MATRIX_SOLID_REACTIVE_SIMPLE
        case RGB_MATRIX_SOLID_REACTIVE_SIMPLE:
#    endif
#    ifdef ENABLE_RGB_MATRIX_SOLID_REACTIVE_WIDE
        case RGB_MATRIX_SOLID_REACTIVE_WIDE:
#    endif
#    ifdef ENABLE_RGB_MATRIX_SOLID_REACTIVE_MULTIWIDE
        case RGB_MATRIX_SOLID_REACTIVE_MULTIWIDE:
#    endif
#    ifdef ENABLE_RGB_MATRIX_SOLID_REACTIVE_CROSS
        case RGB_MATRIX_SOLID_REACTIVE_CROSS:
#    endif
#    ifdef ENABLE_RGB_MATRIX_SOLID_REACTIVE_MULTICROSS
        case RGB_MATRIX_SOLID_REACTIVE_MULTICROSS:
#    endif
#    ifdef ENABLE_RGB_MATRIX_SOLID_REACTIVE_NEXUS
        case RGB_MATRIX_SOLID_REACTIVE_NEXUS:
#    endif
#    ifdef ENABLE_RGB_MATRIX_SOLID_REACTIVE_MULTINEXUS
        case RGB_MATRIX_SOLID_REACTIVE_MULTINEXUS:
#    endif
#    ifdef ENABLE_RGB_MATRIX_SPLASH
        case RGB_MATRIX_SPLASH:
#    endif
#    ifdef ENABLE_RGB_MATRIX_MULTISPLASH
        case RGB_MATRIX_MULTISPLASH:
#    endif
#    ifdef ENABLE_RGB_MATRIX_SOLID_SPLASH
        case RGB_MATRIX_SOLID_SPLASH:
#    endif
#    ifdef ENABLE_RGB_MATRIX_SOLID_MULTISPLASH
        case RGB_MATRIX_SOLID_MULTISPLASH:
#    endif
            return true;
        default:
            return false;
    }
}

// Key-press effects that paint every key (or need their own key handling), so
// they can't be layered over a whole-keyboard effect.
bool led_layers_is_solo(uint8_t mode) {
    switch (mode) {
#    ifdef ENABLE_RGB_MATRIX_SOLID_REACTIVE
        case RGB_MATRIX_SOLID_REACTIVE:
#    endif
#    ifdef ENABLE_RGB_MATRIX_TYPING_HEATMAP
        case RGB_MATRIX_TYPING_HEATMAP:
#    endif
            return true;
        default:
            return false;
    }
}

bool led_layers_is_base(uint8_t mode) {
#    ifdef RGB_MATRIX_EFFECT_VIALRGB_DIRECT
    if (mode == RGB_MATRIX_VIALRGB_DIRECT) {
        return false; // Vial's computer-controlled mode
    }
#    endif
    return mode >= 1 && mode < RGB_MATRIX_EFFECT_MAX && mode != led_layers_mode() && !led_layers_is_react(mode) && !led_layers_is_solo(mode);
}

uint8_t led_layers_mode(void) {
    return RGB_MATRIX_CUSTOM_LAYERED;
}

void led_layers_get(uint8_t *base, uint8_t *react, uint8_t *hue, uint8_t *sat) {
    *base  = base_mode;
    *react = react_mode;
    *hue   = react_hue;
    *sat   = react_sat;
}

void led_layers_set_remote(uint8_t base, uint8_t react, uint8_t hue, uint8_t sat) {
    base_mode  = base;
    react_mode = react;
    react_hue  = hue;
    react_sat  = sat;
}

static bool valid(uint8_t base, uint8_t react) {
    if (base && !led_layers_is_base(base)) {
        return false;
    }
    if (react && !led_layers_is_react(react) && !(led_layers_is_solo(react) && !base)) {
        return false;
    }
    return base || react;
}

static uint8_t checksum(const layers_store_t *s) {
    return LAYERS_EE_MAGIC + s->base + s->react + s->hue + s->sat;
}

static void load(void) {
    layers_store_t s;
    eeconfig_read_kb_datablock(&s, LAYERS_EE_OFFSET, sizeof(s));
    if (s.magic == LAYERS_EE_MAGIC && s.sum == checksum(&s) && valid(s.base, s.react)) {
        led_layers_set_remote(s.base, s.react, s.hue, s.sat);
    } else {
        led_layers_set_remote(0, 0, 0, 0); // key presses in white by default
    }
}

void led_layers_init(void) {
    if (is_keyboard_master()) {
        load();
    }
}

bool led_layers_apply(uint8_t base, uint8_t react, uint8_t hue, uint8_t sat) {
    if (!valid(base, react)) {
        return false;
    }
    led_layers_set_remote(base, react, hue, sat);
    uint8_t mode = base && react ? led_layers_mode() : base ? base : react;
    if (rgb_matrix_get_mode() != mode) {
        rgb_matrix_mode_noeeprom(mode);
    }
    return true;
}

void led_layers_save(void) {
    layers_store_t s = {LAYERS_EE_MAGIC, base_mode, react_mode, react_hue, react_sat, 0};
    s.sum            = checksum(&s);
    eeconfig_update_kb_datablock(&s, LAYERS_EE_OFFSET, sizeof(s));
}

void led_layers_revert(void) {
    load();
}

#endif
