/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "iss_native_light_tiles.h"

static void check(uint32_t w, uint32_t h, uint32_t columns, uint32_t rows) {
    IssLightTiles t;
    assert(iss_light_tiles_layout(w, h, &t));
    assert(t.columns == columns && t.rows == rows);
    assert(t.row_base == 0x120u + 16u * columns);
    assert(t.size == t.row_base + 16u * rows);
    /* Every tile column and row the shader can address has its two boundary planes. */
    assert(t.columns >= (w + 15) / 16 + 1 && t.rows >= (h + 15) / 16 + 1);
}

int main(void) {
    IssLightTiles t;
    /* Retail sizes and smaller scenes keep the retail block exactly. */
    assert(iss_light_tiles_layout(1920, 1080, &t));
    assert(t.columns == 0xc9 && t.rows == 0x81 && t.row_base == 0xdb0 && t.size == 0x15c0);
    check(1280, 720, 0xc9, 0x81);
    check(3200, 1800, 0xc9, 0x81);
    check(3200, 2048, 0xc9, 0x81);
    /* Wider or taller than 200x128 tiles: sized by formula. */
    check(3216, 1800, 202, 0x81);
    check(3440, 1440, 216, 0x81);
    check(3840, 2160, 241, 136);
    check(1920, 1200, 0xc9, 0x81);   /* 16:10 */
    check(5120, 1440, 321, 0x81);    /* 32:9 */
    check(5120, 2160, 321, 136);
    check(7680, 2160, 481, 136);
    check(7680, 4320, 481, 271);
    check(15360, 4320, 961, 271);
    check(7681, 2161, 482, 137);     /* partial tiles round up */
    assert(!iss_light_tiles_layout(0, 1080, &t) && !iss_light_tiles_layout(1920, 0, &t));

    /* Guard and write against the four retail instructions. */
    static unsigned char image[0x16ad40];
    memset(image, 0x90, sizeof(image));
    assert(!iss_light_tiles_guard(image, sizeof(image)));
    for (unsigned i = 0; i < ISS_LIGHT_TILES_SITE_COUNT; i++)
        memcpy(image + iss_light_tiles_sites[i].offset, iss_light_tiles_sites[i].before,
               iss_light_tiles_sites[i].length);
    assert(iss_light_tiles_guard(image, sizeof(image)));
    assert(!iss_light_tiles_guard(image, 0x16ad34 + 6));
    assert(iss_light_tiles_layout(7680, 2160, &t));
    iss_light_tiles_write(image, &t);
    uint32_t v;
    memcpy(&v, image + 0x16acab + 1, 4); assert(v == t.size);
    memcpy(&v, image + 0x16acda + 2, 4); assert(v == 481);
    memcpy(&v, image + 0x16acea + 3, 4); assert(v == 0x120u + 16u * 481u);
    memcpy(&v, image + 0x16ad34 + 3, 4); assert(v == 136);
    /* Opcodes untouched. */
    assert(image[0x16acab] == 0xba && image[0x16acda] == 0x41 && image[0x16acdb] == 0xb8 &&
           image[0x16acea] == 0x48 && image[0x16aceb] == 0x81 && image[0x16acec] == 0xc7 &&
           image[0x16ad34] == 0xc7 && image[0x16ad35] == 0x04 && image[0x16ad36] == 0x24);
    /* After the write the retail guard no longer matches, so a second write is refused. */
    assert(!iss_light_tiles_guard(image, sizeof(image)));
    puts("light-tiles-test ok");
    return 0;
}
