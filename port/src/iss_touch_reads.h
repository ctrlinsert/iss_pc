/* SPDX-License-Identifier: GPL-2.0-or-later
 * US/EU 01.07: count the game scripts' reads of the touchpad, for the interact key's auto-detect.
 *
 * The scripts read the touchpad through six 24-byte thunks, reached only by the script VM's
 * indirect dispatch (the renderer reads the pad's touch state directly, not through these):
 *   0x24df60 TOUCH_X, 0x24df80 TOUCH_Y, 0x24dfa0 TOUCH_X2, 0x24dfc0 TOUCH_Y2,
 *   0x24dfe0 NUM_TOUCH, 0x24e000 TOUCH_ASPECT_RATIO.
 * Each is: mov rdi,[rdi+0x28]; test rdi,rdi; cmove rdi,[rip+X] (the global pad);
 *          mov rax,[rdi]; jmp [rax+slot].
 * A swipe prompt has to read where the finger is (X/Y); holds and drains need no position. So
 * while nothing reads the position, the interact key only taps and holds (runtime_pad.c).
 *
 * Each thunk is replaced by a jump to a stub that bumps a counter and does the same thing: the
 * stub rebuilds the thunk with absolute addresses, so nothing rip-relative is moved. */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

enum { ISS_TOUCH_READ_GETTERS = 6, ISS_TOUCH_READ_THUNK = 24, ISS_TOUCH_READ_STUB = 64 };
enum { ISS_TOUCH_X, ISS_TOUCH_Y, ISS_TOUCH_X2, ISS_TOUCH_Y2, ISS_TOUCH_NUM, ISS_TOUCH_ASPECT };

static const uint32_t iss_touch_read_sites[ISS_TOUCH_READ_GETTERS] = {
    0x24df60, 0x24df80, 0x24dfa0, 0x24dfc0, 0x24dfe0, 0x24e000 };
static const uint32_t iss_touch_read_slots[ISS_TOUCH_READ_GETTERS] = { 0xa0, 0xa8, 0xb0, 0xb8, 0xc0, 0xc8 };

/* The thunk's bytes, with its rip-relative pad pointer (bytes 11..14) left out of the check.
 * Returns the address the cmove reads (the global pad's slot), or 0 if the code differs. */
static inline uintptr_t iss_touch_read_check(const unsigned char *image, size_t size, unsigned i) {
    if (!image || i >= ISS_TOUCH_READ_GETTERS) return 0;
    const uint32_t at = iss_touch_read_sites[i];
    if (at > size || ISS_TOUCH_READ_THUNK > size - at) return 0;
    const unsigned char *p = image + at;
    static const unsigned char head[11] = {0x48,0x8b,0x7f,0x28, 0x48,0x85,0xff, 0x48,0x0f,0x44,0x3d};
    static const unsigned char tail[5] = {0x48,0x8b,0x07, 0xff,0xa0};
    if (memcmp(p, head, sizeof(head)) || memcmp(p + 15, tail, sizeof(tail))) return 0;
    uint32_t slot; memcpy(&slot, p + 20, 4); /* jmp [rax+disp32] */
    if (slot != iss_touch_read_slots[i]) return 0;
    int32_t rel; memcpy(&rel, p + 11, 4);
    return (uintptr_t)(p + 15) + (intptr_t)rel;
}

/* The stub: lock inc [counter]; the thunk with the pad slot as an absolute address. rax and the
 * flags are free at a function entry (rax is set from the pad right after, as the thunk does). */
static inline unsigned iss_touch_read_build_stub(unsigned char *s, uintptr_t counter, uintptr_t pad_slot,
                                                 uint32_t vslot) {
    unsigned n = 0;
    s[n++] = 0x48; s[n++] = 0xb8; memcpy(s + n, &counter, 8); n += 8;    /* mov rax, counter */
    s[n++] = 0xf0; s[n++] = 0x48; s[n++] = 0xff; s[n++] = 0x00;          /* lock inc qword [rax] */
    s[n++] = 0x48; s[n++] = 0x8b; s[n++] = 0x7f; s[n++] = 0x28;          /* mov rdi,[rdi+0x28] */
    s[n++] = 0x48; s[n++] = 0x85; s[n++] = 0xff;                         /* test rdi,rdi */
    s[n++] = 0x48; s[n++] = 0xb8; memcpy(s + n, &pad_slot, 8); n += 8;   /* mov rax, pad slot (flags kept) */
    s[n++] = 0x48; s[n++] = 0x0f; s[n++] = 0x44; s[n++] = 0x38;          /* cmove rdi,[rax] */
    s[n++] = 0x48; s[n++] = 0x8b; s[n++] = 0x07;                         /* mov rax,[rdi] */
    s[n++] = 0xff; s[n++] = 0xa0; memcpy(s + n, &vslot, 4); n += 4;      /* jmp [rax+slot] */
    return n;
}

#ifdef __cplusplus
extern "C" {
#endif
/* Script reads of each getter since start (runtime_pad.c); 1 once the hook is in. */
extern volatile uint64_t iss_touch_reads[ISS_TOUCH_READ_GETTERS];
extern volatile int iss_touch_reads_active;
#ifdef __cplusplus
}
#endif
