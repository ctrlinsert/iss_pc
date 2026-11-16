/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Native keyboard and mouse actions (inFAMOUS Second Son 1.07, CUSA00223 and CUSA00004).
 *
 * The game asks for most gameplay input by action kind, not by button: a kind is an index into
 * a per-control-scheme table of button masks (ISS_BTNK_TABLE, iss_input_trace.h), read by one
 * lookup function (ISS_BTNK_LOOKUP). The controller then tests the mask against its state word
 * (controller+0x20), which the pad read (ISS_PAD_READ) builds every frame.
 *
 * A native key presses actions instead of PS4 buttons: the pad read's end (ISS_PAD_READ_SITE,
 * a bridge) adds the masks of the actions whose keys are down to the state word. Actions that
 * share a button but have different keys (Melee and Execute: one button on PS4) are split: each
 * gets a bit the PS4 pad never sets (ISS_NATIVE_VBITS), the lookup returns the action's mask
 * plus its bit, and its key sets only the bit. The pad's own button still matches both, as on
 * PS4, and a query for any kind no action covers (the plain buttons, menu choices) still sees
 * either key.
 *
 * This file holds the pure parts: the action list, the split and the masks. */
#ifndef ISS_NATIVE_KEYS_H
#define ISS_NATIVE_KEYS_H
#include <stdint.h>
#include <string.h>

#define ISS_PAD_READ 0x24b340u
#define ISS_PAD_READ_SITE 0x24b443u    /* after the buttons; RBX is the controller */
#define ISS_PAD_READ_STOLEN 15u
#define ISS_PAD_STATE 0x20u            /* u32 button state word in the controller */
#define ISS_PAD_HANDLE 0xc8u           /* s32 pad handle: negative while no pad is open */
#define ISS_NATIVE_VBITS 0xfe000000u   /* state bits the pad read never sets */
#define ISS_NATIVE_KINDS 50u

/* Lookups whose caller needs the plain one-bit mask: the button prompt registry (0x56adb0 files
 * a prompt under the mask's bit number; a split bit there has no glyph, so the prompt went
 * missing and a blank one showed at the side) and a prompt check that compares masks for
 * equality (0x56b170). They get the table's mask without split bits. Return addresses. */
static const uint32_t iss_native_plain_callers[]={0x56adcau, 0x56b1a3u, 0x56b1b2u};
static inline int iss_native_plain_caller(uint32_t ret) {
    for (unsigned i=0;i<sizeof(iss_native_plain_callers)/sizeof(*iss_native_plain_callers);++i)
        if (ret==iss_native_plain_callers[i]) return 1;
    return 0;
}

static const unsigned char iss_pad_read_bytes[]={0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,
    0x48,0x83,0xec,0x28,0x48,0x89,0xfb,0x41,0xb6,0x7f};
static const unsigned char iss_pad_read_site_bytes[]={0x48,0x8d,0xbb,0x20,0x04,0x00,0x00,0x48,0x8d,0x4b,0x14,
    0x4c,0x8d,0x43,0x18};

/* The call before each plain caller's return address (call ISS_BTNK_LOOKUP, rel32). */
static inline int iss_native_plain_callers_valid(const unsigned char *image, uint64_t size) {
    for (unsigned i=0;i<sizeof(iss_native_plain_callers)/sizeof(*iss_native_plain_callers);++i) {
        const uint32_t ret=iss_native_plain_callers[i];
        if (ret<5 || ret>size || image[ret-5]!=0xe8) return 0;
        int32_t rel; memcpy(&rel,image+ret-4,4);
        if ((int64_t)ret+rel!=(int64_t)0x15dde0) return 0;
    }
    return 1;
}
static inline int iss_native_keys_signature(const unsigned char *image, uint64_t size) {
    return size>ISS_PAD_READ_SITE+ISS_PAD_READ_STOLEN && iss_native_plain_callers_valid(image,size) &&
        !memcmp(image+ISS_PAD_READ,iss_pad_read_bytes,sizeof(iss_pad_read_bytes)) &&
        !memcmp(image+ISS_PAD_READ_SITE,iss_pad_read_site_bytes,sizeof(iss_pad_read_site_bytes));
}

/* The bindable actions; each presses one or more of the game's action kinds. */
enum {
    ISS_NA_JUMP, ISS_NA_DASH, ISS_NA_MELEE, ISS_NA_EXECUTE, ISS_NA_GOOD, ISS_NA_USE, ISS_NA_DRAIN,
    ISS_NA_PUSH, ISS_NA_AIM, ISS_NA_FIRE, ISS_NA_SECONDARY, ISS_NA_HEAVY, ISS_NA_IONIC, ISS_NA_PING,
    ISS_NA_PAUSE, ISS_NA_INVESTIGATE, ISS_NA_SPRAY, ISS_NA_COUNT
};
typedef struct { const char *name; unsigned char kinds[4]; unsigned char count; } IssNativeAction;
static const IssNativeAction iss_native_actions[ISS_NA_COUNT]={
    {"jump",{0},1}, {"dash",{2},1}, {"melee",{3},1}, {"execute",{4},1}, {"good_choice",{5},1},
    {"use",{6,7,23},3}, {"drain",{8},1}, {"push",{25},1}, {"aim",{9},1}, {"fire",{10},1},
    {"secondary_power",{11},1}, {"heavy_power",{12},1}, {"ionic_power",{13},1}, {"ping",{14},1},
    {"pause",{15},1}, {"investigate",{16,17,18,24},4}, {"spray",{22},1},
};

/* The extra bit per kind (vbit[kind]): actions whose masks overlap another action's while
 * their keys differ (sig: a signature of each action's keys, 0 = unbound) get one bit each, in
 * order, while bits last. Kinds no action covers (the plain buttons, menu choices) get the bits
 * of every split action they overlap, so a query for the button itself still sees those keys. Returns the number of split actions. */
static inline int iss_native_split(const uint32_t table[ISS_NATIVE_KINDS], const uint64_t sig[ISS_NA_COUNT],
                                   uint32_t vbit[ISS_NATIVE_KINDS], uint32_t action_bit[ISS_NA_COUNT]) {
    uint32_t mask[ISS_NA_COUNT];
    for (int a=0;a<ISS_NA_COUNT;++a) {
        mask[a]=0;
        for (int i=0;i<iss_native_actions[a].count;++i) mask[a]|=table[iss_native_actions[a].kinds[i]] & ~ISS_NATIVE_VBITS;
        action_bit[a]=0;
    }
    memset(vbit,0,sizeof(uint32_t)*ISS_NATIVE_KINDS);
    uint32_t next=0x02000000u;
    int split=0;
    for (int a=0;a<ISS_NA_COUNT && next;++a) {
        if (!sig[a] || !mask[a]) continue;
        int conflict=0;
        for (int b=0;b<ISS_NA_COUNT && !conflict;++b) conflict=b!=a && (mask[a] & mask[b]) && sig[b]!=sig[a];
        if (!conflict) continue;
        action_bit[a]=next; next<<=1; ++split;
        for (int i=0;i<iss_native_actions[a].count;++i) vbit[iss_native_actions[a].kinds[i]]|=action_bit[a];
    }
    unsigned char covered[ISS_NATIVE_KINDS]={0};
    for (int a=0;a<ISS_NA_COUNT;++a)
        for (int i=0;i<iss_native_actions[a].count;++i) covered[iss_native_actions[a].kinds[i]]=1;
    for (unsigned k=0;k<ISS_NATIVE_KINDS;++k)
        for (int a=0;a<ISS_NA_COUNT && !covered[k];++a)
            if (action_bit[a] && (table[k] & mask[a])) vbit[k]|=action_bit[a];
    return split;
}

/* The state bits the held actions add: a split action's bit, else its kinds' masks. */
static inline uint32_t iss_native_state(const uint32_t table[ISS_NATIVE_KINDS], const uint32_t action_bit[ISS_NA_COUNT],
                                        uint32_t actions) {
    uint32_t bits=0;
    for (int a=0;a<ISS_NA_COUNT;++a) {
        if (!(actions>>a & 1)) continue;
        if (action_bit[a]) { bits|=action_bit[a]; continue; }
        for (int i=0;i<iss_native_actions[a].count;++i) bits|=table[iss_native_actions[a].kinds[i]] & ~ISS_NATIVE_VBITS;
    }
    return bits;
}
#endif
