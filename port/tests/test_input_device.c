/* SPDX-License-Identifier: GPL-2.0-or-later */
/* input_device.h: the device used last drives the game. */
#undef NDEBUG
#include "input_device.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    InputDevice s;
    input_device_reset(&s,INPUT_DEVICE_KBM);
    assert(input_device_update(&s,false,false)==INPUT_DEVICE_KBM);
    assert(input_device_update(&s,true,false)==INPUT_DEVICE_PAD);   /* a pad button goes down */
    assert(input_device_update(&s,false,false)==INPUT_DEVICE_PAD);  /* held or idle: no change */
    assert(input_device_update(&s,true,true)==INPUT_DEVICE_PAD);    /* both at once: no change */
    assert(input_device_update(&s,false,true)==INPUT_DEVICE_KBM);   /* a key goes down */
    assert(input_device_update(&s,false,false)==INPUT_DEVICE_KBM);
    puts("input device tests passed");
    return 0;
}
