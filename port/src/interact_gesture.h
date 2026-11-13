/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The interact key (runtime_pad.c): one key for everything the DualShock 4 touchpad does with
 * one finger, so keyboard and mouse players need no chord.
 *
 *   - tap (released before the hold time, no motion): a touchpad click;
 *   - hold still past the hold time: the finger lands and the pad stays pressed while the key is
 *     held (absorbing/draining powers, "hold the touchpad" prompts);
 *   - hold and move the mouse past the threshold, or press a direction (W/A/S/D, the arrows, a
 *     stick, also during a hold): a full swipe in that direction, played on its own (finger
 *     down, slide across the pad, lift). The direction snaps to up/down/left/right, as the game's
 *     swipes are. If the key is still held when the swipe ends, the finger stays at the end of
 *     the pad until the key is released (swipe and hold: lifting a mobile command centre while
 *     pressing R2). Nothing else starts until the key is released.
 *
 * While a gesture runs (`owns`), the caller keeps the direction keys from walking; the mouse is
 * taken only while it can still pick a swipe or a swipe plays (`owns_mouse`). A press made while
 * the mouse is already moving (turning the camera) cannot swipe with the mouse: it stays a tap or
 * a hold and the mouse keeps the camera, so draining while looking around works. A hold also
 * gives the mouse back to the camera. Coordinates are 0..1 across the pad (x right, y down). Time is passed
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
    float threshold;  /* mouse counts that make a hold a swipe (default 60) */
    uint32_t hold_us; /* how long the key must stay still to be a hold (default 250 ms) */
    uint32_t swipe_us;/* how long the finger takes to cross the pad (default 250 ms) */
    int swipe_hold;   /* after a swipe, a still-held key keeps the finger at the end (default 1) */
} InteractParams;

enum { INTERACT_IDLE, INTERACT_DECIDING, INTERACT_TAP, INTERACT_HOLD, INTERACT_SWIPE, INTERACT_SPENT };

typedef struct {
    int state;
    uint64_t since;   /* when the current state began */
    float ax, ay;     /* mouse motion gathered while deciding */
    int dx, dy;       /* swipe direction (one of them is 0) */
    int id;           /* touch id of the current gesture, grows with each one */
    int mouse_off;    /* this press began with the mouse turning the camera: it stays the camera */
    float bvx, bvy;   /* ...at this speed (counts per ms); only a flick beyond it swipes */
    int swiped;       /* the gesture was a swipe (SPENT keeps the mouse until release) */
} InteractGesture;

typedef struct {
    int touch, click; /* the game sees a finger, and the touchpad pressed */
    float x, y;       /* the finger, 0..1 */
    int id;
    int owns;         /* the key is in use: keep the character still */
    int owns_mouse;   /* the mouse belongs to the gesture: keep the camera still */
} InteractOut;

static inline InteractParams interact_default_params(void) {
    return (InteractParams){60.0f,250000u,250000u,1};
}

/* Snaps a motion to its main axis: -1/0/+1 each, one of them 0. */
static inline void interact_snap(float x, float y, int *dx, int *dy) {
    if (fabsf(x)>=fabsf(y)) { *dx=x<0 ? -1 : 1; *dy=0; }
    else { *dx=0; *dy=y<0 ? -1 : 1; }
}

/* The mouse picks a swipe once it has moved past the threshold mostly along one axis (the main
 * axis at least twice the other), so the first jitter of a fast flick does not pick the wrong
 * direction. A diagonal that keeps going still swipes, along its main axis, at 3x the threshold. */
static inline int interact_decided(float x, float y, float threshold) {
    const float d2=x*x+y*y, ax=fabsf(x), ay=fabsf(y);
    if (d2<threshold*threshold) return 0;
    return ax>=2.0f*ay || ay>=2.0f*ax || d2>=9.0f*threshold*threshold;
}
static inline void interact_begin(InteractGesture *g, int state, uint64_t now) {
    g->state=state; g->since=now;
    if (state==INTERACT_TAP || state==INTERACT_HOLD || state==INTERACT_SWIPE) g->id=g->id>=200 ? 40 : g->id+1;
}

/* One sample of a gesture already under way (not IDLE). */
static inline InteractOut interact_step(InteractGesture *g, const InteractParams *p, int held,
                                        float mdx, float mdy, int dir_x, int dir_y, uint64_t now) {
    InteractOut o={0,0,0.5f,0.5f,g->id,0,0};
    if (g->state==INTERACT_DECIDING) {
        g->ax+=mdx; g->ay+=mdy;
        /* Pressed while the camera turns: the steady panning is taken away, motion that carries
         * on in the pan's own direction never counts (a pan that speeds up is still a pan), and
         * what is left must be a flick of twice the usual size across or against the pan. So
         * panning stays camera and drains, and a sharp flick the other way (rescuing a detainee
         * on the run) still swipes; a direction key swipes any way. */
        const float el=(float)(now-g->since)/1000.0f;
        float fx=g->ax, fy=g->ay, carried=0.0f;
        if (g->mouse_off) {
            fx-=g->bvx*el; fy-=g->bvy*el;
            const float pl=sqrtf(g->bvx*g->bvx+g->bvy*g->bvy);
            if (pl>0.0f) {
                const float ux=g->bvx/pl, uy=g->bvy/pl, along=fx*ux+fy*uy;
                if (along>0.0f) { fx-=along*ux; fy-=along*uy; carried=along; }
            }
        }
        /* A hand that pans harder drifts sideways too: the flick must also outweigh the pan's
         * own growth (at least half of it), so drift on a fast pan is not a swipe. */
        const int flick_clear=!g->mouse_off || sqrtf(fx*fx+fy*fy)>=0.5f*carried;
        const float need=g->mouse_off ? 2.0f*p->threshold : p->threshold;
        if (!held) interact_begin(g,INTERACT_TAP,now);
        else if (dir_x || dir_y) { interact_snap((float)dir_x,(float)dir_y,&g->dx,&g->dy); g->swiped=1; interact_begin(g,INTERACT_SWIPE,now); }
        else if (p->threshold>0 && flick_clear && interact_decided(fx,fy,need)) {
            interact_snap(fx,fy,&g->dx,&g->dy);
            g->swiped=1; interact_begin(g,INTERACT_SWIPE,now);
        } else if (now-g->since>=p->hold_us) interact_begin(g,INTERACT_HOLD,now);
        else { o.owns=1; return o; }
    } else if (g->state==INTERACT_SPENT) {
        if (!held) { g->state=INTERACT_IDLE; return o; }
        o.owns=1;
        if (g->swiped && p->swipe_hold) { /* swipe and hold: the finger rests at the end */
            const float a=0.5f-INTERACT_REACH, b=0.5f+INTERACT_REACH;
            o.touch=1; o.id=g->id;
            o.x=g->dx ? (g->dx>0 ? b : a) : 0.5f;
            o.y=g->dy ? (g->dy>0 ? b : a) : 0.5f;
        }
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
    if (g->state==INTERACT_HOLD && held && (dir_x || dir_y)) {
        /* A direction pressed during a hold still swipes (press, wait, then W for a lift). */
        interact_snap((float)dir_x,(float)dir_y,&g->dx,&g->dy);
        g->swiped=1; interact_begin(g,INTERACT_SWIPE,now);
        return interact_step(g,p,held,0,0,0,0,now);
    }
    if (g->state==INTERACT_HOLD) {
        if (!held) { g->state=INTERACT_IDLE; return o; }
        o.touch=1; o.click=t>=INTERACT_LEAD_US; o.owns=1;
        return o;
    }
    /* INTERACT_SWIPE: rest at the start for the lead, slide, rest at the end for the tail. */
    const uint64_t slide=p->swipe_us ? p->swipe_us : 1;
    if (t>=INTERACT_LEAD_US+slide+INTERACT_TAIL_US) {
        if (held) { g->state=INTERACT_SPENT; return interact_step(g,p,held,0,0,0,0,now); }
        g->state=INTERACT_IDLE; return o;
    }
    float f=t<=INTERACT_LEAD_US ? 0.0f : (float)(t-INTERACT_LEAD_US)/(float)slide;
    if (f>1.0f) f=1.0f;
    const float a=0.5f-INTERACT_REACH, b=0.5f+INTERACT_REACH;
    o.touch=1;
    o.x=g->dx ? (g->dx>0 ? a+(b-a)*f : b-(b-a)*f) : 0.5f;
    o.y=g->dy ? (g->dy>0 ? a+(b-a)*f : b-(b-a)*f) : 0.5f;
    o.owns=1;
    return o;
}

static inline int interact_owns_mouse_state(const InteractGesture *g) {
    if (g->state==INTERACT_DECIDING) return !g->mouse_off;
    return g->state==INTERACT_SWIPE;
}
/* held: the key is down. mdx/mdy: mouse counts since the last sample. dir_x/dir_y: a direction
 * key or stick (-1/0/+1). mouse_moving: the mouse was turning the camera just before this sample,
 * at vx/vy counts per ms (read only when the key goes down). */
static inline InteractOut interact_update_ex(InteractGesture *g, const InteractParams *p, int held,
                                             float mdx, float mdy, int dir_x, int dir_y, uint64_t now,
                                             int mouse_moving, float vx, float vy) {
    InteractOut o={0,0,0.5f,0.5f,g->id,0,0};
    if (g->id<40) g->id=40;
    if (g->state==INTERACT_IDLE) {
        if (!held) return o;
        g->ax=g->ay=0; g->dx=g->dy=0; g->swiped=0; g->mouse_off=mouse_moving!=0;
        g->bvx=g->mouse_off ? vx : 0.0f; g->bvy=g->mouse_off ? vy : 0.0f;
        interact_begin(g,INTERACT_DECIDING,now); /* this sample's motion counts below */
        if (g->mouse_off) mdx=mdy=0.0f; /* ...unless it is the pan the press landed in */
    }
    o=interact_step(g,p,held,mdx,mdy,dir_x,dir_y,now);
    o.owns_mouse=o.owns && interact_owns_mouse_state(g);
    return o;
}
static inline InteractOut interact_update(InteractGesture *g, const InteractParams *p, int held,
                                          float mdx, float mdy, int dir_x, int dir_y, uint64_t now) {
    return interact_update_ex(g,p,held,mdx,mdy,dir_x,dir_y,now,0,0.0f,0.0f);
}
#endif
