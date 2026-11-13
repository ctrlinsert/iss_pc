/* The interact key (src/interact_gesture.h): tap, hold, mouse swipe, key swipe, spent state.
 * Time is passed in, so nothing sleeps. */
#include <assert.h>
#include <stdio.h>
#include "../src/interact_gesture.h"

#define MS(n) ((uint64_t)(n)*1000u)

static int near(float a, float b) { return fabsf(a-b)<0.01f; }

int main(void) {
    const InteractParams p=interact_default_params();
    uint64_t now=1000000;

    /* Idle: nothing. */
    InteractGesture g={0};
    InteractOut o=interact_update(&g,&p,0,5,5,0,0,now);
    assert(!o.touch && !o.click && !o.owns && g.state==INTERACT_IDLE);

    /* Tap: down 100 ms, up. Finger first, then the click, then the finger lingers. */
    o=interact_update(&g,&p,1,0,0,0,0,now);
    assert(o.owns && !o.touch && g.state==INTERACT_DECIDING);
    o=interact_update(&g,&p,1,0,0,0,0,now+=MS(100));
    assert(o.owns && !o.touch);
    o=interact_update(&g,&p,0,0,0,0,0,now+=MS(10));
    assert(g.state==INTERACT_TAP && o.touch && !o.click && !o.owns);
    const int tap_id=o.id;
    o=interact_update(&g,&p,0,0,0,0,0,now+=MS(60));
    assert(o.touch && o.click && near(o.x,0.5f) && near(o.y,0.5f) && o.id==tap_id);
    o=interact_update(&g,&p,0,0,0,0,0,now+=MS(60));
    assert(o.touch && !o.click);
    o=interact_update(&g,&p,0,0,0,0,0,now+=MS(100));
    assert(!o.touch && g.state==INTERACT_IDLE);

    /* Hold still: past the hold time the finger lands, the click follows and stays while held. */
    o=interact_update(&g,&p,1,0,0,0,0,now+=MS(10));
    o=interact_update(&g,&p,1,1,1,0,0,now+=MS(260)); /* small motion stays below the threshold */
    assert(g.state==INTERACT_HOLD && o.touch && !o.click && o.owns && o.id!=tap_id);
    o=interact_update(&g,&p,1,0,0,0,0,now+=MS(60));
    assert(o.touch && o.click && o.owns);
    o=interact_update(&g,&p,1,500,0,0,0,now+=MS(2000)); /* motion during a hold changes nothing */
    assert(g.state==INTERACT_HOLD && o.touch && o.click && near(o.x,0.5f));
    o=interact_update(&g,&p,0,0,0,0,0,now+=MS(10));
    assert(!o.touch && !o.click && !o.owns && g.state==INTERACT_IDLE);

    /* Mouse swipe up: motion past the threshold picks the direction; the finger runs from the
     * bottom to the top, then lifts; the key, still held, is spent until released. */
    o=interact_update(&g,&p,1,2,-20,0,0,now+=MS(10));
    assert(g.state==INTERACT_DECIDING);
    o=interact_update(&g,&p,1,3,-45,0,0,now+=MS(10));
    assert(g.state==INTERACT_SWIPE && g.dx==0 && g.dy==-1);
    assert(o.touch && !o.click && near(o.y,0.92f) && near(o.x,0.5f));
    o=interact_update(&g,&p,1,0,0,0,0,now+=MS(50+125)); /* halfway */
    assert(o.touch && near(o.y,0.5f));
    o=interact_update(&g,&p,1,0,0,0,0,now+=MS(125));
    assert(o.touch && near(o.y,0.08f));
    const int swipe_id=o.id;
    o=interact_update(&g,&p,1,0,0,0,0,now+=MS(100));
    assert(o.touch && near(o.y,0.08f) && o.id==swipe_id && o.owns && g.state==INTERACT_SPENT); /* swipe and hold */
    o=interact_update(&g,&p,1,0,-100,0,0,now+=MS(2000));
    assert(o.touch && near(o.y,0.08f) && o.id==swipe_id && !o.click && g.state==INTERACT_SPENT); /* no second swipe */
    o=interact_update(&g,&p,0,0,0,0,0,now+=MS(10));
    assert(!o.owns && g.state==INTERACT_IDLE);

    /* Mouse swipe right, a diagonal snapped to its main axis. */
    o=interact_update(&g,&p,1,80,-30,0,0,now+=MS(10));
    assert(g.state==INTERACT_SWIPE && g.dx==1 && g.dy==0 && near(o.x,0.08f) && near(o.y,0.5f));
    o=interact_update(&g,&p,0,0,0,0,0,now+=MS(50+250));
    assert(o.touch && near(o.x,0.92f) && o.owns); /* released mid-swipe: it still finishes */
    o=interact_update(&g,&p,0,0,0,0,0,now+=MS(101));
    assert(!o.touch && g.state==INTERACT_IDLE);

    /* A flick that starts sideways but goes up: undecided until one axis dominates. */
    o=interact_update(&g,&p,1,40,-45,0,0,now+=MS(10));
    assert(g.state==INTERACT_DECIDING);
    o=interact_update(&g,&p,1,0,-100,0,0,now+=MS(10));
    assert(g.state==INTERACT_SWIPE && g.dx==0 && g.dy==-1);
    o=interact_update(&g,&p,0,0,0,0,0,now+=MS(500));
    assert(!o.touch && g.state==INTERACT_IDLE);
    /* A true diagonal still swipes along its main axis once it is long enough. */
    o=interact_update(&g,&p,1,100,-90,0,0,now+=MS(10));
    assert(g.state==INTERACT_DECIDING);
    o=interact_update(&g,&p,1,40,-30,0,0,now+=MS(10));
    assert(g.state==INTERACT_SWIPE && g.dx==1 && g.dy==0);
    o=interact_update(&g,&p,0,0,0,0,0,now+=MS(500));
    assert(!o.touch && g.state==INTERACT_IDLE);

    /* Key swipe: a direction key while deciding swipes that way at once, even later than the
     * mouse would. */
    o=interact_update(&g,&p,1,0,0,0,0,now+=MS(10));
    o=interact_update(&g,&p,1,0,0,-1,0,now+=MS(200));
    assert(g.state==INTERACT_SWIPE && g.dx==-1 && near(o.x,0.92f));
    o=interact_update(&g,&p,0,0,0,0,0,now+=MS(400));
    assert(!o.touch && g.state==INTERACT_IDLE);

    /* No swipe prompt: mouse motion after the hold has started does not swipe (looking around
     * while draining); the hold stays. A direction key does (hold-then-direction case below). */
    o=interact_update(&g,&p,1,0,0,0,0,now+=MS(10));
    o=interact_update(&g,&p,1,0,0,0,0,now+=MS(300));
    o=interact_update(&g,&p,1,0,300,0,0,now+=MS(60));
    assert(g.state==INTERACT_HOLD && o.click);
    o=interact_update(&g,&p,0,0,0,0,0,now+=MS(10));

    /* At a swipe prompt a still press does not hold: it waits for the flick, however long, and a
     * release with no flick taps. */
    g.swipe_prompt=1;
    o=interact_update(&g,&p,1,0,0,0,0,now+=MS(10));
    o=interact_update(&g,&p,1,0,0,0,0,now+=MS(900));
    assert(g.state==INTERACT_DECIDING && !o.touch && o.owns_mouse);
    o=interact_update(&g,&p,1,-300,0,0,0,now+=MS(60));
    assert(g.state==INTERACT_SWIPE && g.dx==-1);
    o=interact_update(&g,&p,0,0,0,0,0,now+=MS(500));
    assert(g.state==INTERACT_IDLE);
    o=interact_update(&g,&p,1,0,0,0,0,now+=MS(10));
    o=interact_update(&g,&p,1,0,0,0,0,now+=MS(900));
    o=interact_update(&g,&p,0,0,0,0,0,now+=MS(10));
    assert(g.state==INTERACT_TAP);
    o=interact_update(&g,&p,0,0,0,0,0,now+=MS(500));
    g.swipe_prompt=0;

    /* A swipe prompt that comes up during a hold: a flick still swipes. A slow drift does not. */
    o=interact_update(&g,&p,1,0,0,0,0,now+=MS(10));
    o=interact_update(&g,&p,1,0,0,0,0,now+=MS(300));
    assert(g.state==INTERACT_HOLD);
    g.swipe_prompt=1;
    for (int k=0;k<10;++k) o=interact_update(&g,&p,1,0,5,0,0,now+=MS(16)); /* 0.3 counts/ms */
    assert(g.state==INTERACT_HOLD && o.click);
    o=interact_update(&g,&p,1,0,300,0,0,now+=MS(60));
    assert(g.state==INTERACT_SWIPE && g.dy==1 && o.touch && !o.click && o.owns_mouse);
    o=interact_update(&g,&p,1,0,0,0,0,now+=MS(500));
    assert(g.state==INTERACT_SPENT && near(o.y,0.92f));
    o=interact_update(&g,&p,0,0,0,0,0,now+=MS(10));
    assert(g.state==INTERACT_IDLE);
    /* ...and a hold pressed while panning ignores more panning the same way. */
    g.swipe_prompt=0;
    o=interact_update_ex(&g,&p,1,20,0,0,0,now+=MS(10),1,2.0f,0.0f);
    o=interact_update_ex(&g,&p,1,0,0,0,0,now+=MS(300),0,0,0);
    assert(g.state==INTERACT_HOLD);
    g.swipe_prompt=1;
    for (int k=0;k<10;++k) o=interact_update_ex(&g,&p,1,160,0,0,0,now+=MS(16),0,0,0); /* 10/ms, same way */
    assert(g.state==INTERACT_HOLD);
    o=interact_update_ex(&g,&p,0,0,0,0,0,now+=MS(10),0,0,0);
    g.swipe_prompt=0;

    /* threshold 0 turns mouse swipes off; keys still swipe. */
    InteractParams nomouse=p; nomouse.threshold=0;
    o=interact_update(&g,&nomouse,1,1000,0,0,0,now+=MS(10));
    assert(g.state==INTERACT_DECIDING);
    o=interact_update(&g,&nomouse,1,0,0,0,1,now+=MS(10));
    assert(g.state==INTERACT_SWIPE && g.dy==1);

    /* Pressed while the mouse turns the camera (2 counts/ms to the right): steady panning stays
     * camera (the mouse is not taken) and the press holds; a sharp flick on top still swipes. */
    {
        InteractGesture h={0};
        uint64_t t=now+MS(1000);
        o=interact_update_ex(&h,&p,1,20,0,0,0,t,1,2.0f,0.0f);
        assert(h.state==INTERACT_DECIDING && o.owns && !o.owns_mouse);
        o=interact_update_ex(&h,&p,1,200,5,0,0,t+=MS(100),0,0,0);
        assert(h.state==INTERACT_DECIDING && !o.owns_mouse);
        o=interact_update_ex(&h,&p,1,300,0,0,0,t+=MS(160),0,0,0);
        assert(h.state==INTERACT_HOLD && o.touch && !o.owns_mouse);
        o=interact_update_ex(&h,&p,0,0,0,0,0,t+=MS(10),0,0,0);
        assert(h.state==INTERACT_IDLE && !o.owns_mouse);
        /* Same panning, plus a flick up of 150 counts (rescuing a detainee on the run): swipes up. */
        o=interact_update_ex(&h,&p,1,20,0,0,0,t+=MS(10),1,2.0f,0.0f);
        o=interact_update_ex(&h,&p,1,100,-150,0,0,t+=MS(50),0,0,0);
        assert(h.state==INTERACT_SWIPE && h.dx==0 && h.dy==-1 && o.owns_mouse);
        o=interact_update_ex(&h,&p,0,0,0,0,0,t+=MS(500),0,0,0);
        assert(h.state==INTERACT_IDLE);
        /* A flick the size a still press needs (70) is not enough on top of panning. */
        o=interact_update_ex(&h,&p,1,20,0,0,0,t+=MS(10),1,2.0f,0.0f);
        o=interact_update_ex(&h,&p,1,100,-70,0,0,t+=MS(50),0,0,0);
        assert(h.state==INTERACT_DECIDING);
        o=interact_update_ex(&h,&p,0,0,0,0,0,t+=MS(10),0,0,0);
        o=interact_update_ex(&h,&p,0,0,0,0,0,t+=MS(500),0,0,0);
        assert(h.state==INTERACT_IDLE);
        /* A still press still swipes, and owns the mouse while deciding and swiping. */
        o=interact_update_ex(&h,&p,1,0,0,0,0,t+=MS(10),0,0,0);
        assert(o.owns_mouse);
        o=interact_update_ex(&h,&p,1,0,-80,0,0,t+=MS(10),0,0,0);
        assert(h.state==INTERACT_SWIPE && o.owns_mouse);
        o=interact_update_ex(&h,&p,1,0,0,0,0,t+=MS(500),0,0,0);
        assert(h.state==INTERACT_SPENT && o.touch && near(o.y,0.08f) && !o.owns_mouse);
        o=interact_update_ex(&h,&p,0,0,0,0,0,t+=MS(10),0,0,0);
        assert(!o.touch && h.state==INTERACT_IDLE);
        /* TLG_INTERACT_SWIPE_HOLD=0: the finger lifts after the swipe even if the key is held. */
        InteractParams q=p; q.swipe_hold=0;
        o=interact_update_ex(&h,&q,1,0,0,0,0,t+=MS(10),0,0,0);
        o=interact_update_ex(&h,&q,1,0,-80,0,0,t+=MS(10),0,0,0);
        o=interact_update_ex(&h,&q,1,0,0,0,0,t+=MS(500),0,0,0);
        assert(h.state==INTERACT_SPENT && !o.touch);
        o=interact_update_ex(&h,&p,0,0,0,0,0,t+=MS(10),0,0,0);
        /* A still press that becomes a hold hands the mouse back. */
        o=interact_update_ex(&h,&p,1,0,0,0,0,t+=MS(10),0,0,0);
        o=interact_update_ex(&h,&p,1,0,0,0,0,t+=MS(300),0,0,0);
        assert(h.state==INTERACT_HOLD && !o.owns_mouse && o.owns);
        /* A direction key still swipes from a press made while the mouse moved. */
        o=interact_update_ex(&h,&p,0,0,0,0,0,t+=MS(10),0,0,0);
        o=interact_update_ex(&h,&p,1,100,0,0,0,t+=MS(10),1,0,0);
        o=interact_update_ex(&h,&p,1,0,0,1,0,t+=MS(10),0,0,0);
        assert(h.state==INTERACT_SWIPE && h.dx==1 && o.owns_mouse);
    }

    /* Hold first, then a direction: the hold becomes a swipe that way, and stays at the end. */
    {
        InteractGesture h={0};
        uint64_t t=now+MS(5000);
        o=interact_update(&h,&p,1,0,0,0,0,t);
        o=interact_update(&h,&p,1,0,0,0,0,t+=MS(300));
        assert(h.state==INTERACT_HOLD && o.touch);
        o=interact_update(&h,&p,1,0,0,0,-1,t+=MS(500));
        assert(h.state==INTERACT_SWIPE && h.dy==-1 && o.touch && near(o.y,0.92f));
        o=interact_update(&h,&p,1,0,0,0,0,t+=MS(500));
        assert(h.state==INTERACT_SPENT && o.touch && near(o.y,0.08f));
        o=interact_update(&h,&p,0,0,0,0,0,t+=MS(10));
        assert(!o.touch && h.state==INTERACT_IDLE);
    }

    /* Panning that speeds up after the press (Joshua's drain at 90..1265 counts) is still a pan:
     * no swipe in the pan's direction, the press holds and drains. */
    {
        InteractGesture h={0};
        uint64_t t=now+MS(9000);
        o=interact_update_ex(&h,&p,1,160,0,0,0,t,1,10.0f,0.0f);   /* press sample: 160 counts */
        assert(h.state==INTERACT_DECIDING);
        for (int k=0;k<15;++k) {
            o=interact_update_ex(&h,&p,1,400,10,0,0,t+=MS(17),0,0,0); /* 2.4x faster, same way */
            assert(h.state==INTERACT_DECIDING || h.state==INTERACT_HOLD);
        }
        assert(h.state==INTERACT_HOLD && o.touch);
        o=interact_update_ex(&h,&p,0,0,0,0,0,t+=MS(10),0,0,0);
        /* A flick up across the same pan swipes. */
        o=interact_update_ex(&h,&p,1,160,0,0,0,t+=MS(10),1,10.0f,0.0f);
        o=interact_update_ex(&h,&p,1,170,-150,0,0,t+=MS(17),0,0,0);
        assert(h.state==INTERACT_SWIPE && h.dy==-1);
        o=interact_update_ex(&h,&p,0,0,0,0,0,t+=MS(500),0,0,0);
        assert(h.state==INTERACT_IDLE);
    }

    /* Velocity: a slow drift from a still press that adds up past the distance (0.4 counts/ms,
     * 100 counts in 240 ms) is noise, not a flick: it holds. */
    {
        InteractGesture h={0};
        uint64_t t=now+MS(20000);
        o=interact_update(&h,&p,1,0,0,0,0,t);
        for (int k=0;k<15;++k) o=interact_update(&h,&p,1,7,0,0,0,t+=MS(16));
        assert(h.state==INTERACT_DECIDING || h.state==INTERACT_HOLD);
        o=interact_update(&h,&p,1,0,0,0,0,t+=MS(20));
        assert(h.state==INTERACT_HOLD);
        o=interact_update(&h,&p,0,0,0,0,0,t+=MS(10));
    }

    puts("PASS: interact gesture");
    return 0;
}
