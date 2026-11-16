/* SPDX-License-Identifier: GPL-2.0-or-later */
/* input_device.h: the device used last drives the game. */
#undef NDEBUG
#include "input_device.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    InputDevice s;
    uint64_t t=1000000;
    input_device_reset(&s,INPUT_DEVICE_KBM);
    assert(input_device_update(&s,false,false,t+=8000)==INPUT_DEVICE_KBM);
    assert(input_device_update(&s,true,false,t+=8000)==INPUT_DEVICE_PAD);   /* the keyboard was idle: the pad */
    assert(input_device_update(&s,true,true,t+=8000)==INPUT_DEVICE_PAD);    /* both: the one in use keeps it */
    for (int i=0;i<20;++i) assert(input_device_update(&s,true,true,t+=8000)==INPUT_DEVICE_PAD);
    /* The stick rests: the mouse takes over only after the pad has been idle a moment. */
    assert(input_device_update(&s,false,true,t+=100000)==INPUT_DEVICE_PAD);
    assert(input_device_update(&s,false,true,t+=150000)==INPUT_DEVICE_KBM);
    /* A stick crossing its centre (one idle read) does not lose the pad. */
    assert(input_device_update(&s,true,false,t+=300000)==INPUT_DEVICE_PAD);
    assert(input_device_update(&s,false,true,t+=8000)==INPUT_DEVICE_PAD);
    assert(input_device_update(&s,true,true,t+=8000)==INPUT_DEVICE_PAD);
    InputChannel c={INPUT_DEVICE_KBM,false,false};
    assert(input_channel_update(&c,true,false)==INPUT_DEVICE_PAD);   /* the stick alone */
    assert(input_channel_update(&c,true,true)==INPUT_DEVICE_KBM);    /* the mouse joins: it started last */
    assert(input_channel_update(&c,true,true)==INPUT_DEVICE_KBM);
    assert(input_channel_update(&c,false,true)==INPUT_DEVICE_KBM);
    assert(input_channel_update(&c,true,true)==INPUT_DEVICE_PAD);    /* the stick pushed again */
    assert(input_channel_update(&c,false,false)==INPUT_DEVICE_PAD);  /* idle: keeps the last */
    assert(input_channel_update(&c,true,true)==INPUT_DEVICE_PAD);    /* both start together: no change */
    puts("input device tests passed");
    return 0;
}
