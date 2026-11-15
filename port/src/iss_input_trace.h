/* SPDX-License-Identifier: GPL-2.0-or-later */
/* ISS_INPUT_TRACE=1: an observer of how the game asks for input, to plan native keyboard and
 * mouse support (which actions and which raw buttons each screen reads, and from where).
 * Nothing the game sees changes. Hooked, US/EU 1.07 only, each guarded by its entry bytes:
 *   ISS_BTNK_LOOKUP   button kind (the game's own action numbers) -> pad mask;
 *                     every action query, C++ or script, goes through it. Replaced by the same
 *                     table read plus a record.
 *   ISS_PAD_PRESSED   controller "pressed within a window" (raw masks too: menus, script);
 *   ISS_PAD_HELD      controller "pressed and still down"; both observed at entry (bridge).
 *   ISS_PAD_DOWN      the IS_BUTTON_DOWN script native (level of a raw mask). Replaced.
 * Each distinct (kind, value, caller, menu scene) is logged once as INPUT_TRACE. */
#ifndef ISS_INPUT_TRACE_H
#define ISS_INPUT_TRACE_H
#include <stdint.h>
#include <string.h>

#define ISS_BTNK_LOOKUP 0x15dde0u
#define ISS_BTNK_TABLE 0xe17220u        /* u32[ISS_BTNK_COUNT], filled per control scheme */
#define ISS_BTNK_COUNT 50u
#define ISS_PAD_PRESSED 0x248b40u
#define ISS_PAD_PRESSED_STOLEN 17u
#define ISS_PAD_HELD 0x248ad0u
#define ISS_PAD_HELD_STOLEN 14u
#define ISS_PAD_DOWN 0x24ddc0u
#define ISS_PAD_DEFAULT 0xd9de90u       /* pointer to the default controller */

static const unsigned char iss_btnk_lookup_bytes[]={0x31,0xc0,0x83,0xff,0xff,0x74,0x0d,0x48,0x8d,0x0d,0x32,0x94,0xcb,0x00,
    0x48,0x63,0xc7,0x8b,0x04,0x81,0xc3};
static const unsigned char iss_pad_pressed_bytes[]={0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,
    0x48,0x83,0xec,0x18};
static const unsigned char iss_pad_held_bytes[]={0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x54,0x53,0x41,0x89,0xf4};
static const unsigned char iss_pad_down_bytes[]={0x48,0x8b,0x47,0x28,0x48,0x85,0xc0,0x48,0x0f,0x44,0x05,0xc1,0x00,0xb5,0x00,
    0x85,0x70,0x20,0x0f,0x95,0xc0,0xc3};

static inline int iss_input_trace_signature(const unsigned char *image, uint64_t size) {
    return size>ISS_BTNK_TABLE+4*ISS_BTNK_COUNT &&
        !memcmp(image+ISS_BTNK_LOOKUP,iss_btnk_lookup_bytes,sizeof(iss_btnk_lookup_bytes)) &&
        !memcmp(image+ISS_PAD_PRESSED,iss_pad_pressed_bytes,sizeof(iss_pad_pressed_bytes)) &&
        !memcmp(image+ISS_PAD_HELD,iss_pad_held_bytes,sizeof(iss_pad_held_bytes)) &&
        !memcmp(image+ISS_PAD_DOWN,iss_pad_down_bytes,sizeof(iss_pad_down_bytes));
}

enum { ISS_TRACE_BTNK=1, ISS_TRACE_PRESSED, ISS_TRACE_HELD, ISS_TRACE_DOWN };
/* A fixed open-addressing set of what was seen. Keys are never 0. */
#define ISS_TRACE_SLOTS 4096u
typedef struct { uint64_t key; uint32_t caller; } IssTraceSlot;
static inline uint64_t iss_trace_key(unsigned kind, uint32_t value, uint32_t caller, unsigned scene) {
    return (uint64_t)kind<<60 | (uint64_t)(scene&15)<<56 | (uint64_t)(value&0xffffffu)<<32 | caller;
}
/* 1 when the key is new (inserted), 0 when seen or the set is full. Single writer per slot is
 * enough for a log: a lost race only drops or repeats a line. */
static inline int iss_trace_insert(IssTraceSlot *slots, uint64_t key) {
    uint64_t h=key*0x9e3779b97f4a7c15ull;
    for (unsigned probe=0;probe<64;++probe) {
        IssTraceSlot *s=&slots[((h>>52)+probe) & (ISS_TRACE_SLOTS-1)];
        if (s->key==key) return 0;
        if (!s->key) { s->key=key; return 1; }
    }
    return 0;
}
#endif
