// Copyright 2026 Brett-D
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Trackpad gestures for the Cirque Pinnacle (absolute mode, one finger):
//
//   tap                      -> left click (sent after TRACKPAD_TAP_DRAG_TERM)
//   tap, then touch and hold -> left button held while the finger is down
//                               (drag windows / select text). Lifting and
//                               touching again within TRACKPAD_DRAG_RELEASE_DELAY
//                               keeps dragging.
//   double tap               -> double click
//   touch and hold still     -> right click after TRACKPAD_LONG_PRESS_TERM
//   hold DRAG_SCROLL key     -> trackpad movement scrolls
//
// Cursor glide and circular scroll from the QMK driver keep working.

#include "brettssofle.h"

#ifndef TRACKPAD_TAP_TERM
#    define TRACKPAD_TAP_TERM 200
#endif
#ifndef TRACKPAD_TAP_DRAG_TERM
#    define TRACKPAD_TAP_DRAG_TERM 200
#endif
#ifndef TRACKPAD_LONG_PRESS_TERM
#    define TRACKPAD_LONG_PRESS_TERM 500
#endif
#ifndef TRACKPAD_DRAG_RELEASE_DELAY
#    define TRACKPAD_DRAG_RELEASE_DELAY 300
#endif
#ifndef TRACKPAD_MOVE_THRESHOLD
#    define TRACKPAD_MOVE_THRESHOLD 6
#endif
#ifndef TRACKPAD_DRAG_SCROLL_DIVISOR
#    define TRACKPAD_DRAG_SCROLL_DIVISOR 8
#endif

#ifdef TRACKPAD_DEBUG
#    define tp_dprintf(...) dprintf(__VA_ARGS__)
#else
#    define tp_dprintf(...) \
        do {                \
        } while (0)
#endif

#define BTN_LEFT (1 << POINTING_DEVICE_BUTTON1)
#define BTN_RIGHT (1 << POINTING_DEVICE_BUTTON2)

typedef enum {
    TP_IDLE,
    TP_TOUCH,       // first touch of a possible tap / long press
    TP_TAP_PENDING, // tapped once; waiting to see if a drag or second tap follows
    TP_DRAG,        // left button held while the finger is down
    TP_DRAG_GRACE,  // finger lifted mid-drag; button still held briefly
} tp_state_t;

static struct {
    tp_state_t state;
    uint16_t   timer;
    uint16_t   moved;
    bool       long_press_fired;
    bool       drag_moved;   // the current drag has moved or been resumed
    uint8_t    held;         // buttons held by a gesture
    uint8_t    clicks_left;  // queued left clicks
    uint8_t    clicks_right; // queued right clicks
    uint8_t    click_down;   // button currently pressed by a queued click
    uint8_t    owned;        // buttons this module set in the last report
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

static void tp_start_touch(void) {
    tp.timer            = timer_read();
    tp.moved            = 0;
    tp.long_press_fired = false;
}

static void tp_set_state(tp_state_t state) {
    tp_dprintf("trackpad: state %u -> %u\n", tp.state, state);
    tp.state = state;
}

static void tp_gesture_step(bool touch, uint16_t motion) {
    switch (tp.state) {
        case TP_IDLE:
            if (touch) {
                tp_start_touch();
                tp_set_state(TP_TOUCH);
            }
            break;

        case TP_TOUCH:
            tp.moved += motion;
            if (touch) {
                if (!tp.long_press_fired && tp.moved <= TRACKPAD_MOVE_THRESHOLD && timer_elapsed(tp.timer) >= TRACKPAD_LONG_PRESS_TERM) {
                    tp.clicks_right++;
                    tp.long_press_fired = true;
                    tp_dprintf("trackpad: long press -> right click\n");
                }
            } else if (!tp.long_press_fired && tp.moved <= TRACKPAD_MOVE_THRESHOLD && timer_elapsed(tp.timer) < TRACKPAD_TAP_TERM) {
                tp.timer = timer_read();
                tp_set_state(TP_TAP_PENDING);
            } else {
                tp_set_state(TP_IDLE);
            }
            break;

        case TP_TAP_PENDING:
            if (touch) {
                tp.held |= BTN_LEFT;
                tp.drag_moved = false;
                tp_start_touch();
                tp_set_state(TP_DRAG);
            } else if (timer_elapsed(tp.timer) >= TRACKPAD_TAP_DRAG_TERM) {
                tp.clicks_left++;
                tp_set_state(TP_IDLE);
            }
            break;

        case TP_DRAG:
            tp.moved += motion;
            if (tp.moved > TRACKPAD_MOVE_THRESHOLD) {
                tp.drag_moved = true;
            }
            if (!touch) {
                if (!tp.drag_moved && timer_elapsed(tp.timer) < TRACKPAD_TAP_TERM) {
                    // Second quick tap: finish the first click, add a second.
                    tp.held &= ~BTN_LEFT;
                    tp.clicks_left++;
                    tp_set_state(TP_IDLE);
                } else if (TRACKPAD_DRAG_RELEASE_DELAY > 0) {
                    tp.timer = timer_read();
                    tp_set_state(TP_DRAG_GRACE);
                } else {
                    tp.held &= ~BTN_LEFT;
                    tp_set_state(TP_IDLE);
                }
            }
            break;

        case TP_DRAG_GRACE:
            if (touch) {
                tp_start_touch();
                tp.drag_moved = true;
                tp_set_state(TP_DRAG);
            } else if (timer_elapsed(tp.timer) >= TRACKPAD_DRAG_RELEASE_DELAY) {
                tp.held &= ~BTN_LEFT;
                tp_set_state(TP_IDLE);
            }
            break;
    }
}

// Emits queued clicks as press/release pairs over consecutive reports.
static uint8_t tp_click_step(uint8_t prev_out) {
    if (tp.click_down) {
        tp.click_down = 0;
    } else if (tp.clicks_left && !(prev_out & BTN_LEFT)) {
        tp.clicks_left--;
        tp.click_down = BTN_LEFT;
    } else if (tp.clicks_right && !(prev_out & BTN_RIGHT)) {
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
#ifdef TRACKPAD_DEBUG
    debug_enable = true;
    debug_mouse  = true;
#endif
    set_auto_mouse_enable(true);
}

report_mouse_t pointing_device_task_user(report_mouse_t mouse_report) {
    // With POINTING_DEVICE_AUTO_MOUSE_ENABLE the Cirque driver reports finger
    // contact through auto_mouse_activation(); an empty report isolates it.
    bool     touch  = auto_mouse_activation((report_mouse_t){0});
    uint16_t motion = tp_abs(mouse_report.x) + tp_abs(mouse_report.y) + tp_abs(mouse_report.h) + tp_abs(mouse_report.v);

    tp_gesture_step(touch, motion);

    uint8_t out = tp.held | tp_click_step(tp.owned);
    // Only clear buttons this module pressed; mouse keys share the same bits.
    mouse_report.buttons = (mouse_report.buttons & ~(tp.owned & ~out)) | out;
    tp.owned             = out;

    if (drag_scroll) {
        tp_drag_scroll(&mouse_report);
    }
    return mouse_report;
}

bool is_mouse_record_user(uint16_t keycode, keyrecord_t *record) {
    // Keep the auto mouse layer active while DRAG_SCROLL is used.
    return keycode == DRAG_SCROLL;
}
