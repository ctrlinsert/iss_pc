// SPDX-License-Identifier: GPL-2.0-or-later
/* Skill tree preview videos: mark a video stream busy in the same critical section that hands it
 * to the stream worker.
 *
 * The worker's dequeue picks a stream under the stream lock S and drops the lock, but the stream
 * is only marked busy (byte +0x2841) much later, after the decoder is created and reset. When the
 * menu cancels a preview in that gap, its cancel path sees busy=0 and destroys the stream, and the
 * worker then carries on with freed memory. With busy already set, the cancel takes the game's own
 * deferred-stop path, and the worker's single busy release applies it. busy is a byte flag, so the
 * worker setting it again later changes nothing.
 *
 * The patch replaces the dequeue's "store depth; if depth is 0 unlock" tail with a jump to a stub
 * that sets busy on the chosen stream (rbx, 0 when nothing was dequeued) and then does the same
 * tail. Registers, flags and the stack at the resume point match the original. */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

enum {
    ISS_STREAM_BUSY_SITE = 0x28ea2e,   /* mov [r13+0x18],eax; test eax,eax; jne; mov rdi,r14; call unlock */
    ISS_STREAM_BUSY_RESUME = 0x28ea3e,
    ISS_STREAM_BUSY_UNLOCK = 0x9845d0, /* scePthreadMutexUnlock thunk */
    ISS_STREAM_BUSY_LOCK_LEA = 0x28e908, /* lea r13,[S] */
    ISS_STREAM_BUSY_FLAG = 0x2841,
};
static const unsigned char iss_stream_busy_site_bytes[16] = {
    0x41,0x89,0x45,0x18, 0x85,0xc0, 0x75,0x08, 0x4c,0x89,0xf7, 0xe8,0x92,0x5b,0x6f,0x00};
static const unsigned char iss_stream_busy_lea_bytes[7] = {0x4c,0x8d,0x2d,0x91,0x11,0xa9,0x01};

static inline int iss_stream_busy_valid(const unsigned char *image, size_t size) {
    return image && size >= ISS_STREAM_BUSY_SITE + sizeof(iss_stream_busy_site_bytes) &&
           !memcmp(image + ISS_STREAM_BUSY_SITE, iss_stream_busy_site_bytes, sizeof(iss_stream_busy_site_bytes)) &&
           !memcmp(image + ISS_STREAM_BUSY_LOCK_LEA, iss_stream_busy_lea_bytes, sizeof(iss_stream_busy_lea_bytes));
}

/* The stub, with absolute addresses for the unlock thunk and the resume point. Returns its size. */
static inline size_t iss_stream_busy_stub(unsigned char *out, uintptr_t base) {
    size_t n = 0;
    const unsigned char head[] = {
        0x41,0x89,0x45,0x18,                     /* mov [r13+0x18],eax        (depth, as before) */
        0x48,0x85,0xdb,                          /* test rbx,rbx               (a stream dequeued?) */
        0x74,0x07,                               /* jz +7 */
        0xc6,0x83,0x41,0x28,0x00,0x00,0x01,      /* mov byte [rbx+0x2841],1    (busy, still under S) */
        0x85,0xc0,                               /* test eax,eax               (flags as the original) */
        0x75,0x0f,                               /* jnz +15                    (still nested: no unlock) */
        0x4c,0x89,0xf7,                          /* mov rdi,r14 */
        0x48,0xb8};                              /* movabs rax, unlock thunk */
    memcpy(out + n, head, sizeof(head)); n += sizeof(head);
    uintptr_t unlock = base + ISS_STREAM_BUSY_UNLOCK;
    memcpy(out + n, &unlock, 8); n += 8;
    out[n++] = 0xff; out[n++] = 0xd0;            /* call rax                   (same rsp as the original call) */
    const unsigned char jump[] = {0xff,0x25,0,0,0,0};
    memcpy(out + n, jump, sizeof(jump)); n += sizeof(jump);
    uintptr_t resume = base + ISS_STREAM_BUSY_RESUME;
    memcpy(out + n, &resume, 8); n += 8;
    return n;
}

/* The 16 site bytes: an absolute jump to the stub, then two NOPs (never executed). */
static inline void iss_stream_busy_site(unsigned char *site, uintptr_t stub) {
    const unsigned char jump[] = {0xff,0x25,0,0,0,0};
    memcpy(site, jump, sizeof(jump));
    memcpy(site + 6, &stub, 8);
    site[14] = 0x90; site[15] = 0x90;
}

/* Second part: the stream error/kill path (0x28a5c0). Under S it dispatches on the stream state
 * through a rel32 jump table. States 0, 1 and 7 go straight to "move to stopped (8)", which the
 * owner then deletes; states 2-4 defer. The worker is inside a state 1 or 7 stream from its
 * dequeue until it has created and reset the decoder, so with busy set at dequeue a kill there
 * still deleted the stream under the worker. Entries 1 and 7 now go to a stub: busy leaves the
 * state alone (the error is already recorded, so the worker's decode loop stops and its busy
 * release moves the stream to 5 or 8 as for any other stream); idle keeps the original move to 8.
 * The stub only changes flags, and both targets write flags before reading them. */
enum {
    ISS_STREAM_KILL_DISPATCH = 0x28a66c, /* mov eax,[rbx+0x38] ... call 0x28a700 */
    ISS_STREAM_KILL_STOP = 0x28a6a8,     /* lea rdi,[S]; mov edx,8; mov rsi,rbx; call 0x28d0e0 */
    ISS_STREAM_KILL_SKIP = 0x28a6bc,     /* mov rdi,rbx; call 0x28a700 (no state change) */
    ISS_STREAM_KILL_TABLE = 0x9a5894,
};
static const unsigned char iss_stream_kill_dispatch_bytes[88] = {
    0x8b,0x43,0x38, 0x48,0x83,0xf8,0x07, 0x77,0x47, 0x48,0x8d,0x0d,0x18,0xb2,0x71,0x00,
    0x48,0x63,0x04,0x81, 0x48,0x01,0xc8, 0xff,0xe0,
    0x48,0x8d,0x3d,0x14,0x54,0xa9,0x01, 0xba,0x05,0x00,0x00,0x00, 0xeb,0x21,
    0x66,0x8b,0x83,0x40,0x28,0x00,0x00, 0x84,0xc0, 0x75,0x1e, 0x0f,0xb7,0xc0, 0x3d,0xff,0x00,0x00,0x00, 0x77,0x14,
    0x48,0x8d,0x3d,0xf1,0x53,0xa9,0x01, 0xba,0x08,0x00,0x00,0x00,
    0x48,0x89,0xde, 0xe8,0x24,0x2a,0x00,0x00,
    0x48,0x89,0xdf, 0xe8,0x3c,0x00,0x00,0x00};
static const unsigned char iss_stream_kill_table_bytes[32] = {
    0x14,0x4e,0x8e,0xff, 0x14,0x4e,0x8e,0xff, 0xf1,0x4d,0x8e,0xff, 0xf1,0x4d,0x8e,0xff,
    0xf1,0x4d,0x8e,0xff, 0xff,0x4d,0x8e,0xff, 0x28,0x4e,0x8e,0xff, 0x14,0x4e,0x8e,0xff};

static inline int iss_stream_kill_valid(const unsigned char *image, size_t size) {
    return image && size >= ISS_STREAM_KILL_TABLE + sizeof(iss_stream_kill_table_bytes) &&
           !memcmp(image + ISS_STREAM_KILL_DISPATCH, iss_stream_kill_dispatch_bytes, sizeof(iss_stream_kill_dispatch_bytes)) &&
           !memcmp(image + ISS_STREAM_KILL_TABLE, iss_stream_kill_table_bytes, sizeof(iss_stream_kill_table_bytes));
}

/* cmp byte [rbx+0x2841],0; jne skip; jmp [rip] stop; skip: jmp [rip] no-change. Returns 37. */
static inline size_t iss_stream_kill_stub(unsigned char *out, uintptr_t base) {
    const unsigned char head[] = {0x80,0xbb,0x41,0x28,0x00,0x00,0x00, 0x75,0x0e};
    size_t n = 0;
    memcpy(out, head, sizeof(head)); n += sizeof(head);
    const unsigned char jump[] = {0xff,0x25,0,0,0,0};
    uintptr_t stop = base + ISS_STREAM_KILL_STOP, skip = base + ISS_STREAM_KILL_SKIP;
    memcpy(out + n, jump, 6); n += 6; memcpy(out + n, &stop, 8); n += 8;
    memcpy(out + n, jump, 6); n += 6; memcpy(out + n, &skip, 8); n += 8;
    return n;
}

/* Points table entries 1 and 7 at the stub. The stub must sit within rel32 reach of the table
 * (it lives in a page allocated with the image). Returns 0 when it is out of reach. */
static inline int iss_stream_kill_table(unsigned char *image, uintptr_t stub) {
    intptr_t rel = (intptr_t)stub - (intptr_t)(uintptr_t)(image + ISS_STREAM_KILL_TABLE);
    if (rel < INT32_MIN || rel > INT32_MAX) return 0;
    int32_t entry = (int32_t)rel;
    memcpy(image + ISS_STREAM_KILL_TABLE + 4 * 1, &entry, 4);
    memcpy(image + ISS_STREAM_KILL_TABLE + 4 * 7, &entry, 4);
    return 1;
}
