/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Input device priority. By default both devices work together, and only controls that would
 * fight take turns: moving (the left stick against the movement keys) and the camera (the right
 * stick against the mouse and the camera keys). On each, the one that started being used last
 * wins while both are in use (InputChannel). With strict priority on (tlg.ini input_strict, the
 * Insert menu, TLG_INPUT_STRICT=1) one device drives the game at a time (InputDevice): the one
 * in use keeps it until it has been idle a moment. TLG_INPUT_PRIORITY=0 turns both off (mixed). */
#ifndef INPUT_DEVICE_H
#define INPUT_DEVICE_H
#include <stdbool.h>
#include <stdint.h>

enum { INPUT_DEVICE_KBM, INPUT_DEVICE_PAD };
/* Mouse motion, in counts per pad read, that counts as using the mouse (a hand resting on it
 * stays below this). */
#define INPUT_DEVICE_MOUSE_COUNTS 6.0f
/* Once in use, slower motion (at least this many counts a read) keeps it in use, and it stays in
 * use this long after its last such motion. */
#define INPUT_DEVICE_MOUSE_SLOW 1.0f
#define INPUT_DEVICE_MOUSE_LINGER_US 150000u

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

/* One control two devices can drive: the one that started being used last wins while both are
 * in use; one alone always has it. */
typedef struct { int owner; bool pad_was, kbm_was; } InputChannel;
static inline int input_channel_update(InputChannel *c, bool pad, bool kbm) {
    const bool pad_start=pad && !c->pad_was, kbm_start=kbm && !c->kbm_was;
    if (pad && !kbm) c->owner=INPUT_DEVICE_PAD;
    else if (kbm && !pad) c->owner=INPUT_DEVICE_KBM;
    else if (pad_start && !kbm_start) c->owner=INPUT_DEVICE_PAD;
    else if (kbm_start && !pad_start) c->owner=INPUT_DEVICE_KBM;
    c->pad_was=pad; c->kbm_was=kbm;
    return c->owner;
}
#endif
