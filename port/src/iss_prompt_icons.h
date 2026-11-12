/* Button-prompt icons: the format side (no game data here; everything is read from the
 * player's own lang_<language>_text.xpps when the game opens it).
 *
 * The game draws button prompts as text: strings hold Private Use Area code points
 * (U+F080 circle, U+F083 cross, U+F089 OPTIONS, ...) and one font of the language's text package
 * draws them from a single RGBA8 atlas texture. Replacing those glyph cells in the atlas, as the
 * file is read into guest memory, changes every prompt without touching strings or files.
 *
 * Package (KCAP, see tools/xpps/kcap.py): u32 data offset at 0x28, u32 data size at 0x2c; every
 * pointer inside the data is relative to the data offset.
 *   Font object (8-aligned): +0x18 u64 name hash of its atlas texture, +0x28 u64 glyph table,
 *     +0x30 u32 glyph count. Glyph: 32 bytes, u16 code point, u16 x, u16 y, u16 w, u16 h
 *     (pixels, rows top-down), ..., u16 advance at +0x0e.
 *   Texture object: GNM T# (8 dwords) at +0x08, u64 name hash at +0x28. dword0 = data offset of
 *     the pixels, dword1 bits 20-25 data format (0xA = 8_8_8_8), dword2 width-1 (bits 0-13) and
 *     height-1 (bits 14-27), dword3 bits 20-24 tile mode (13 = Thin1DThin).
 *   Thin1DThin, 32-bit texels: 8x8 micro tiles in row-major order, texel index inside a tile
 *     from the bits x0 y0 x1 y1 x2 y2. The atlas rows are stored bottom-up. */
#ifndef ISS_PROMPT_ICONS_H
#define ISS_PROMPT_ICONS_H
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define IPI_FIRST_CP 0xF080u
#define IPI_LAST_CP 0xF1FFu
#define IPI_MAX_GLYPHS 256

typedef struct { uint16_t cp, x, y, w, h; } IpiGlyph;
typedef struct {
    uint64_t pixels;          /* file offset of the atlas texels */
    uint32_t width, height;   /* texels */
    uint32_t count;           /* glyphs */
    IpiGlyph glyphs[IPI_MAX_GLYPHS];
} IpiAtlas;

static inline uint16_t ipi_u16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static inline uint32_t ipi_u32(const uint8_t *p) { return (uint32_t)ipi_u16(p) | (uint32_t)ipi_u16(p + 2) << 16; }
static inline uint64_t ipi_u64(const uint8_t *p) { return (uint64_t)ipi_u32(p) | (uint64_t)ipi_u32(p + 4) << 32; }

/* Whether a guest path is a language text package ("/app0/art/cache/lang_<x>_text.xpps";
 * the "_text_p" patch packages hold no fonts). */
static inline int ipi_is_text_package(const char *path) {
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    const size_t n = strlen(base);
    return !strncmp(base, "lang_", 5) && n > 15 && !strcmp(base + n - 10, "_text.xpps");
}

/* Byte offset of texel (x, y) (rows top-down) in a Thin1DThin RGBA8 atlas stored bottom-up. */
static inline size_t ipi_texel_offset(uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
    y = height - 1 - y;
    const uint32_t e = (x & 1) | (y & 1) << 1 | (x >> 1 & 1) << 2 | (y >> 1 & 1) << 3 |
                       (x >> 2 & 1) << 4 | (y >> 2 & 1) << 5;
    return ((size_t)(y / 8) * (width / 8) + x / 8) * 256 + (size_t)e * 4;
}

/* Finds the button-icon font and its atlas in a whole package held in memory: the font whose
 * glyphs all lie in U+F080..U+F1FF, and the texture its name hash names. 1 when found. */
static inline int ipi_locate(const uint8_t *file, size_t size, IpiAtlas *out) {
    if (size < 0x98 || memcmp(file, "KCAP", 4)) return 0;
    const uint64_t doff = ipi_u32(file + 0x28);
    if (doff >= size) return 0;
    const uint8_t *d = file + doff;
    const uint64_t dsize = size - doff;
    for (uint64_t f = 0; f + 0x38 <= dsize; f += 8) {
        const uint64_t table = ipi_u64(d + f + 0x28);
        const uint32_t count = ipi_u32(d + f + 0x30);
        if (count < 4 || count > IPI_MAX_GLYPHS || table & 3 || table + (uint64_t)count * 32 > dsize) continue;
        uint32_t i = 0;
        for (; i < count; ++i) {
            const uint16_t cp = ipi_u16(d + table + i * 32);
            if (cp < IPI_FIRST_CP || cp > IPI_LAST_CP) break;
        }
        if (i < count) continue;
        const uint64_t name = ipi_u64(d + f + 0x18);
        for (uint64_t t = 0; t + 0x30 <= dsize; t += 8) {
            if (ipi_u64(d + t + 0x28) != name || t == f) continue;
            const uint8_t *ts = d + t + 0x08;
            const uint32_t w0 = ipi_u32(ts), w1 = ipi_u32(ts + 4), w2 = ipi_u32(ts + 8), w3 = ipi_u32(ts + 12);
            const uint32_t width = (w2 & 0x3fff) + 1, height = (w2 >> 14 & 0x3fff) + 1;
            if ((w1 >> 20 & 0x3f) != 0xA || (w3 >> 20 & 0x1f) != 13 || width % 8 || height % 8) continue;
            if ((uint64_t)w0 + (uint64_t)width * height * 4 > dsize) continue;
            out->pixels = doff + w0;
            out->width = width;
            out->height = height;
            out->count = count;
            for (i = 0; i < count; ++i) {
                const uint8_t *g = d + table + i * 32;
                out->glyphs[i] = (IpiGlyph){ipi_u16(g), ipi_u16(g + 2), ipi_u16(g + 4), ipi_u16(g + 6), ipi_u16(g + 8)};
                if (out->glyphs[i].x + out->glyphs[i].w > width || out->glyphs[i].y + out->glyphs[i].h > height) return 0;
            }
            return 1;
        }
    }
    return 0;
}

/* Prompt styles. */
enum { IPI_STYLE_PS = 0, IPI_STYLE_XBOX = 1, IPI_STYLE_KEYBOARD = 2 };

#ifdef __cplusplus
extern "C" {
#endif
/* gpu/shim/tlg_prompt_icons.cpp. open: a text package was opened (host path for reading it
 * whole); part: bytes [offset, offset+size) of it were just read into `buffer` (guest memory);
 * close: the descriptor is gone. style: the device the player used last (IPI_STYLE_*); the
 * shim rewrites atlases already in memory when it changes (TLG_PROMPTS=auto|ps|xbox|keyboard). */
void iss_prompt_icons_open(int fd, const char *host_path);
void iss_prompt_icons_part(int fd, void *buffer, int64_t offset, int64_t size);
void iss_prompt_icons_close(int fd);
void iss_prompt_icons_device(int style);
/* runtime_pad.c: a short label of the keyboard/mouse source bound to a PS4 button bit (BTN_*),
 * e.g. "SPACE", "LMB"; NULL when none is bound. */
const char *runtime_pad_key_label(uint32_t ps_button);
#ifdef __cplusplus
}
#endif
#endif
