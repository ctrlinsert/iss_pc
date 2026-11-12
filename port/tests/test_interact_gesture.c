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
    o=interact_update(&g,&p,1,0,0,0,0,now+=MS(100));
    assert(!o.touch && o.owns && g.state==INTERACT_SPENT);
    o=interact_update(&g,&p,1,0,-100,0,0,now+=MS(100));
    assert(!o.touch && o.owns && g.state==INTERACT_SPENT); /* no second swipe */
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

    /* A direction key after the hold has started does not swipe: the hold stays. */
    o=interact_update(&g,&p,1,0,0,0,0,now+=MS(10));
    o=interact_update(&g,&p,1,0,0,0,0,now+=MS(300));
    o=interact_update(&g,&p,1,0,0,0,1,now+=MS(60));
    assert(g.state==INTERACT_HOLD && o.click);
    o=interact_update(&g,&p,0,0,0,0,0,now+=MS(10));

    /* threshold 0 turns mouse swipes off; keys still swipe. */
    InteractParams nomouse=p; nomouse.threshold=0;
    o=interact_update(&g,&nomouse,1,1000,0,0,0,now+=MS(10));
    assert(g.state==INTERACT_DECIDING);
    o=interact_update(&g,&nomouse,1,0,0,0,1,now+=MS(10));
    assert(g.state==INTERACT_SWIPE && g.dy==1);

    puts("PASS: interact gesture");
    return 0;
}
