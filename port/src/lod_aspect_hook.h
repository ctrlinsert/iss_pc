/* SPDX-License-Identifier: GPL-2.0-or-later
 * LOD at any aspect (CUSA00223/CUSA00004 01.07). The game scales its distance-based detail tests
 * by the camera's horizontal tangent (camera +0x5a0). With the native ultrawide output (Hor+, same
 * vertical FOV) that tangent grows with the aspect: at 32:9 it is twice the 16:9 value, so every
 * LOD and detail threshold acts at half the distance and aim zoom no longer reaches the next LOD.
 * These four sites read the 16:9-equivalent tangent instead, tan_y (+0x5a4) * 16/9: identical at
 * 16:9, and at any other aspect the same choices as 16:9 at the same vertical FOV (aim zoom still
 * shrinks tan_y). Projection, frustum and culling keep the real tangent.
 *   0x435baf  vmulss xmm0,xmm0,[rax+0x5a0]   per-level LOD switch factor (LOD table row * tan_x)
 *   0x1fe96f  vmovss xmm1,[r14+0x5a0]        detail test: tan_x^2 * distance^2 against a constant
 *   0x4ef053  vmovss xmm1,[r14+0x5a0]        per-object detail: tan_x * k * depth against a constant
 *   0x4ef3d4  vmovss xmm1,[r14+0x5a0]        same, second path
 * ISS_LOD_ASPECT=0 leaves the game as shipped. */
#ifndef ISS_LOD_ASPECT_HOOK_H
#define ISS_LOD_ASPECT_HOOK_H
#include <stdint.h>
#include <string.h>

#define ISS_LOD_ASPECT_SITES 4u

static const uint32_t iss_lod_aspect_site[ISS_LOD_ASPECT_SITES] = {0x435bafu, 0x1fe96fu, 0x4ef053u, 0x4ef3d4u};
static const unsigned iss_lod_aspect_length[ISS_LOD_ASPECT_SITES] = {8u, 9u, 9u, 9u};
static const unsigned char iss_lod_aspect_mul_rax[8] = {0xc5, 0xfa, 0x59, 0x80, 0xa0, 0x05, 0x00, 0x00};
static const unsigned char iss_lod_aspect_mov_r14[9] = {0xc4, 0xc1, 0x7a, 0x10, 0x8e, 0xa0, 0x05, 0x00, 0x00};

static inline const unsigned char *iss_lod_aspect_original(unsigned i) {
    return i == 0 ? iss_lod_aspect_mul_rax : iss_lod_aspect_mov_r14;
}

/* Every site holds its original instruction (this eboot build, nothing else patched it). */
static inline int iss_lod_aspect_signature_match(const unsigned char *image, uint64_t size) {
    for (unsigned i = 0; i < ISS_LOD_ASPECT_SITES; ++i) {
        if (size < (uint64_t)iss_lod_aspect_site[i] + iss_lod_aspect_length[i]) return 0;
        if (memcmp(image + iss_lod_aspect_site[i], iss_lod_aspect_original(i), iss_lod_aspect_length[i])) return 0;
    }
    return 1;
}

/* Writes the stub for site i at `stub` (at most 48 bytes): the tangent from +0x5a4, times the
 * float at `ratio` (16/9), then an absolute jump back after the site. Returns its length, 0 when
 * the ratio is out of rip-relative range. */
static inline unsigned iss_lod_aspect_build_stub(unsigned char *stub, unsigned i, const float *ratio,
                                                 uintptr_t resume) {
    unsigned n = 0;
    if (i == 0) {
        /* vmulss xmm0,xmm0,[rax+0x5a4]; vmulss xmm0,xmm0,[rip+ratio] */
        static const unsigned char a[8] = {0xc5, 0xfa, 0x59, 0x80, 0xa4, 0x05, 0x00, 0x00};
        memcpy(stub + n, a, 8); n += 8;
        stub[n++] = 0xc5; stub[n++] = 0xfa; stub[n++] = 0x59; stub[n++] = 0x05;
    } else {
        /* vmovss xmm1,[r14+0x5a4]; vmulss xmm1,xmm1,[rip+ratio] */
        static const unsigned char a[9] = {0xc4, 0xc1, 0x7a, 0x10, 0x8e, 0xa4, 0x05, 0x00, 0x00};
        memcpy(stub + n, a, 9); n += 9;
        stub[n++] = 0xc5; stub[n++] = 0xf2; stub[n++] = 0x59; stub[n++] = 0x0d;
    }
    const int64_t rel = (int64_t)(uintptr_t)ratio - (int64_t)(uintptr_t)(stub + n + 4);
    if (rel < INT32_MIN || rel > INT32_MAX) return 0;
    const int32_t r32 = (int32_t)rel;
    memcpy(stub + n, &r32, 4); n += 4;
    /* jmp qword [rip+0]; .quad resume */
    stub[n++] = 0xff; stub[n++] = 0x25; stub[n++] = 0; stub[n++] = 0; stub[n++] = 0; stub[n++] = 0;
    memcpy(stub + n, &resume, 8); n += 8;
    return n;
}
#endif
