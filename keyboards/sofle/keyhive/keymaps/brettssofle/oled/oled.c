// Copyright 2026 Brett-D
// SPDX-License-Identifier: GPL-2.0-or-later
//
// OLED status for the right half (the only OLED on this board); the other
// pages (trackpad, LEDs, info; right knob press) are in oled_pages.c. The right half
// is the split target, so layer, LED, mod and activity state are synced to it
// (see config.h). The left half has no OLED and renders nothing.

#ifdef OLED_ENABLE

#    include "brettssofle.h"
#include "config_mode.h"
#include "oled_pages.h"
#include "display.h"
#include "perf.h"
#include "brett_util.h"
#include "i2c_master.h"
#include "gpio.h"

/* ---- v29: the right OLED's I2C, watched ------------------------------------
 * Brett, v28: "the oled is locked up" after leaving network mode. That one was
 * config mode's own state (config_mode.c: the web session outlived network
 * mode), but a frozen picture can also be the I2C bus: if a transfer is cut
 * off while the OLED drives SDA low, every later one fails and the screen
 * keeps its last picture. QMK's driver only stops and restarts the RP2040's
 * I2C block, which can't free a held SDA. So every OLED transfer is counted
 * here (QMK's weak oled_send_cmd / oled_send_data); after a few failures in a
 * row the bus is cleared (up to 9 SCL pulses until SDA is free, then a STOP)
 * and the OLED re-initialised and redrawn, at most once a second. */
#define OLED_I2C_DATA 0x40
#define OLED_FAILS_TO_RECOVER 3
#define OLED_RECOVER_GAP_MS 1000
static uint8_t  i2c_fails;
static uint16_t i2c_recoveries;
static uint32_t recovered_at;
static bool     recovering;

static void note(bool ok) {
    if (ok) {
        i2c_fails = 0;
    } else if (i2c_fails < 255) {
        i2c_fails++;
    }
}

bool oled_send_cmd(const uint8_t *data, uint16_t size) {
    bool ok = i2c_transmit((OLED_DISPLAY_ADDRESS << 1), data, size, OLED_I2C_TIMEOUT) == I2C_STATUS_SUCCESS;
    note(ok);
    return ok;
}

bool oled_send_data(const uint8_t *data, uint16_t size) {
    bool ok = i2c_write_register((OLED_DISPLAY_ADDRESS << 1), OLED_I2C_DATA, data, size, OLED_I2C_TIMEOUT) == I2C_STATUS_SUCCESS;
    note(ok);
    return ok;
}

uint16_t oled_i2c_recoveries(void) {
    return i2c_recoveries;
}

static void scl_pulse(void) {
    gpio_write_pin_low(I2C1_SCL_PIN);
    gpio_set_pin_output(I2C1_SCL_PIN);
    wait_us(5);
    gpio_set_pin_input_high(I2C1_SCL_PIN); // released: the pull-ups take it high
    wait_us(5);
}

static void i2c_bus_clear(void) {
    gpio_set_pin_input_high(I2C1_SDA_PIN);
    gpio_set_pin_input_high(I2C1_SCL_PIN);
    wait_us(10);
    for (uint8_t i = 0; i < 9 && !gpio_read_pin(I2C1_SDA_PIN); i++) {
        scl_pulse(); // a device mid-byte shifts out its bits and lets SDA go
    }
    // START then STOP (SDA rises while SCL is high): every device back to idle.
    gpio_write_pin_low(I2C1_SDA_PIN);
    gpio_set_pin_output(I2C1_SDA_PIN);
    wait_us(5);
    gpio_set_pin_input_high(I2C1_SDA_PIN);
    wait_us(5);
    palSetLineMode(I2C1_SCL_PIN, I2C1_SCL_PAL_MODE);
    palSetLineMode(I2C1_SDA_PIN, I2C1_SDA_PAL_MODE);
}

static void oled_recover_task(void) {
    if (i2c_fails < OLED_FAILS_TO_RECOVER || recovering || (recovered_at && timer_elapsed32(recovered_at) < OLED_RECOVER_GAP_MS)) {
        return;
    }
    recovering   = true;
    recovered_at = timer_stamp32();
    if (i2c_recoveries < 0xFFFF) {
        i2c_recoveries++;
    }
    i2c_bus_clear();
    i2c_fails = 0;
    if (oled_init(OLED_ROTATION_270)) { // the whole setup again; clears the buffer, so everything is sent
        uint8_t contrast[] = {0x00, 0x81, oled_get_brightness()}; // oled_init set its default contrast
        oled_send_cmd(contrast, sizeof(contrast));
    }
    config_mode_oled_invalidate();
    oled_pages_invalidate();
    recovering = false;
}


static void render_default_layer(void) {
    switch (get_highest_layer(default_layer_state)) {
        case _QWERTY:
            oled_write_P(PSTR("Qwrt "), false);
            break;
        case _COLEMAK:
            oled_write_P(PSTR("Clmk "), false);
            break;
        default:
            oled_write_P(PSTR("Mod  "), false);
            break;
    }
}

static void render_layer(void) {
    oled_write_P(PSTR("LAYER"), false);
    switch (get_highest_layer(layer_state | default_layer_state)) {
        case _QWERTY:
        case _COLEMAK:
            oled_write_P(PSTR("Base "), false);
            break;
        case _LOWER:
            oled_write_P(PSTR("Lower"), false);
            break;
        case _RAISE:
            oled_write_P(PSTR("Raise"), false);
            break;
        case _MOUSE:
            oled_write_P(PSTR("Mouse"), false);
            break;
        case _ADJUST:
            oled_write_P(PSTR("Adjst"), false);
            break;
        default:
            oled_write_P(PSTR("Undef"), false);
            break;
    }
}

static void render_mods(void) {
    uint8_t mods = get_mods() | get_oneshot_mods();
    oled_write_P(PSTR("S"), mods & MOD_MASK_SHIFT);
    oled_write_P(PSTR("C"), mods & MOD_MASK_CTRL);
    oled_write_P(PSTR("A"), mods & MOD_MASK_ALT);
    oled_write_P(PSTR("G"), mods & MOD_MASK_GUI);
    oled_write_P(PSTR(" "), false);
}

static void print_status_narrow(void) {
    oled_write_P(PSTR("\n\n"), false);
    render_default_layer();
    oled_write_P(PSTR("\n\n"), false);
    render_layer();
    oled_write_P(PSTR("\n\n"), false);
    led_t led_state = host_keyboard_led_state();
    oled_write_P(PSTR("CPSLK"), led_state.caps_lock);
    oled_write_P(PSTR("NUMLK"), led_state.num_lock);
    oled_write_P(PSTR("\n"), false);
    render_mods();
}

void oled_right_init(void) {
    // sofle.c only rotates the master's OLED (and ignores oled_init_user), so
    // re-initialise the right half's OLED in portrait for the narrow status.
    if (!is_keyboard_master()) {
        oled_init(OLED_ROTATION_270);
    }
}

static void draw(void) {
    static bool config_shown;
    if (config_mode_oled_task()) {
        config_shown = true;
        return;
    }
    if (config_shown) {
        config_shown = false;
        oled_pages_invalidate(); // v29: the config screen cleared it: the page starts afresh
    }
    if (oled_pages_render()) {
        return;
    }
    print_status_narrow();
    oled_pages_status_footer();
}

bool oled_task_user(void) {
    if (is_keyboard_master()) {
        return false;
    }

    // Activity is synced from the master, so typing on either half or using
    // the trackpad keeps the screen on. Config, network and delivery screens
    // stay on regardless: the page or a long delivery isn't key activity.
    oled_recover_task();
    // v27: the sleep time, brightness and Off come from the master (display.c).
    if (display_oled_off(config_mode_oled_busy(), last_input_activity_elapsed())) {
        oled_off();
        return false;
    }
    oled_on();
    PERF_SPAN(PF_OLED, draw());
    return false;
}

#endif
