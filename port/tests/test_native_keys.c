/* SPDX-License-Identifier: GPL-2.0-or-later */
/* iss_action_input.h and iss_native_keys.h: the keyboard's per-kind press state, with the game's
 * timing rules, and the kinds each native action presses. */
#undef NDEBUG
#include "iss_action_input.h"
#include "iss_native_keys.h"
#include <assert.h>
#include <stdio.h>

static const IssActionPause running={0,0,0};  /* never paused */

int main(void) {
    /* Melee and Execute are separate kinds: one key never presses the other. */
    const uint64_t melee=iss_native_kinds(1u<<ISS_NA_MELEE), execute=iss_native_kinds(1u<<ISS_NA_EXECUTE);
    assert(melee==1ull<<3 && execute==1ull<<4);
    assert(iss_native_kinds(1u<<ISS_NA_USE)==(1ull<<6|1ull<<7|1ull<<23));
    assert(iss_native_kinds(1u<<ISS_NA_INVESTIGATE)==(1ull<<16|1ull<<17|1ull<<18|1ull<<24));
    assert(iss_native_kinds(0)==0);
    for (int a=0;a<ISS_NA_COUNT;++a)
        for (int i=0;i<iss_native_actions[a].count;++i) assert(iss_native_actions[a].kinds[i]<ISS_ACT_KINDS);

    IssActionState s;
    iss_action_reset(&s);
    assert(!iss_action_pressed(&s,3,FLT_MAX,0,running) && !iss_action_held(&s,3) && !iss_action_down(&s,3));

    /* Press melee at t=1440 (1 s). */
    iss_action_update(&s,melee,1440);
    assert(iss_action_down(&s,3) && !iss_action_down(&s,4));
    assert(iss_action_held(&s,3) && !iss_action_held(&s,4));
    assert(iss_action_pressed(&s,3,0.2f,1440+144,running));        /* 0.1 s old, window 0.2 s */
    assert(!iss_action_pressed(&s,3,0.2f,1440+576,running));       /* 0.4 s old */
    assert(iss_action_pressed(&s,3,FLT_MAX,1440*60,running));      /* down and unconsumed */
    assert(!iss_action_pressed(&s,4,FLT_MAX,1440,running));

    /* Holding does not press again; consuming ends it until the next press. */
    iss_action_update(&s,melee,1440+288);
    assert(s.time[3]==1440);
    iss_action_consume(&s,3);
    assert(!iss_action_pressed(&s,3,FLT_MAX,1440+288,running) && !iss_action_held(&s,3) && iss_action_down(&s,3));
    iss_action_update(&s,0,1440+432);
    assert(!iss_action_down(&s,3));
    iss_action_update(&s,melee,2880);
    assert(iss_action_pressed(&s,3,0.1f,2880,running) && iss_action_held(&s,3));

    /* A release keeps a press inside its window (a tap between two reads still counts). */
    iss_action_update(&s,0,2880+72);
    assert(iss_action_pressed(&s,3,0.1f,2880+72,running) && !iss_action_held(&s,3));

    /* A pause from 0.1 s after the press, 10 s long: the paused time does not age the press. */
    const int32_t start=2880+144, end=start+1440*10;
    const IssActionPause after={0,end,start}, during={1,0,start};
    assert(iss_action_pressed(&s,3,0.25f,end+144,after));      /* 0.2 s of play since the press */
    assert(!iss_action_pressed(&s,3,0.15f,end+144,after));
    assert(!iss_action_pressed(&s,3,0.25f,end+144,running));
    assert(iss_action_pressed(&s,3,0.15f,start+1440*5,during)); /* still paused: 0.1 s of play */

    /* Out-of-range kinds are ignored. */
    iss_action_consume(&s,ISS_ACT_KINDS);
    assert(!iss_action_pressed(&s,ISS_ACT_KINDS,FLT_MAX,0,running) && !iss_action_down(&s,99));

    /* Every replaced function has a distinct address; no 14-byte jump runs into the next one. */
    const unsigned n=sizeof(iss_action_signatures)/sizeof(iss_action_signatures[0]);
    for (unsigned i=0;i<n;++i)
        for (unsigned j=0;j<n;++j)
            if (i!=j) {
                const uint32_t a=iss_action_signatures[i].address, b=iss_action_signatures[j].address;
                assert(a!=b && (b<a || b>=a+ISS_ACT_HOOK_BYTES));
            }
    puts("native keys tests passed");
    return 0;
}
