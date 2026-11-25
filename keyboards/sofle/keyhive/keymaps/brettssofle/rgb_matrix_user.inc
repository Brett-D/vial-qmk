// Copyright 2026 Brett-D
// SPDX-License-Identifier: GPL-2.0-or-later
//
// LAYERED: the background (one of QMK's effects, or ours from led_fx_core.h);
// the key-press overlay is composited over it at flush (led_fx.c,
// rgb_driver.c). This file is part of rgb_matrix.c, so the built-in effect
// functions are callable here.

RGB_MATRIX_EFFECT(LAYERED)

#ifdef RGB_MATRIX_CUSTOM_EFFECT_IMPLS

bool led_fx_background(effect_params_t *params); // led_fx.c: our own backgrounds (or dark)
#    include "led_fx.h"

#    define LAYER_EFFECT(name) \
        case RGB_MATRIX_##name: \
            return name(params);

static bool layered_run(uint8_t mode, effect_params_t *params) {
    switch (mode) {
        LAYER_EFFECT(SOLID_COLOR)
#    ifdef ENABLE_RGB_MATRIX_ALPHAS_MODS
        LAYER_EFFECT(ALPHAS_MODS)
#    endif
#    ifdef ENABLE_RGB_MATRIX_GRADIENT_LEFT_RIGHT
        LAYER_EFFECT(GRADIENT_LEFT_RIGHT)
#    endif
#    ifdef ENABLE_RGB_MATRIX_BREATHING
        LAYER_EFFECT(BREATHING)
#    endif
#    ifdef ENABLE_RGB_MATRIX_CYCLE_ALL
        LAYER_EFFECT(CYCLE_ALL)
#    endif
#    ifdef ENABLE_RGB_MATRIX_CYCLE_LEFT_RIGHT
        LAYER_EFFECT(CYCLE_LEFT_RIGHT)
#    endif
#    ifdef ENABLE_RGB_MATRIX_CYCLE_OUT_IN
        LAYER_EFFECT(CYCLE_OUT_IN)
#    endif
#    ifdef ENABLE_RGB_MATRIX_RAINBOW_MOVING_CHEVRON
        LAYER_EFFECT(RAINBOW_MOVING_CHEVRON)
#    endif
#    ifdef ENABLE_RGB_MATRIX_CYCLE_PINWHEEL
        LAYER_EFFECT(CYCLE_PINWHEEL)
#    endif
#    ifdef ENABLE_RGB_MATRIX_CYCLE_SPIRAL
        LAYER_EFFECT(CYCLE_SPIRAL)
#    endif
#    ifdef ENABLE_RGB_MATRIX_RAINBOW_BEACON
        LAYER_EFFECT(RAINBOW_BEACON)
#    endif
#    ifdef ENABLE_RGB_MATRIX_RAINDROPS
        LAYER_EFFECT(RAINDROPS)
#    endif
#    ifdef ENABLE_RGB_MATRIX_JELLYBEAN_RAINDROPS
        LAYER_EFFECT(JELLYBEAN_RAINDROPS)
#    endif
#    ifdef ENABLE_RGB_MATRIX_HUE_WAVE
        LAYER_EFFECT(HUE_WAVE)
#    endif
#    ifdef ENABLE_RGB_MATRIX_PIXEL_RAIN
        LAYER_EFFECT(PIXEL_RAIN)
#    endif
#    ifdef ENABLE_RGB_MATRIX_DIGITAL_RAIN
        LAYER_EFFECT(DIGITAL_RAIN)
#    endif
#    ifdef ENABLE_RGB_MATRIX_SOLID_REACTIVE_SIMPLE
        LAYER_EFFECT(SOLID_REACTIVE_SIMPLE)
#    endif
#    ifdef ENABLE_RGB_MATRIX_SOLID_REACTIVE_WIDE
        LAYER_EFFECT(SOLID_REACTIVE_WIDE)
#    endif
#    ifdef ENABLE_RGB_MATRIX_SOLID_REACTIVE_MULTIWIDE
        LAYER_EFFECT(SOLID_REACTIVE_MULTIWIDE)
#    endif
#    ifdef ENABLE_RGB_MATRIX_SOLID_REACTIVE_CROSS
        LAYER_EFFECT(SOLID_REACTIVE_CROSS)
#    endif
#    ifdef ENABLE_RGB_MATRIX_SOLID_REACTIVE_MULTICROSS
        LAYER_EFFECT(SOLID_REACTIVE_MULTICROSS)
#    endif
#    ifdef ENABLE_RGB_MATRIX_SOLID_REACTIVE_NEXUS
        LAYER_EFFECT(SOLID_REACTIVE_NEXUS)
#    endif
#    ifdef ENABLE_RGB_MATRIX_SOLID_REACTIVE_MULTINEXUS
        LAYER_EFFECT(SOLID_REACTIVE_MULTINEXUS)
#    endif
#    ifdef ENABLE_RGB_MATRIX_SPLASH
        LAYER_EFFECT(SPLASH)
#    endif
#    ifdef ENABLE_RGB_MATRIX_MULTISPLASH
        LAYER_EFFECT(MULTISPLASH)
#    endif
#    ifdef ENABLE_RGB_MATRIX_SOLID_SPLASH
        LAYER_EFFECT(SOLID_SPLASH)
#    endif
#    ifdef ENABLE_RGB_MATRIX_SOLID_MULTISPLASH
        LAYER_EFFECT(SOLID_MULTISPLASH)
#    endif
        default:
            return led_fx_background(params); // ours, or dark
    }
}

static bool LAYERED(effect_params_t *params) {
    led_fx_layered_drawn(); // this frame may get the key-press overlay (not a suspend / timeout blackout)
    // A new background inside LAYERED starts like a new effect (QMK only does
    // that when the mode changes): init for one whole frame (all render passes).
    static uint8_t last_base = 0xFF, reinit;
    uint8_t        base      = led_fx_base();
    if (base != last_base) {
        last_base = base;
        reinit    = 2;
    }
    if (reinit && params->iter == 0) {
        reinit--;
    }
    if (reinit) {
        params->init = true;
    }
    return layered_run(base, params);
}

#endif
