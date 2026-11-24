// Copyright 2026 Brett-D
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Trackpad behaviour on top of the QMK Cirque Pinnacle driver (absolute mode):
//
//   tap                  -> left click (sent when the finger lifts)
//   press harder         -> left button pressed when Z (signal strength) crosses
//                           the threshold, held until the finger lifts or Z
//                           drops below the release level (press, then drag)
//   hold DRAG_SCROLL key -> trackpad movement scrolls
//
// Mouse buttons are also on the auto mouse layer (see keymap.c): T/Y = left
// (hold and move to drag), R/U = right.
//
// Optional (off by default, enable in config.h):
//   TRACKPAD_LONG_PRESS_RIGHT_CLICK -> touch and hold still = right click
//
// Cursor glide and circular scroll (slide around the outer ring) come from the
// QMK driver.

#include "brettssofle.h"

#ifndef POINTING_DEVICE_ENABLE
// Trackpad disabled at build time (e.g. POINTING_DEVICE_ENABLE=no for A/B tests).
void trackpad_set_drag_scroll(bool enable) {}
#else

#    ifndef TRACKPAD_TAP_TERM
#        define TRACKPAD_TAP_TERM 200
#    endif
#    ifndef TRACKPAD_LONG_PRESS_TERM
#        define TRACKPAD_LONG_PRESS_TERM 500
#    endif
#    ifndef TRACKPAD_MOVE_THRESHOLD
#        define TRACKPAD_MOVE_THRESHOLD 6
#    endif
#    ifndef TRACKPAD_DRAG_SCROLL_DIVISOR
#        define TRACKPAD_DRAG_SCROLL_DIVISOR 8
#    endif
#    ifndef TRACKPAD_PRESS_DEBOUNCE
#        define TRACKPAD_PRESS_DEBOUNCE 2
#    endif
#    ifndef TRACKPAD_PRESS_MIN_MS
#        define TRACKPAD_PRESS_MIN_MS 30
#    endif
#    ifndef TRACKPAD_PRESS_FREEZE_MS
#        define TRACKPAD_PRESS_FREEZE_MS 60
#    endif

#    ifdef TRACKPAD_DEBUG
#        define tp_dprintf(...) dprintf(__VA_ARGS__)
#    else
#        define tp_dprintf(...) \
            do {                \
            } while (0)
#    endif

#    define BTN_LEFT (1 << POINTING_DEVICE_BUTTON1)
#    define BTN_RIGHT (1 << POINTING_DEVICE_BUTTON2)

static struct {
    bool     touching;
    bool     cancelled;    // moved, scrolled or long-pressed: not a tap
    uint16_t timer;        // start of the current touch
    uint16_t moved;        // movement during the current touch
    uint8_t  clicks_left;  // queued left clicks
    uint8_t  clicks_right; // queued right clicks
    uint8_t  click_down;   // button currently pressed by a queued click
} tp;

static bool    drag_scroll = false;
static int16_t scroll_acc_h, scroll_acc_v;

void trackpad_set_drag_scroll(bool enable) {
    drag_scroll  = enable;
    scroll_acc_h = 0;
    scroll_acc_v = 0;
}

static inline uint16_t tp_abs(int16_t v) {
    return v < 0 ? -v : v;
}

/* ---------------------------------------------------------------------------
 * Press-to-click: Z (signal strength) above a threshold holds the left button.
 * ------------------------------------------------------------------------ */

static struct {
    bool     down;
    bool     blocked; // ring scroll during this touch
    uint8_t  above;   // consecutive polls at/above the threshold
    uint16_t down_timer;
    bool     freeze;
    uint16_t freeze_timer;
} press;

static void tp_press_set(bool down) {
    press.down         = down;
    press.down_timer   = timer_read();
    press.freeze       = true; // a hard press/release can nudge the finger
    press.freeze_timer = timer_read();
    tp_dprintf("trackpad: press %s\n", down ? "down" : "up");
}

static void tp_press_step(bool touch, uint8_t z, bool scrolling) {
    if (!touch) {
        press.blocked = false;
        press.above   = 0;
        if (press.down) {
            tp_press_set(false);
        }
        return;
    }
    if (scrolling && !press.down) {
        press.blocked = true;
    }
    if (!trackpad_press_enabled() || press.blocked) {
        press.above = 0;
        if (press.down) {
            tp_press_set(false);
        }
        return;
    }

    uint8_t threshold = trackpad_press_threshold();
    if (!press.down) {
        if (z < threshold) {
            press.above = 0;
        } else if (++press.above >= TRACKPAD_PRESS_DEBOUNCE) {
            tp.cancelled = true; // a press is not also a tap on lift
            tp_press_set(true);
        }
    } else if (z < trackpad_press_release_z() && timer_elapsed(press.down_timer) >= TRACKPAD_PRESS_MIN_MS) {
        tp_press_set(false);
        press.above = 0; // the next press needs TRACKPAD_PRESS_DEBOUNCE high samples again
    }
}

static void tp_gesture_step(bool touch, uint16_t motion, bool wheel) {
    if (touch && !tp.touching) {
        tp.touching  = true;
        tp.cancelled = false;
        tp.timer     = timer_read();
        tp.moved     = 0;
        return;
    }
    if (!tp.touching) {
        return;
    }

    tp.moved += motion;
    if (tp.moved > TRACKPAD_MOVE_THRESHOLD || wheel) { // any ring scroll: not a tap
        tp.cancelled = true;
    }

    if (touch) {
#    ifdef TRACKPAD_LONG_PRESS_RIGHT_CLICK
        if (!tp.cancelled && timer_elapsed(tp.timer) >= TRACKPAD_LONG_PRESS_TERM) {
            tp.clicks_right++;
            tp.cancelled = true;
            tp_dprintf("trackpad: long press -> right click\n");
        }
#    endif
        return;
    }

    tp.touching = false;
    if (!tp.cancelled && timer_elapsed(tp.timer) < TRACKPAD_TAP_TERM) {
        tp.clicks_left++;
        tp_dprintf("trackpad: tap -> left click\n");
    }
}

// Emits queued clicks as press/release pairs over consecutive reports.
static uint8_t tp_click_step(void) {
    if (tp.click_down) {
        tp.click_down = 0;
    } else if (tp.clicks_left) {
        tp.clicks_left--;
        tp.click_down = BTN_LEFT;
    } else if (tp.clicks_right) {
        tp.clicks_right--;
        tp.click_down = BTN_RIGHT;
    }
    return tp.click_down;
}

static void tp_drag_scroll(report_mouse_t *mouse_report) {
    scroll_acc_h += mouse_report->x;
    scroll_acc_v -= mouse_report->y;
    mouse_report->x = 0;
    mouse_report->y = 0;

    int16_t h = scroll_acc_h / TRACKPAD_DRAG_SCROLL_DIVISOR;
    int16_t v = scroll_acc_v / TRACKPAD_DRAG_SCROLL_DIVISOR;
    scroll_acc_h -= h * TRACKPAD_DRAG_SCROLL_DIVISOR;
    scroll_acc_v -= v * TRACKPAD_DRAG_SCROLL_DIVISOR;
    mouse_report->h = h;
    mouse_report->v = v;
}

void pointing_device_init_user(void) {
#    ifdef TRACKPAD_DEBUG
    debug_enable = true;
    debug_mouse  = true;
#    endif
    set_auto_mouse_enable(true);
}

report_mouse_t pointing_device_task_user(report_mouse_t mouse_report) {
    // With POINTING_DEVICE_AUTO_MOUSE_ENABLE the Cirque driver reports finger
    // contact through auto_mouse_activation(); an empty report isolates it.
    bool     touch  = auto_mouse_activation((report_mouse_t){0});
    uint16_t motion = tp_abs(mouse_report.x) + tp_abs(mouse_report.y) + tp_abs(mouse_report.h) + tp_abs(mouse_report.v);

    tp_gesture_step(touch, motion, mouse_report.h != 0 || mouse_report.v != 0);
    uint8_t z = trackpad_z_update(touch);
    // Ring scroll reports wheel motion; don't let a firm press click mid-scroll.
    tp_press_step(touch, z, mouse_report.h != 0 || mouse_report.v != 0);
    trackpad_z_set_pressed(press.down);

    if (press.freeze) {
        if (timer_elapsed(press.freeze_timer) < TRACKPAD_PRESS_FREEZE_MS) {
            mouse_report.x = 0;
            mouse_report.y = 0;
        } else {
            press.freeze = false;
        }
    }

    static uint8_t owned = 0;
    uint8_t        out   = tp_click_step() | (press.down ? BTN_LEFT : 0);
    // Only clear buttons this module pressed; mouse keys share the same bits.
    mouse_report.buttons = (mouse_report.buttons & ~(owned & ~out)) | out;
    owned                = out;

    if (drag_scroll) {
        tp_drag_scroll(&mouse_report);
    }
    return mouse_report;
}

bool is_mouse_record_user(uint16_t keycode, keyrecord_t *record) {
    // Keep the auto mouse layer active while DRAG_SCROLL is used.
    return keycode == DRAG_SCROLL;
}

#endif // POINTING_DEVICE_ENABLE
