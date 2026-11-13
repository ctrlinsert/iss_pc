/* SPDX-License-Identifier: GPL-2.0-or-later
 * US 01.07 tiled lighting: per-tile frustum planes for the lighting compute pass.
 *
 * Each frame 0x16ac40 allocates a constant block of 0x15c0 bytes and has 0x16a4e0 fill two
 * plane tables, one plane per 16-pixel tile boundary: columns at +0x120, rows at +0xdb0.
 * The block was sized for 200x128 tiles (3200x2048, the PS4 Pro's 3200x1800 mode). The filler
 * writes cols+1 and rows+1 planes without a limit (the passed maxima only size two memsets), so
 * a wider scene overwrites the row table and runs past the block, and the lighting shader reads
 * row planes or other data as column planes: wrong lights past x=3200.
 *
 * The fix sizes both tables from the scene: the column table gets cols+1 entries, the row table
 * follows it, and the block grows to hold both. The lighting shader's row-table offset (3504)
 * is moved to match by the recompiler (iss_light_tiles_row_base). Never smaller than retail. */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

enum {
    ISS_LIGHT_TILE_PX = 16,
    ISS_LIGHT_TILES_COLUMN_BASE = 0x120,
    ISS_LIGHT_TILES_ORIGINAL_COLUMNS = 0xc9, /* planes: 200 tile columns */
    ISS_LIGHT_TILES_ORIGINAL_ROWS = 0x81,    /* planes: 128 tile rows */
    ISS_LIGHT_TILES_ORIGINAL_ROW_BASE = 0xdb0,
    ISS_LIGHT_TILES_ORIGINAL_SIZE = 0x15c0,
    ISS_LIGHT_TILES_SITE_COUNT = 4
};

typedef struct { uint32_t columns, rows, row_base, size; } IssLightTiles;

/* Plane counts and layout for a scene; 0 on an unusable size. */
static inline int iss_light_tiles_layout(uint32_t width, uint32_t height, IssLightTiles *out) {
    if (!out || !width || !height || width > 65536u || height > 65536u) return 0;
    uint32_t columns = (width + ISS_LIGHT_TILE_PX - 1) / ISS_LIGHT_TILE_PX + 1;
    uint32_t rows = (height + ISS_LIGHT_TILE_PX - 1) / ISS_LIGHT_TILE_PX + 1;
    if (columns < ISS_LIGHT_TILES_ORIGINAL_COLUMNS) columns = ISS_LIGHT_TILES_ORIGINAL_COLUMNS;
    if (rows < ISS_LIGHT_TILES_ORIGINAL_ROWS) rows = ISS_LIGHT_TILES_ORIGINAL_ROWS;
    out->columns = columns;
    out->rows = rows;
    out->row_base = ISS_LIGHT_TILES_COLUMN_BASE + 16u * columns;
    out->size = out->row_base + 16u * rows;
    return 1;
}

typedef struct { uint32_t offset; unsigned length, imm; unsigned char before[7]; } IssLightTilesSite;
static const IssLightTilesSite iss_light_tiles_sites[ISS_LIGHT_TILES_SITE_COUNT] = {
    {0x16acab, 5, 1, {0xba, 0xc0, 0x15, 0x00, 0x00}},             /* mov $0x15c0,%edx: block size */
    {0x16acda, 6, 2, {0x41, 0xb8, 0xc9, 0x00, 0x00, 0x00}},       /* mov $0xc9,%r8d: column planes */
    {0x16acea, 7, 3, {0x48, 0x81, 0xc7, 0xb0, 0x0d, 0x00, 0x00}}, /* add $0xdb0,%rdi: row table */
    {0x16ad34, 7, 3, {0xc7, 0x04, 0x24, 0x81, 0x00, 0x00, 0x00}}, /* movl $0x81,(%rsp): row planes */
};

static inline int iss_light_tiles_guard(const unsigned char *image, size_t size) {
    if (!image) return 0;
    for (unsigned i = 0; i < ISS_LIGHT_TILES_SITE_COUNT; i++) {
        const IssLightTilesSite *s = &iss_light_tiles_sites[i];
        if (s->offset > size || s->length > size - s->offset ||
            memcmp(image + s->offset, s->before, s->length)) return 0;
    }
    return 1;
}

/* Call only after complete startup preflight, before guest threads run. */
static inline void iss_light_tiles_write(unsigned char *image, const IssLightTiles *t) {
    const uint32_t values[ISS_LIGHT_TILES_SITE_COUNT] = {t->size, t->columns, t->row_base, t->rows};
    for (unsigned i = 0; i < ISS_LIGHT_TILES_SITE_COUNT; i++)
        memcpy(image + iss_light_tiles_sites[i].offset + iss_light_tiles_sites[i].imm, &values[i], 4);
}

#ifdef __cplusplus
extern "C" {
#endif
/* The row-table offset the patched game writes; the original 0xdb0 when unpatched. The
 * recompiler moves the lighting shader's matching constant to it. */
uint32_t iss_light_tiles_row_base(void);
void iss_light_tiles_publish(uint32_t row_base);
#ifdef __cplusplus
}
#endif
