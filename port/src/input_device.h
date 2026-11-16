/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Input device priority: one device drives the game at a time. The keyboard and mouse, or the
 * gamepad in use, whichever was used last. A device takes over when input starts on it (a key or
 * button goes down, a stick or the mouse starts moving) and not on the other in the same read, so
 * a key still held, a resting stick or a slight desk bump does not pull the game back. The other
 * device is ignored until it is used. TLG_INPUT_PRIORITY=0 restores the old mix (both at once). */
#ifndef INPUT_DEVICE_H
#define INPUT_DEVICE_H
#include <stdbool.h>

enum { INPUT_DEVICE_KBM, INPUT_DEVICE_PAD };
/* Mouse motion, in counts per pad read, that counts as using the mouse (a hand resting on it
 * stays below this). */
#define INPUT_DEVICE_MOUSE_COUNTS 6.0f

typedef struct { int active; } InputDevice;

static inline void input_device_reset(InputDevice *s, int active) { s->active=active; }
/* pad / kbm: whether input started on each device in this read (runtime_pad.c tracks the
 * controls). Returns the device that drives this read. */
static inline int input_device_update(InputDevice *s, bool pad, bool kbm) {
    if (pad && !kbm) s->active=INPUT_DEVICE_PAD;
    else if (kbm && !pad) s->active=INPUT_DEVICE_KBM;
    return s->active;
}
#endif
