// Copyright 2026 Brett-D
// SPDX-License-Identifier: GPL-2.0-or-later
//
// LAYERED: the whole-keyboard effect, then the key-press effect drawn on top
// in its own colour (led_layers.c). This file is part of rgb_matrix.c, so the
// built-in effect functions are callable here.

RGB_MATRIX_EFFECT(LAYERED)

#ifdef RGB_MATRIX_CUSTOM_EFFECT_IMPLS
#    include "led_layers.h"

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
        default: {
            // No whole-keyboard effect: dark underneath.
            RGB_MATRIX_USE_LIMITS(led_min, led_max);
            for (uint8_t i = led_min; i < led_max; i++) {
                rgb_matrix_set_color(i, 0, 0, 0);
            }
            return rgb_matrix_check_finished_leds(led_max);
        }
    }
}

static bool LAYERED(effect_params_t *params) {
    uint8_t base, react, hue, sat;
    led_layers_get(&base, &react, &hue, &sat);
    bool more = layered_run(base, params);
    if (react) {
        // The key-press layer in its own colour, blended over the base.
        hsv_t saved            = rgb_matrix_config.hsv;
        rgb_matrix_config.hsv.h = hue;
        rgb_matrix_config.hsv.s = sat;
        rgb_driver_overlay(true);
        layered_run(react, params);
        rgb_driver_overlay(false);
        rgb_matrix_config.hsv = saved;
    }
    return more;
}

#endif
