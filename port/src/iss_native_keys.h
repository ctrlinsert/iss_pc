/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Native keyboard and mouse actions (inFAMOUS Second Son 1.07, CUSA00223 and CUSA00004).
 *
 * The game asks for most gameplay input by action kind (iss_action_input.h). A native key presses
 * actions, and each action is one or more of the game's kinds: the keyboard answers by kind
 * (runtime_native_keys.c), so actions that share a PS4 button (Melee and Execute) are separate
 * keys. The end of the pad read (ISS_PAD_READ_SITE, a bridge) is where the held actions become
 * the keyboard's kinds for that read.
 *
 * This file holds the pure parts: the action list and its kinds. */
#ifndef ISS_NATIVE_KEYS_H
#define ISS_NATIVE_KEYS_H
#include <stdint.h>
#include <string.h>

#define ISS_PAD_READ 0x24b340u
#define ISS_PAD_READ_SITE 0x24b443u    /* after the buttons; RBX is the controller */
#define ISS_PAD_READ_STOLEN 15u
#define ISS_PAD_STATE 0x20u            /* u32 button state word in the controller */
#define ISS_PAD_HANDLE 0xc8u           /* s32 pad handle: negative while no pad is open */

static const unsigned char iss_pad_read_bytes[]={0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,
    0x48,0x83,0xec,0x28,0x48,0x89,0xfb,0x41,0xb6,0x7f};
static const unsigned char iss_pad_read_site_bytes[]={0x48,0x8d,0xbb,0x20,0x04,0x00,0x00,0x48,0x8d,0x4b,0x14,
    0x4c,0x8d,0x43,0x18};

static inline int iss_native_keys_signature(const unsigned char *image, uint64_t size) {
    return size>ISS_PAD_READ_SITE+ISS_PAD_READ_STOLEN &&
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

/* The kinds the held actions press (bit = kind). */
static inline uint64_t iss_native_kinds(uint32_t actions) {
    uint64_t kinds=0;
    for (int a=0;a<ISS_NA_COUNT;++a)
        if (actions>>a & 1)
            for (int i=0;i<iss_native_actions[a].count;++i) kinds|=1ull<<iss_native_actions[a].kinds[i];
    return kinds;
}
#endif
