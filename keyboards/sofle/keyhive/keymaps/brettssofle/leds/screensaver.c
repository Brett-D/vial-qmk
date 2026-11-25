// Copyright 2026 Brett-D
// SPDX-License-Identifier: GPL-2.0-or-later
//
// v27 LED screensaver. Brett: "could we also add a led mode to switch too if
// im not using the keyboard for a certain time like a screen saver?"
//
// After "Screensaver after" without a key, knob or trackpad input, the LEDs
// fade (1 s) to one of our own backgrounds with its own colour, brightness
// and speed, and fade back on the next input (that key is still typed). The
// existing "Turn off after" still counts from the last input: if it comes
// first, there is no screensaver. Not in config mode, LED calibration, typed
// delivery, network mode or a page session (the page's Preview shows it).
//
// The master decides (it sees all input); the target and the settings go to
// the right half in the lighting RPC (led_fx.c), and each half fades on its
// own clock, so they go together. It is mixed into the frame at flush
// (rgb_driver.c), whatever the normal lighting is, under the key-press overlay
// and (if allowed) the pet's reactions; the output curve and cap apply as usual.

#include QMK_KEYBOARD_H
#include "kb_config.h"
#include "led_fx.h"
#include "led_fx_core.h"
#include "config_mode.h"
#include "user_config.h"
#include "brett_util.h"
#include "screensaver.h"

#ifdef RGB_MATRIX_ENABLE

#    define SS_PREVIEW_MS 10000

static ss_settings_t cur; // master: live (previewed) settings; right half: the master's
static bool          remote_on; // right half: the master's target
static bool          preview;
static uint32_t      preview_at, preview_pd;

static void from_config(void) {
    kb_config_t c;
    kb_config_get(&c);
    cur = (ss_settings_t){.after = c.ss_after, .effect = c.ss_effect, .pet = c.ss_pet, .hue = c.ss_hue, .sat = c.ss_sat, .val = c.ss_val, .speed = c.ss_speed};
}

void screensaver_load(void) {
    from_config();
}

void screensaver_save(void) {
    kb_config_t c;
    kb_config_get(&c);
    c.ss_after  = cur.after;
    c.ss_effect = cur.effect;
    c.ss_pet    = cur.pet;
    c.ss_hue    = cur.hue;
    c.ss_sat    = cur.sat;
    c.ss_val    = cur.val;
    c.ss_speed  = cur.speed;
    kb_config_set(&c);
    from_config(); // as stored (the hue in steps of 8)
}

void screensaver_get(ss_settings_t *s) {
    *s = cur;
}

bool screensaver_set(const ss_settings_t *s) {
    if (s->after >= SS_AFTER_CHOICES || s->effect >= FX_OWN_BG_COUNT || s->sat >= SS_SAT_LEVELS || s->val >= SS_VAL_LEVELS || s->speed >= SS_SPEED_LEVELS) {
        return false;
    }
    cur     = *s;
    cur.hue = s->hue / SS_HUE_STEP * SS_HUE_STEP;
    return true;
}

void screensaver_preview(bool on) {
    preview    = on;
    preview_at = timer_read32();
    preview_pd = last_pointing_device_activity_time();
}

void screensaver_input(void) {
    preview = false;
}

bool screensaver_wanted(void) {
    if (preview) {
        if (timer_elapsed32(preview_at) > SS_PREVIEW_MS || last_pointing_device_activity_time() != preview_pd || config_mode_leds_quiet(false)) {
            preview = false;
        } else {
            return true;
        }
    }
    uint32_t after = (uint32_t)ss_after_minutes(cur.after) * 60000;
    if (!after || config_mode_leds_quiet(true)) {
        return false;
    }
    uint32_t off  = user_config_rgb_idle_ms();
    uint32_t idle = last_input_activity_elapsed();
    return idle >= after && (!off || off > after);
}

/* ---- the lighting RPC ---- */

void screensaver_pack(uint8_t p[SS_RPC_LEN]) {
    p[0] = (screensaver_wanted() ? 1 : 0) | (cur.pet ? 2 : 0);
    p[1] = cur.effect;
    p[2] = cur.hue;
    p[3] = cur.sat;
    p[4] = cur.val;
    p[5] = cur.speed;
    p[6] = cur.after;
}

void screensaver_unpack(const uint8_t p[SS_RPC_LEN]) {
    ss_settings_t s = {.after = p[6], .effect = p[1], .pet = p[0] & 2, .hue = p[2], .sat = p[3], .val = p[4], .speed = p[5]};
    if (screensaver_set(&s)) {
        remote_on = p[0] & 1;
    }
}

/* ---- drawing (both halves) ---- */

static uint16_t    level; // 0 - FX_SS_FULL
static uint32_t    last_t;
static fx_bg_ctx_t ctx;
static bool        ug_dark;

void screensaver_blank(void) {
    level = 0;
}

bool screensaver_pet_ok(void) {
    return !level || cur.pet;
}

uint16_t screensaver_frame_begin(bool calibrating) {
    uint32_t now = timer_read32();
    uint32_t dt  = last_t ? now - last_t : 0;
    last_t       = now;
    bool want    = !calibrating && (is_keyboard_master() ? screensaver_wanted() : remote_on);
    level        = fx_ss_step(level, dt > 100 ? 100 : (uint16_t)dt, want);
    uint16_t k   = fx_ss_share(level);
    if (!k) {
        return 0;
    }
    ctx     = (fx_bg_ctx_t){.t = g_rgb_timer, .hue = cur.hue, .sat = ss_sat_value(cur.sat), .val = (uint8_t)((uint16_t)ss_val_percent(cur.val) * RGB_MATRIX_MAXIMUM_BRIGHTNESS / 100), .spd = ss_speed_value(cur.speed)};
    ug_dark = config_mode_underglow_dark();
    return k;
}

void screensaver_pixel(uint8_t led, uint8_t rgb[3], uint16_t mix) {
    led_flags_t f = g_led_config.flags[led];
    if (f == LED_FLAG_NONE || (ug_dark && HAS_FLAGS(f, LED_FLAG_UNDERGLOW))) {
        return;
    }
    fx_hsv_t o     = led_fx_bg_at(FX_OWN_BG + cur.effect, &ctx, g_led_config.point[led].x, g_led_config.point[led].y);
    rgb_t    c     = hsv_to_rgb((hsv_t){o.h, o.s, o.v});
    uint8_t  ss[3] = {c.r, c.g, c.b};
    fx_ss_mix(rgb, ss, mix);
}

#endif
