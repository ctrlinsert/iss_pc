/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Action-level input for the keyboard and mouse (inFAMOUS Second Son 1.07).
 *
 * The game asks for gameplay input by action kind through a few small functions: "was kind K
 * pressed within W seconds (and consume the press)", "is it held since an unconsumed press", "is
 * it down", "consume it" (ISS_ACT_* below), and three script natives that do the same. They turn
 * the kind into the controller's PS4 button mask and ask the controller, which keeps one press
 * time per button bit.
 *
 * probe.c replaces those functions. The pad half of each answer is still the game's own
 * controller code with the kind's plain mask, so a PS4 or Xbox pad behaves exactly as before. The
 * keyboard half comes from IssActionState: one down bit, press time and consumed mark per KIND, so
 * two kinds that share a PS4 button (Melee and Execute) are separate on the keyboard, with no
 * limit and nothing written into the PS4 button word.
 *
 * The press rules copy the controller's: times are in the game's 1/1440 s clock; a press is
 * consumed until the next press; a window of FLT_MAX means "down and not consumed"; a press made
 * before the controller's last pause is moved forward by the paused time. This file holds that
 * pure logic; runtime_native_keys.c holds the hooks. */
#ifndef ISS_ACTION_INPUT_H
#define ISS_ACTION_INPUT_H
#include <float.h>
#include <stdint.h>
#include <string.h>

#define ISS_ACT_KINDS 50u
#define ISS_ACT_CONSUMED ((int32_t)-15000)    /* the controller's "no unconsumed press" time */
#define ISS_ACT_TICKS 1440.0

/* The functions probe.c replaces (all sysv; ctl = controller, kind, window in seconds). */
#define ISS_ACT_PRESSED 0x248750u         /* (ctl,kind,window) pressed within window; consumes */
#define ISS_ACT_HELD_CONSUME 0x2487a0u    /* (ctl,kind) held since a press; consumes */
#define ISS_ACT_PRESSED_KEEP 0x2487e0u    /* (ctl,kind,window) pressed within window */
#define ISS_ACT_HELD 0x248820u            /* (ctl,kind) held since an unconsumed press */
#define ISS_ACT_DOWN 0x248850u            /* (ctl,kind) down */
#define ISS_ACT_CONSUME 0x248870u         /* (ctl,kind) */
#define ISS_ACT_SCRIPT_PRESSED 0x24dde0u  /* (object,kind,window): object+0x28 or the default */
#define ISS_ACT_SCRIPT_PRESSED_KEEP 0x24de40u
#define ISS_ACT_SCRIPT_DOWN 0x24de90u
#define ISS_ACT_ANY_PRESSED 0x20f740u     /* (player,set,window): kinds by index; consumes the hits */
#define ISS_ACT_ANY_HELD 0x20fa00u        /* (player,set): same, held */
#define ISS_ACT_CONSUME_SET 0x218b60u     /* (player,set) */
#define ISS_ACT_SET_KINDS 0x99e9b0u       /* s32 kind per set index (18) */
#define ISS_ACT_SET_COUNT 18u
#define ISS_ACT_ENABLED 0x1d0d0f0u        /* u32: set indexes the game allows now */
#define ISS_ACT_CLOCK 0xe162b4u           /* s32 game clock, 1/1440 s */
#define ISS_ACT_HOOK_BYTES 14u

typedef struct {
    uint64_t down;                 /* kinds down at the last pad read */
    int32_t time[ISS_ACT_KINDS];   /* press time, or ISS_ACT_CONSUMED */
} IssActionState;

/* The controller's pause bookkeeping, read from the controller at each query: whether it is paused
 * now, when the last pause ended and when it started. */
typedef struct { uint8_t use_clock; int32_t since, start; } IssActionPause;

static inline void iss_action_reset(IssActionState *s) {
    s->down=0;
    for (unsigned k=0;k<ISS_ACT_KINDS;++k) s->time[k]=ISS_ACT_CONSUMED;
}
/* At each pad read: a kind that goes down is pressed now. */
static inline void iss_action_update(IssActionState *s, uint64_t down, int32_t now) {
    const uint64_t pressed=down & ~s->down;
    for (unsigned k=0;k<ISS_ACT_KINDS;++k) if (pressed>>k & 1) s->time[k]=now;
    s->down=down;
}
static inline int iss_action_pressed(const IssActionState *s, unsigned kind, float window, int32_t now,
                                     IssActionPause pause) {
    if (kind>=ISS_ACT_KINDS) return 0;
    int32_t t=s->time[kind];
    if (t==ISS_ACT_CONSUMED) return 0;
    if (window==FLT_MAX) return (int)(s->down>>kind & 1);
    const int32_t ref=pause.use_clock ? now : pause.since;
    if (t<ref) {
        const int32_t from=t>pause.start ? t : pause.start;
        t+=(int32_t)((float)((double)(ref-from)*(1.0/ISS_ACT_TICKS))*(float)ISS_ACT_TICKS+0.5f);
    }
    const float age=(float)((double)(now-t)*(1.0/ISS_ACT_TICKS));
    return !(age>window);
}
static inline int iss_action_held(const IssActionState *s, unsigned kind) {
    return kind<ISS_ACT_KINDS && s->time[kind]!=ISS_ACT_CONSUMED && (s->down>>kind & 1);
}
static inline int iss_action_down(const IssActionState *s, unsigned kind) {
    return kind<ISS_ACT_KINDS && (s->down>>kind & 1);
}
static inline void iss_action_consume(IssActionState *s, unsigned kind) {
    if (kind<ISS_ACT_KINDS) s->time[kind]=ISS_ACT_CONSUMED;
}

/* The first bytes of each replaced function (and the set table), checked before patching. */
typedef struct { uint32_t address; unsigned char bytes[12]; } IssActionSignature;
static const IssActionSignature iss_action_signatures[]={
    {ISS_ACT_PRESSED,{0x55,0x48,0x89,0xe5,0x41,0x56,0x53,0x48,0x83,0xec,0x10,0x49}},
    {ISS_ACT_HELD_CONSUME,{0x55,0x48,0x89,0xe5,0x41,0x56,0x53,0x49,0x89,0xfe,0x89,0xf7}},
    {ISS_ACT_PRESSED_KEEP,{0x55,0x48,0x89,0xe5,0x41,0x56,0x53,0x48,0x83,0xec,0x10,0x48}},
    {ISS_ACT_HELD,{0x55,0x48,0x89,0xe5,0x41,0x56,0x53,0x48,0x89,0xfb,0x89,0xf7}},
    {ISS_ACT_DOWN,{0x55,0x48,0x89,0xe5,0x53,0x50,0x8b,0x5f,0x20,0x89,0xf7,0xe8}},
    {ISS_ACT_CONSUME,{0x55,0x48,0x89,0xe5,0x41,0x56,0x53,0x48,0x89,0xfb,0x89,0xf7}},
    {ISS_ACT_SCRIPT_PRESSED,{0x55,0x48,0x89,0xe5,0x41,0x56,0x53,0x48,0x83,0xec,0x10,0x48}},
    {ISS_ACT_SCRIPT_PRESSED_KEEP,{0x55,0x48,0x89,0xe5,0x41,0x56,0x53,0x48,0x83,0xec,0x10,0x48}},
    {ISS_ACT_SCRIPT_DOWN,{0x55,0x48,0x89,0xe5,0x53,0x50,0x48,0x8b,0x47,0x28,0x89,0xf7}},
    {ISS_ACT_ANY_PRESSED,{0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54}},
    {ISS_ACT_ANY_HELD,{0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54}},
    {ISS_ACT_CONSUME_SET,{0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x54,0x53,0x48}},
};
#endif
