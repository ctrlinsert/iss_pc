/* The interact key's touchpad-read hook (src/iss_touch_reads.h): thunk recognition and the stub. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../src/iss_touch_reads.h"

volatile uint64_t iss_touch_reads[ISS_TOUCH_READ_GETTERS];
volatile int iss_touch_reads_active;

static void put_thunk(unsigned char *image, unsigned i, int32_t rel) {
    unsigned char *p = image + iss_touch_read_sites[i];
    const unsigned char head[11] = {0x48,0x8b,0x7f,0x28, 0x48,0x85,0xff, 0x48,0x0f,0x44,0x3d};
    memcpy(p, head, 11); memcpy(p + 11, &rel, 4);
    p[15] = 0x48; p[16] = 0x8b; p[17] = 0x07; p[18] = 0xff; p[19] = 0xa0;
    memcpy(p + 20, &iss_touch_read_slots[i], 4);
}

int main(void) {
    const size_t size = 0x300000;
    unsigned char *image = calloc(1, size);
    assert(image);
    for (unsigned i = 0; i < ISS_TOUCH_READ_GETTERS; i++) assert(!iss_touch_read_check(image, size, i));
    for (unsigned i = 0; i < ISS_TOUCH_READ_GETTERS; i++) {
        const int32_t rel = 0x1000 - (int32_t)(iss_touch_read_sites[i] + 15); /* the pad slot at +0x1000 */
        put_thunk(image, i, rel);
        assert(iss_touch_read_check(image, size, i) == (uintptr_t)image + 0x1000);
    }
    /* A different vtable slot, or a size too small for the thunk, is refused. */
    image[iss_touch_read_sites[2] + 20] ^= 8;
    assert(!iss_touch_read_check(image, size, 2));
    image[iss_touch_read_sites[2] + 20] ^= 8;
    assert(!iss_touch_read_check(image, iss_touch_read_sites[5] + 10, 5));
    assert(!iss_touch_read_check(NULL, size, 0) && !iss_touch_read_check(image, size, 6));

    unsigned char stub[ISS_TOUCH_READ_STUB];
    const unsigned n = iss_touch_read_build_stub(stub, 0x1122334455667788ull, 0x99aabbccddeeff00ull, 0xa8);
    assert(n <= ISS_TOUCH_READ_STUB && n == 44);
    uint64_t v; uint32_t s;
    assert(stub[0] == 0x48 && stub[1] == 0xb8); memcpy(&v, stub + 2, 8); assert(v == 0x1122334455667788ull);
    assert(stub[10] == 0xf0 && stub[11] == 0x48 && stub[12] == 0xff && stub[13] == 0x00);
    assert(stub[21] == 0x48 && stub[22] == 0xb8); memcpy(&v, stub + 23, 8); assert(v == 0x99aabbccddeeff00ull);
    assert(stub[31] == 0x48 && stub[32] == 0x0f && stub[33] == 0x44 && stub[34] == 0x38);
    assert(stub[38] == 0xff && stub[39] == 0xa0); memcpy(&s, stub + 40, 4); assert(s == 0xa8);
    free(image);
    puts("PASS: touch reads");
    return 0;
}
