/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Native renderer, game side: hooks that bracket engine effect draws with the BEGIN/END markers of
 * gpu_v2/gpu/native_render_core.h, so the renderer can run its own pass in their place.
 *
 * Each hook is a CALL SITE: the 5-byte `call rel32` that runs one effect's draw routine. Its target
 * is redirected to a 14-byte stub (`jmp *0(%rip)`) that jumps to a host wrapper. The wrapper writes
 * BEGIN into the draw command buffer, calls the game's routine with the same arguments (the routine
 * always runs: the commands it records are the fallback), then writes END. Nothing else in the
 * image changes, and a site is only written when the call there is exactly the expected one and
 * its target starts with the expected bytes.
 *
 * The draw routines all take (effect, render context, ...) in the System V registers, and the
 * render context's DrawCommandBuffer writer is at +8 (src/iss_temporal_hook.h, the same writer).
 * Images reach the routines as pointers to their T# (8 dwords), which the wrapper copies into the
 * marker so the renderer can find the image without reading game memory later.
 *
 * Off unless ISS_NATIVE_RENDER names a pass (or "probe"): then the hooks are installed and write
 * markers; the renderer decides per pass whether anything changes. CUSA00223 / CUSA00004 01.07. */
#ifndef ISS_NATIVE_RENDER_HOOK_H
#define ISS_NATIVE_RENDER_HOOK_H

#include <stdint.h>
#include <string.h>

#ifndef ISS_NATIVE_RENDER_ABI
#define ISS_NATIVE_RENDER_ABI __attribute__((sysv_abi))
#endif

/* --------------------------------------------------------------------------- the marker contract */
/* Must match gpu_v2/gpu/native_render_core.h (tests/test_native_render_hook.cpp checks it). */
enum {
    ISS_NR_MAGIC = 0x49534E00u,  /* 'ISN' | kind */
    ISS_NR_BEGIN = 1,
    ISS_NR_END = 2,
    ISS_NR_HEADER_WORDS = 3,     /* magic|kind, pass, sequence */
    ISS_NR_MAX_PAYLOAD = 29,
    /* Pass numbers (native_render_core.h Pass). */
    ISS_NR_PASS_COPY = 2,        /* CFxCopyTexture */
    ISS_NR_PASS_FXAA = 3,        /* CFxFXAA */
    /* Payloads. COPY: source T# (8), mode (the routine's channel selector, -1 = all). */
    ISS_NR_COPY_PAYLOAD = 9,
    /* FXAA: source T# (8), 1 if a second image is bound, its T# (8, zero when not). */
    ISS_NR_FXAA_PAYLOAD = 17
};

/* --------------------------------------------------------------------------- eboot sites (1.07) */
enum {
    ISS_NR_COPY_DRAW = 0x4560f0, /* CFxCopyTexture draw(effect, ctx, T* src, rcx, r8, r9d mode, s0, s1) */
    ISS_NR_FXAA_DRAW = 0x1a71f0, /* CFxFXAA draw(effect, ctx, rdx, T* src, T* second or 0) */
    ISS_NR_SITE_LENGTH = 5,
    ISS_NR_STUB_BYTES = 16,
    ISS_NR_MAX_SITES = 8
};

typedef struct IssNrSite {
    uint32_t at;     /* the call instruction */
    uint32_t target; /* the routine it calls */
    uint32_t pass;
} IssNrSite;

/* The frame post chain's calls (the second chain is another view of the same chain). */
static const IssNrSite iss_nr_sites[] = {
    {0x192cfa, ISS_NR_FXAA_DRAW, ISS_NR_PASS_FXAA},
    {0x1959f4, ISS_NR_FXAA_DRAW, ISS_NR_PASS_FXAA},
    {0x192db2, ISS_NR_COPY_DRAW, ISS_NR_PASS_COPY},
    {0x1932d1, ISS_NR_COPY_DRAW, ISS_NR_PASS_COPY},
    {0x195ac8, ISS_NR_COPY_DRAW, ISS_NR_PASS_COPY},
};
enum { ISS_NR_SITE_COUNT = (int)(sizeof(iss_nr_sites) / sizeof(iss_nr_sites[0])) };

/* Both routines open with the same frame setup; the full check is the call plus these bytes. */
static const unsigned char iss_nr_draw_prologue[16] = {0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56,
                                                       0x41, 0x55, 0x41, 0x54, 0x53, 0x48, 0x83, 0xec};

static inline uint32_t iss_nr_call_target(const unsigned char *image, uint32_t at) {
    int32_t rel;
    memcpy(&rel, image + at + 1, 4);
    return (uint32_t)((int64_t)at + 5 + rel);
}

/* 1 when every site is a call to its routine and every routine has the expected prologue. */
static inline int iss_nr_signature_match(const unsigned char *image, uint64_t size) {
    for (int i = 0; i < ISS_NR_SITE_COUNT; ++i) {
        const IssNrSite *s = &iss_nr_sites[i];
        if (size < (uint64_t)s->at + ISS_NR_SITE_LENGTH || size < (uint64_t)s->target + 16) return 0;
        if (image[s->at] != 0xe8 || iss_nr_call_target(image, s->at) != s->target) return 0;
        if (memcmp(image + s->target, iss_nr_draw_prologue, sizeof(iss_nr_draw_prologue))) return 0;
    }
    return 1;
}

/* ------------------------------------------------------------------------------ the shared state */
typedef void(ISS_NATIVE_RENDER_ABI *IssNrCopyFn)(void *effect, unsigned char *ctx, const void *src,
                                                   uint64_t rcx, uint64_t r8, uint64_t r9, uint64_t s0,
                                                   uint64_t s1);
typedef void(ISS_NATIVE_RENDER_ABI *IssNrFxaaFn)(void *effect, unsigned char *ctx, uint64_t rdx,
                                                   const void *src, const void *second, uint64_t r9);

typedef struct IssNrState {
    volatile uint8_t active;     /* markers are written (installed and wanted) */
    uint8_t installed;
    volatile uint32_t sequence;
    volatile uint32_t written, failed; /* markers written / not written (no room) */
    IssNrCopyFn copy;
    IssNrFxaaFn fxaa;
} IssNrState;

static IssNrState iss_nr_state;

/* Appends one marker to the render context's draw command buffer (writer at ctx + 8: end +0x10,
 * cursor +0x18, grow +0x20, grow argument +0x28, as in iss_temporal_write_marker). 1 when written. */
typedef char(ISS_NATIVE_RENDER_ABI *IssNrGrow)(void *writer, uint32_t dwords, void *argument);
static inline int iss_nr_write_marker(unsigned char *ctx, uint32_t kind, uint32_t pass, uint32_t sequence,
                                      const uint32_t *payload, uint32_t payload_words) {
    if (!ctx || payload_words > ISS_NR_MAX_PAYLOAD) return 0;
    const uint32_t dwords = 1 + ISS_NR_HEADER_WORDS + payload_words;
    unsigned char *writer = ctx + 8;
    unsigned char *end, *cursor;
    memcpy(&end, writer + 8, sizeof(end));
    memcpy(&cursor, writer + 0x10, sizeof(cursor));
    if (!end || !cursor) return 0;
    if ((uint64_t)((end - cursor) >> 2) < dwords) {
        IssNrGrow grow;
        void *argument;
        memcpy(&grow, writer + 0x18, sizeof(grow));
        memcpy(&argument, writer + 0x20, sizeof(argument));
        if (!grow || !grow(writer, dwords, argument)) return 0;
        memcpy(&end, writer + 8, sizeof(end));
        memcpy(&cursor, writer + 0x10, sizeof(cursor));
        if ((uint64_t)((end - cursor) >> 2) < dwords) return 0;
    }
    uint32_t packet[1 + ISS_NR_HEADER_WORDS + ISS_NR_MAX_PAYLOAD];
    packet[0] = (3u << 30) | (((ISS_NR_HEADER_WORDS + payload_words - 1) & 0x3FFFu) << 16) | (0x10u << 8);
    packet[1] = ISS_NR_MAGIC | kind;
    packet[2] = pass;
    packet[3] = sequence;
    if (payload_words) memcpy(packet + 4, payload, payload_words * 4);
    memcpy(cursor, packet, dwords * 4);
    cursor += dwords * 4;
    memcpy(writer + 0x10, &cursor, sizeof(cursor));
    return 1;
}

static inline uint32_t iss_nr_next_sequence(void) {
    return __atomic_add_fetch(&iss_nr_state.sequence, 1, __ATOMIC_RELAXED);
}

static inline void iss_nr_count(int ok) {
    if (ok) __atomic_add_fetch(&iss_nr_state.written, 1, __ATOMIC_RELAXED);
    else __atomic_add_fetch(&iss_nr_state.failed, 1, __ATOMIC_RELAXED);
}

/* ------------------------------------------------------------------------------- the wrappers */
/* The game's routine always runs between the markers. If BEGIN could not be written, END is not
 * written either; if END cannot be written, the renderer finds no END and keeps the engine's
 * commands. */
static ISS_NATIVE_RENDER_ABI void iss_nr_copy_wrapper(void *effect, unsigned char *ctx, const void *src,
                                                      uint64_t rcx, uint64_t r8, uint64_t r9, uint64_t s0,
                                                      uint64_t s1) {
    int open = 0;
    uint32_t seq = 0;
    if (iss_nr_state.active && src) {
        uint32_t payload[ISS_NR_COPY_PAYLOAD];
        memcpy(payload, src, 32);
        payload[8] = (uint32_t)r9;
        seq = iss_nr_next_sequence();
        open = iss_nr_write_marker(ctx, ISS_NR_BEGIN, ISS_NR_PASS_COPY, seq, payload, ISS_NR_COPY_PAYLOAD);
        iss_nr_count(open);
    }
    iss_nr_state.copy(effect, ctx, src, rcx, r8, r9, s0, s1);
    if (open) iss_nr_count(iss_nr_write_marker(ctx, ISS_NR_END, ISS_NR_PASS_COPY, seq, NULL, 0));
}

static ISS_NATIVE_RENDER_ABI void iss_nr_fxaa_wrapper(void *effect, unsigned char *ctx, uint64_t rdx,
                                                      const void *src, const void *second, uint64_t r9) {
    int open = 0;
    uint32_t seq = 0;
    if (iss_nr_state.active && src) {
        uint32_t payload[ISS_NR_FXAA_PAYLOAD];
        memset(payload, 0, sizeof(payload));
        memcpy(payload, src, 32);
        payload[8] = second ? 1u : 0u;
        if (second) memcpy(payload + 9, second, 32);
        seq = iss_nr_next_sequence();
        open = iss_nr_write_marker(ctx, ISS_NR_BEGIN, ISS_NR_PASS_FXAA, seq, payload, ISS_NR_FXAA_PAYLOAD);
        iss_nr_count(open);
    }
    iss_nr_state.fxaa(effect, ctx, rdx, src, second, r9);
    if (open) iss_nr_count(iss_nr_write_marker(ctx, ISS_NR_END, ISS_NR_PASS_FXAA, seq, NULL, 0));
}

/* ------------------------------------------------------------------------------- installation */
static inline int iss_nr_call_in_range(const unsigned char *site, const unsigned char *stub) {
    const int64_t delta = (int64_t)(intptr_t)stub - (int64_t)(intptr_t)(site + 5);
    return delta >= INT32_MIN && delta <= INT32_MAX;
}

/* Writes one stub per site into `stubs` (ISS_NR_SITE_COUNT * ISS_NR_STUB_BYTES bytes, made
 * executable by the caller afterwards) and redirects the calls. 0, with the image untouched, when
 * the code is not this eboot's or a stub is out of reach of the call. */
static inline int iss_nr_install_hooks(unsigned char *image, uint64_t size, unsigned char *stubs) {
    if (!iss_nr_signature_match(image, size)) return 0;
    for (int i = 0; i < ISS_NR_SITE_COUNT; ++i)
        if (!iss_nr_call_in_range(image + iss_nr_sites[i].at, stubs + i * ISS_NR_STUB_BYTES)) return 0;
    iss_nr_state.copy = (IssNrCopyFn)(uintptr_t)(image + ISS_NR_COPY_DRAW);
    iss_nr_state.fxaa = (IssNrFxaaFn)(uintptr_t)(image + ISS_NR_FXAA_DRAW);
    for (int i = 0; i < ISS_NR_SITE_COUNT; ++i) {
        const IssNrSite *s = &iss_nr_sites[i];
        unsigned char *stub = stubs + i * ISS_NR_STUB_BYTES;
        const uintptr_t wrapper = s->pass == ISS_NR_PASS_FXAA ? (uintptr_t)&iss_nr_fxaa_wrapper
                                                              : (uintptr_t)&iss_nr_copy_wrapper;
        static const unsigned char jump[6] = {0xff, 0x25, 0, 0, 0, 0}; /* jmp *0(%rip) */
        memcpy(stub, jump, 6);
        memcpy(stub + 6, &wrapper, 8);
        memset(stub + 14, 0xcc, ISS_NR_STUB_BYTES - 14);
    }
    for (int i = 0; i < ISS_NR_SITE_COUNT; ++i) {
        unsigned char *site = image + iss_nr_sites[i].at;
        const int32_t rel = (int32_t)((int64_t)(intptr_t)(stubs + i * ISS_NR_STUB_BYTES) -
                                      (int64_t)(intptr_t)(site + 5));
        memcpy(site + 1, &rel, 4);
    }
    iss_nr_state.installed = 1;
    iss_nr_state.active = 1;
    return 1;
}

#endif
