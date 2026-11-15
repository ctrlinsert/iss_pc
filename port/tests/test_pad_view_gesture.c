/* View on a pad without a touchpad (src/pad_view_gesture.h): click away from prompts, and at a
 * swipe prompt tap / resting finger / right-stick swipes. Time is passed in, so nothing sleeps. */
#include <assert.h>
#include <stdio.h>
#include "../src/pad_view_gesture.h"

#define MS(n) ((uint64_t)(n)*1000u)

static int near(float a, float b) { return fabsf(a-b)<0.01f; }

int main(void) {
    const ViewParams p=view_default_params();
    uint64_t t=1000000;
    ViewGesture g={0};
    ViewOut o;

    /* Idle: nothing, and the stick alone never swipes. */
    o=view_update(&g,&p,0,1,0,0.0f,-1.0f,t);
    assert(!o.pass && !o.touch && !o.owns_stick && g.state==VIEW_IDLE);

    /* No prompt: View is the click while held, the stick stays the camera, no swipe. */
    o=view_update(&g,&p,1,0,0,0,0,t+=MS(10));
    assert(o.pass && !o.touch && !o.owns_stick);
    o=view_update(&g,&p,1,1,0,1.0f,0,t+=MS(500)); /* a prompt showing up later: still a drain */
    assert(o.pass && !o.touch && !o.owns_stick && g.state==VIEW_PASS);
    o=view_update(&g,&p,0,1,0,0,0,t+=MS(10));
    assert(!o.pass && g.state==VIEW_IDLE);

    /* Prompt, quick tap: no click while down, one click sample on release. */
    o=view_update(&g,&p,1,1,0,0,0,t+=MS(10));
    assert(!o.pass && !o.touch && o.owns_stick);
    o=view_update(&g,&p,0,1,0,0,0,t+=MS(100));
    assert(o.pass && !o.touch);
    o=view_update(&g,&p,0,1,0,0,0,t+=MS(10));
    assert(!o.pass && g.state==VIEW_IDLE);

    /* Prompt, held still: a finger rests in the middle, never a click. */
    o=view_update(&g,&p,1,1,0,0,0,t+=MS(10));
    o=view_update(&g,&p,1,1,0,0,0,t+=MS(260));
    assert(g.state==VIEW_HOLD && o.touch && !o.pass && near(o.x,0.5f) && near(o.y,0.5f));
    const int hold_id=o.id;
    o=view_update(&g,&p,0,1,0,0,0,t+=MS(10));
    assert(!o.touch && !o.pass && g.state==VIEW_IDLE); /* a long press is not a tap */

    /* Prompt, flick up: the finger slides bottom to top; stick kept up = finger rests at the top;
     * stick back = lift; a second flick (right) swipes again. */
    o=view_update(&g,&p,1,1,0,0,0,t+=MS(10));
    o=view_update(&g,&p,1,1,0,0.1f,-0.9f,t+=MS(30));
    assert(g.state==VIEW_SWIPE && g.dy==-1 && g.dx==0 && o.id!=hold_id);
    assert(o.touch && near(o.y,0.5f+INTERACT_REACH) && o.owns_stick);
    o=view_update(&g,&p,1,1,0,0,-0.9f,t+=MS(50+125));
    assert(o.touch && near(o.y,0.5f) );
    o=view_update(&g,&p,1,1,0,0,-0.9f,t+=MS(130));
    assert(g.state==VIEW_REST && o.touch && near(o.y,0.5f-INTERACT_REACH));
    o=view_update(&g,&p,1,1,0,0,-0.9f,t+=MS(2000));
    assert(g.state==VIEW_REST && o.touch);
    o=view_update(&g,&p,1,1,0,0,0,t+=MS(10));
    assert(!o.touch && g.state==VIEW_DECIDING && o.owns_stick);
    o=view_update(&g,&p,1,1,0,0,0,t+=MS(500)); /* after a swipe, waiting never rests a finger */
    assert(!o.touch && g.state==VIEW_DECIDING);
    o=view_update(&g,&p,1,1,0,1.0f,0.2f,t+=MS(10));
    assert(g.state==VIEW_SWIPE && g.dx==1 && near(o.x,0.5f-INTERACT_REACH));
    /* release during the slide: it finishes, then lifts */
    o=view_update(&g,&p,0,1,0,0,0,t+=MS(100));
    assert(o.touch && g.state==VIEW_SWIPE);
    o=view_update(&g,&p,0,1,0,0,0,t+=MS(400));
    assert(!o.touch && g.state==VIEW_IDLE);

    /* A stick already pushed (camera) when View goes down does not swipe until re-centred. */
    o=view_update(&g,&p,1,1,0,-1.0f,0,t+=MS(10));
    o=view_update(&g,&p,1,1,0,-1.0f,0,t+=MS(100));
    assert(g.state==VIEW_DECIDING && !o.touch);
    o=view_update(&g,&p,1,1,0,0,0,t+=MS(10));
    o=view_update(&g,&p,1,1,0,-1.0f,0,t+=MS(10));
    assert(g.state==VIEW_SWIPE && g.dx==-1);
    o=view_update(&g,&p,0,1,0,0,0,t+=MS(500));
    o=view_update(&g,&p,0,1,0,0,0,t+=MS(10));
    assert(g.state==VIEW_IDLE);

    /* RB or L1 with View at a prompt: the click chords (two fingers, left click) pass through. */
    o=view_update(&g,&p,1,1,1,0,0,t+=MS(10));
    assert(o.pass && !o.owns_stick && g.state==VIEW_PASS);
    o=view_update(&g,&p,0,1,0,0,0,t+=MS(10));
    o=view_update(&g,&p,1,1,0,0,0,t+=MS(10));
    o=view_update(&g,&p,1,1,1,0,0,t+=MS(50)); /* RB pressed just after View */
    assert(o.pass && g.state==VIEW_PASS);
    o=view_update(&g,&p,0,1,0,0,0,t+=MS(10));

    puts("pad view gesture: ok");
    return 0;
}
