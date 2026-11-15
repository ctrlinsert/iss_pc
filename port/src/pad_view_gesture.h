/* SPDX-License-Identifier: GPL-2.0-or-later */
/* View/Select/- on a controller without a touchpad (Xbox, Switch Pro...) as the DualShock 4
 * touchpad (runtime_pad.c). The right stick never swipes on its own: every swipe needs View held,
 * so turning the camera in a fight cannot start a prompt by accident.
 *
 *   - No swipe prompt up when View goes down (the auto-detect, iss_touch_reads.h): View is the
 *     touchpad click and nothing else. A tap clicks, a hold keeps it pressed (draining), View + RB
 *     is two fingers and L1 + View the left click (play_click / play_two_finger). The right stick
 *     stays the camera, so looking around while draining works.
 *   - A swipe prompt up: View puts no click in. Released quickly it is still a click (a tap);
 *     held still past the hold time a finger rests in the middle of the pad (no click, so it
 *     cannot start a drain); a right-stick flick while held swipes in its direction (snapped to
 *     up/down/left/right). The stick has to come from its centre after View went down, so a
 *     stick already pushed for the camera does not count. Keep the stick pushed and the finger
 *     rests at the end of the pad (lifting a mobile command centre, then R2); let it go back to
 *     the centre and it lifts, and another flick swipes again. Meanwhile the right stick is kept
 *     from the camera.
 *
 * Coordinates are 0..1 across the pad (x right, y down); the stick is -1..1 with y down, as SDL
 * gives it. Time is passed in, so tests/test_pad_view_gesture.c checks it without SDL. */
#ifndef ISS_PAD_VIEW_GESTURE_H
#define ISS_PAD_VIEW_GESTURE_H
#include <math.h>
#include <stdint.h>
#include "interact_gesture.h"

#define VIEW_ARM 0.35f    /* the stick counts as centred below this */
#define VIEW_FLICK 0.70f  /* ...and as a flick past this */

typedef struct {
    uint32_t hold_us;  /* still this long at a prompt: the finger rests (default 250 ms) */
    uint32_t swipe_us; /* the finger takes this long to cross the pad (default 250 ms) */
} ViewParams;

enum { VIEW_IDLE, VIEW_PASS, VIEW_DECIDING, VIEW_TAP, VIEW_HOLD, VIEW_SWIPE, VIEW_REST };

typedef struct {
    int state;
    uint64_t since;
    int armed;   /* the stick has been centred since View went down (or since the last swipe) */
    int swiped;  /* a swipe was played in this press: no resting finger in the middle after it */
    int dx, dy;
    int id;
} ViewGesture;

typedef struct {
    int pass;        /* send View on as the touchpad click (the caller's click source) */
    int touch;       /* a finger at x, y */
    float x, y;
    int id;
    int owns_stick;  /* keep the right stick from the camera */
} ViewOut;

static inline ViewParams view_default_params(void) { return (ViewParams){250000u,250000u}; }

static inline void view_begin(ViewGesture *g, int state, uint64_t now) {
    g->state=state; g->since=now;
    if (state==VIEW_SWIPE || state==VIEW_HOLD) g->id=g->id>=250 || g->id<210 ? 210 : g->id+1;
}

/* held: View is down. prompt: a swipe prompt is up (read when View goes down). chord: RB or L1
 * is held (two fingers, the left click), which are clicks. sx, sy: the right stick. */
static inline ViewOut view_update(ViewGesture *g, const ViewParams *p, int held, int prompt, int chord,
                                  float sx, float sy, uint64_t now) {
    ViewOut o={0,0,0.5f,0.5f,g->id,0};
    const float m=sqrtf(sx*sx+sy*sy);
    if (g->state==VIEW_IDLE) {
        if (!held) return o;
        g->armed=m<VIEW_ARM; g->swiped=0; g->dx=g->dy=0;
        view_begin(g,prompt && !chord ? VIEW_DECIDING : VIEW_PASS,now);
    }
    if (g->state==VIEW_PASS) {
        if (!held) { g->state=VIEW_IDLE; return o; }
        o.pass=1; return o;
    }
    if (g->state==VIEW_TAP) { g->state=VIEW_IDLE; return o; } /* one sample of click was enough */
    if (!held && g->state!=VIEW_SWIPE) {
        if (g->state==VIEW_DECIDING && !g->swiped && now-g->since<p->hold_us) {
            g->state=VIEW_TAP; o.pass=1; /* a quick tap is still a click (play_click plays it out) */
            return o;
        }
        g->state=VIEW_IDLE; return o;
    }
    o.owns_stick=1;
    if (!g->armed && m<VIEW_ARM) g->armed=1;
    const int flick=g->armed && m>=VIEW_FLICK;
    if ((g->state==VIEW_DECIDING || g->state==VIEW_HOLD) && flick) {
        interact_snap(sx,sy,&g->dx,&g->dy);
        g->swiped=1; g->armed=0;
        view_begin(g,VIEW_SWIPE,now);
    }
    if (g->state==VIEW_DECIDING && chord) { view_begin(g,VIEW_PASS,now); o.pass=1; o.owns_stick=0; return o; }
    if (g->state==VIEW_DECIDING) {
        if (!g->swiped && now-g->since>=p->hold_us) view_begin(g,VIEW_HOLD,now);
        else return o;
    }
    o.id=g->id;
    const float a=0.5f-INTERACT_REACH, b=0.5f+INTERACT_REACH;
    if (g->state==VIEW_HOLD) { o.touch=1; return o; }
    if (g->state==VIEW_SWIPE) {
        const uint64_t t=now-g->since, slide=p->swipe_us ? p->swipe_us : 1;
        if (t>=INTERACT_LEAD_US+slide) {
            if (held && m>=VIEW_ARM) { view_begin(g,VIEW_REST,now); }
            else if (t>=INTERACT_LEAD_US+slide+INTERACT_TAIL_US) {
                if (held) view_begin(g,VIEW_DECIDING,now); else g->state=VIEW_IDLE;
                return o;
            }
        }
        if (g->state==VIEW_SWIPE) {
            float f=t<=INTERACT_LEAD_US ? 0.0f : (float)(t-INTERACT_LEAD_US)/(float)slide;
            if (f>1.0f) f=1.0f;
            o.touch=1;
            o.x=g->dx ? (g->dx>0 ? a+(b-a)*f : b-(b-a)*f) : 0.5f;
            o.y=g->dy ? (g->dy>0 ? a+(b-a)*f : b-(b-a)*f) : 0.5f;
            return o;
        }
    }
    /* VIEW_REST: the finger at the end of the swipe while View and the stick are held. */
    if (m<VIEW_ARM) { g->armed=1; view_begin(g,VIEW_DECIDING,now); return o; }
    o.touch=1;
    o.x=g->dx ? (g->dx>0 ? b : a) : 0.5f;
    o.y=g->dy ? (g->dy>0 ? b : a) : 0.5f;
    return o;
}
#endif
