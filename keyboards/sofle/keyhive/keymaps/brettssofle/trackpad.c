// Copyright 2026 Brett-D
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Trackpad behaviour on top of the QMK Cirque Pinnacle driver (absolute mode):
//
//   tap                  -> left click (sent when the finger lifts)
//   hold DRAG_SCROLL key -> trackpad movement scrolls
//
// Clicking and dragging is done with keys on the auto mouse layer (see
// keymap.c): hold T and move to drag, R is right click.
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

static void tp_gesture_step(bool touch, uint16_t motion) {
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
    if (tp.moved > TRACKPAD_MOVE_THRESHOLD) {
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

    tp_gesture_step(touch, motion);
#    ifdef TRACKPAD_Z_DEBUG
    trackpad_z_update(touch);
#    endif

    static uint8_t owned = 0;
    uint8_t        out   = tp_click_step();
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
