/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The interact key (runtime_pad.c): one key for everything the DualShock 4 touchpad does with
 * one finger, so keyboard and mouse players need no chord.
 *
 *   - tap (released before the hold time, no motion): a touchpad click;
 *   - hold still past the hold time: the finger lands and the pad stays pressed while the key is
 *     held (absorbing/draining powers, "hold the touchpad" prompts);
 *   - hold and move the mouse past the threshold, or press a direction (W/A/S/D, the arrows, a
 *     stick), before the hold starts: a full swipe in that direction, played on its own (finger
 *     down, slide across the pad, lift). The direction snaps to up/down/left/right, as the game's
 *     swipes are. After a swipe the key does nothing more until it is released.
 *
 * While a gesture runs (`owns`), the caller keeps the mouse and the direction keys from turning
 * the camera or walking. Coordinates are 0..1 across the pad (x right, y down). Time is passed
 * in, so tests/test_interact_gesture.c checks it without SDL or the runtime. */
#ifndef ISS_INTERACT_GESTURE_H
#define ISS_INTERACT_GESTURE_H
#include <math.h>
#include <stdint.h>

#define INTERACT_LEAD_US 50000u   /* finger down before the click, as play_click */
#define INTERACT_CLICK_US 50000u  /* a tap's click */
#define INTERACT_TAIL_US 100000u  /* finger stays after the click */
#define INTERACT_REACH 0.42f      /* a swipe runs from 0.5-REACH to 0.5+REACH */

typedef struct {
    float threshold;  /* mouse counts that make a hold a swipe (default 30) */
    uint32_t hold_us; /* how long the key must stay still to be a hold (default 250 ms) */
    uint32_t swipe_us;/* how long the finger takes to cross the pad (default 180 ms) */
} InteractParams;

enum { INTERACT_IDLE, INTERACT_DECIDING, INTERACT_TAP, INTERACT_HOLD, INTERACT_SWIPE, INTERACT_SPENT };

typedef struct {
    int state;
    uint64_t since;   /* when the current state began */
    float ax, ay;     /* mouse motion gathered while deciding */
    int dx, dy;       /* swipe direction (one of them is 0) */
    int id;           /* touch id of the current gesture, grows with each one */
} InteractGesture;

typedef struct {
    int touch, click; /* the game sees a finger, and the touchpad pressed */
    float x, y;       /* the finger, 0..1 */
    int id;
    int owns;         /* the key is in use: keep the camera and the character still */
} InteractOut;

static inline InteractParams interact_default_params(void) {
    return (InteractParams){30.0f,250000u,180000u};
}

/* Snaps a motion to its main axis: -1/0/+1 each, one of them 0. */
static inline void interact_snap(float x, float y, int *dx, int *dy) {
    if (fabsf(x)>=fabsf(y)) { *dx=x<0 ? -1 : 1; *dy=0; }
    else { *dx=0; *dy=y<0 ? -1 : 1; }
}

static inline void interact_begin(InteractGesture *g, int state, uint64_t now) {
    g->state=state; g->since=now;
    if (state==INTERACT_TAP || state==INTERACT_HOLD || state==INTERACT_SWIPE) g->id=g->id>=200 ? 40 : g->id+1;
}

/* held: the key is down. mdx/mdy: mouse counts since the last sample. dir_x/dir_y: a direction
 * key or stick (-1/0/+1). */
static inline InteractOut interact_update(InteractGesture *g, const InteractParams *p, int held,
                                          float mdx, float mdy, int dir_x, int dir_y, uint64_t now) {
    InteractOut o={0,0,0.5f,0.5f,g->id,0};
    if (g->id<40) g->id=40;
    if (g->state==INTERACT_IDLE) {
        if (!held) return o;
        g->ax=g->ay=0; g->dx=g->dy=0;
        interact_begin(g,INTERACT_DECIDING,now); /* this sample's motion counts below */
    }
    if (g->state==INTERACT_DECIDING) {
        g->ax+=mdx; g->ay+=mdy;
        if (!held) interact_begin(g,INTERACT_TAP,now);
        else if (dir_x || dir_y) { interact_snap((float)dir_x,(float)dir_y,&g->dx,&g->dy); interact_begin(g,INTERACT_SWIPE,now); }
        else if (p->threshold>0 && g->ax*g->ax+g->ay*g->ay>=p->threshold*p->threshold) {
            interact_snap(g->ax,g->ay,&g->dx,&g->dy);
            interact_begin(g,INTERACT_SWIPE,now);
        } else if (now-g->since>=p->hold_us) interact_begin(g,INTERACT_HOLD,now);
        else { o.owns=1; return o; }
    } else if (g->state==INTERACT_SPENT) {
        if (!held) { g->state=INTERACT_IDLE; return o; }
        o.owns=1;
        return o;
    }
    o.id=g->id;
    const uint64_t t=now-g->since;
    if (g->state==INTERACT_TAP) {
        if (t>=INTERACT_LEAD_US+INTERACT_CLICK_US+INTERACT_TAIL_US) { g->state=held ? INTERACT_SPENT : INTERACT_IDLE; o.owns=held; return o; }
        o.touch=1;
        o.click=t>=INTERACT_LEAD_US && t<INTERACT_LEAD_US+INTERACT_CLICK_US;
        o.owns=held;
        return o;
    }
    if (g->state==INTERACT_HOLD) {
        if (!held) { g->state=INTERACT_IDLE; return o; }
        o.touch=1; o.click=t>=INTERACT_LEAD_US; o.owns=1;
        return o;
    }
    /* INTERACT_SWIPE: rest at the start for the lead, slide, rest at the end for the tail. */
    const uint64_t slide=p->swipe_us ? p->swipe_us : 1;
    if (t>=INTERACT_LEAD_US+slide+INTERACT_TAIL_US) { g->state=held ? INTERACT_SPENT : INTERACT_IDLE; o.owns=held; return o; }
    float f=t<=INTERACT_LEAD_US ? 0.0f : (float)(t-INTERACT_LEAD_US)/(float)slide;
    if (f>1.0f) f=1.0f;
    const float a=0.5f-INTERACT_REACH, b=0.5f+INTERACT_REACH;
    o.touch=1;
    o.x=g->dx ? (g->dx>0 ? a+(b-a)*f : b-(b-a)*f) : 0.5f;
    o.y=g->dy ? (g->dy>0 ? a+(b-a)*f : b-(b-a)*f) : 0.5f;
    o.owns=1;
    return o;
}
#endif
