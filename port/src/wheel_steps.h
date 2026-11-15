/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Mouse wheel notches as d-pad steps, counted in the game's pad reads (runtime_pad.c).
 * The game updates its controller once per frame (US 1.07 0x24a2c0): it latches a press on each
 * up-to-down edge between two updates, and the menu scripts consume that latch
 * (IS_BUTTON_PRESSED). Some queries also need the button still down when the script asks, and
 * a held button is level-read, so a script may repeat. One notch is therefore the button down
 * for WHEEL_STEP_DOWN reads, then up for WHEEL_STEP_UP reads: one edge, one step, never a hold.
 * Notches queue up to WHEEL_STEP_QUEUE per axis, so a fast spin does not keep scrolling after
 * the wheel stops, and turning the wheel the other way drops the queue and the current press. */
#ifndef WHEEL_STEPS_H
#define WHEEL_STEPS_H
enum { WHEEL_STEP_DOWN = 2, WHEEL_STEP_UP = 1, WHEEL_STEP_QUEUE = 2 };
typedef struct { int pending, dir, down_left, up_left; } WheelAxis;
/* negative/positive: notches since the last read. Returns the direction held this read
 * (-1, 0 or +1). */
static inline int wheel_axis_read(WheelAxis *a, int negative, int positive) {
    const int net = positive - negative;
    if (net) {
        const int dir = net > 0 ? 1 : -1;
        const int busy = a->down_left ? a->dir : 0;
        if ((a->pending && (a->pending > 0) != (dir > 0)) || (busy && busy != dir)) {
            a->pending = 0;
            if (busy && busy != dir) { a->down_left = 0; a->up_left = WHEEL_STEP_UP; }
        }
        a->pending += net;
        if (a->pending > WHEEL_STEP_QUEUE) a->pending = WHEEL_STEP_QUEUE;
        if (a->pending < -WHEEL_STEP_QUEUE) a->pending = -WHEEL_STEP_QUEUE;
    }
    if (a->down_left) {
        if (--a->down_left == 0) a->up_left = WHEEL_STEP_UP;
        return a->dir;
    }
    if (a->up_left) { --a->up_left; return 0; }
    if (a->pending) {
        a->dir = a->pending > 0 ? 1 : -1;
        a->pending -= a->dir;
        a->down_left = WHEEL_STEP_DOWN - 1;
        if (!a->down_left) a->up_left = WHEEL_STEP_UP;
        return a->dir;
    }
    return 0;
}
#endif
