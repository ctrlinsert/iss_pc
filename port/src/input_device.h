/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Input device priority: one device drives the game at a time, the keyboard and mouse or the
 * gamepad in use. The one in use keeps the game while it is used; the other takes over once the
 * one in use has been idle for INPUT_DEVICE_IDLE_US, so a mouse and a stick moved together do not
 * flip between them. "Used" is a key or button held, a stick or trigger past a third, the mouse
 * moved past a few counts or a mouse button held. TLG_INPUT_PRIORITY=0 restores the old mix. */
#ifndef INPUT_DEVICE_H
#define INPUT_DEVICE_H
#include <stdbool.h>
#include <stdint.h>

enum { INPUT_DEVICE_KBM, INPUT_DEVICE_PAD };
/* Mouse motion, in counts per pad read, that counts as using the mouse (a hand resting on it
 * stays below this). */
#define INPUT_DEVICE_MOUSE_COUNTS 6.0f

#define INPUT_DEVICE_IDLE_US 200000u

typedef struct { int active; uint64_t busy_at; } InputDevice;

static inline void input_device_reset(InputDevice *s, int active) { s->active=active; s->busy_at=0; }
/* pad / kbm: whether each device is being used in this read. Returns the device that drives it. */
static inline int input_device_update(InputDevice *s, bool pad, bool kbm, uint64_t now) {
    if (s->active==INPUT_DEVICE_PAD ? pad : kbm) { s->busy_at=now; return s->active; }
    const bool idle=!s->busy_at || now-s->busy_at>=INPUT_DEVICE_IDLE_US;
    if (idle && (s->active==INPUT_DEVICE_PAD ? kbm : pad)) {
        s->active=s->active==INPUT_DEVICE_PAD ? INPUT_DEVICE_KBM : INPUT_DEVICE_PAD;
        s->busy_at=now;
    }
    return s->active;
}
#endif
