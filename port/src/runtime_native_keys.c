/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Native keyboard and mouse actions: the game-side half (iss_action_input.h, iss_native_keys.h).
 * probe.c replaces the game's action input functions with the ones below and bridges the end of
 * the pad read to runtime_native_pad; runtime_pad.c samples the keys. Each answer is the pad's
 * (the game's own controller code, with the kind's plain mask) or the keyboard's (per kind). */
#include "iss_action_input.h"
#include "iss_native_keys.h"
#include "iss_input_trace.h"

uint32_t runtime_pad_native_take(void);

typedef uint32_t (__attribute__((sysv_abi)) *PadPressed)(uintptr_t ctl, uint32_t mask, float window);
typedef uint32_t (__attribute__((sysv_abi)) *PadMask)(uintptr_t ctl, uint32_t mask);
typedef void (__attribute__((sysv_abi)) *PadConsume)(uintptr_t ctl, uint32_t mask);

static uintptr_t native_image;
static uintptr_t keys_ctl;          /* the controller the keyboard plays on (the last one read) */
static IssActionState keys;

void runtime_native_keys_attach(uintptr_t image) { native_image=image; iss_action_reset(&keys); }

static int32_t clock_now(void) { return *(const volatile int32_t*)(native_image+ISS_ACT_CLOCK); }
static uint32_t kind_mask(int32_t kind) {
    if (kind<0 || (uint32_t)kind>=ISS_BTNK_COUNT) return 0;
    return ((const volatile uint32_t*)(native_image+ISS_BTNK_TABLE))[kind];
}
static uintptr_t vt(uintptr_t ctl, unsigned slot) { return (*(const uintptr_t*const*)ctl)[slot]; }
static uint32_t pad_pressed(uintptr_t ctl, uint32_t mask, float window) { return mask ? ((PadPressed)vt(ctl,0))(ctl,mask,window) : 0; }
static uint32_t pad_held(uintptr_t ctl, uint32_t mask) { return mask ? ((PadMask)vt(ctl,1))(ctl,mask) : 0; }
static void pad_consume(uintptr_t ctl, uint32_t mask) { if (mask) ((PadConsume)vt(ctl,2))(ctl,mask); }
static IssActionPause pause_of(uintptr_t ctl) {
    return (IssActionPause){*(const volatile uint8_t*)(ctl+0xb8),*(const volatile int32_t*)(ctl+0xbc),
                            *(const volatile int32_t*)(ctl+0xc0)};
}
static int keyboard(uintptr_t ctl) { return ctl && ctl==keys_ctl; }
static int key_pressed(uintptr_t ctl, int32_t kind, float window) {
    return keyboard(ctl) && kind>=0 && iss_action_pressed(&keys,(unsigned)kind,window,clock_now(),pause_of(ctl));
}
static int key_held(uintptr_t ctl, int32_t kind) { return keyboard(ctl) && kind>=0 && iss_action_held(&keys,(unsigned)kind); }
static int key_down(uintptr_t ctl, int32_t kind) { return keyboard(ctl) && kind>=0 && iss_action_down(&keys,(unsigned)kind); }
static void key_consume(uintptr_t ctl, int32_t kind) { if (keyboard(ctl) && kind>=0) iss_action_consume(&keys,(unsigned)kind); }
static uintptr_t script_ctl(uintptr_t object) {
    const uintptr_t ctl=*(const uintptr_t*)(object+0x28);
    return ctl ? ctl : *(const uintptr_t*)(native_image+ISS_PAD_DEFAULT);
}
static uintptr_t player_ctl(uintptr_t player) { return *(const uintptr_t*)(*(const uintptr_t*)(player+0x38)+0x4b0); }

/* The replacements. */
uint8_t __attribute__((sysv_abi)) runtime_act_pressed(uintptr_t ctl, int32_t kind, float window) {
    const uint32_t hits=pad_pressed(ctl,kind_mask(kind),window);
    if (hits) pad_consume(ctl,hits);
    const int key=key_pressed(ctl,kind,window);
    if (key) key_consume(ctl,kind);
    return hits || key;
}
uint8_t __attribute__((sysv_abi)) runtime_act_held_consume(uintptr_t ctl, int32_t kind) {
    const uint32_t hits=pad_held(ctl,kind_mask(kind));
    if (hits) pad_consume(ctl,hits);
    const int key=key_held(ctl,kind);
    if (key) key_consume(ctl,kind);
    return hits || key;
}
uint8_t __attribute__((sysv_abi)) runtime_act_pressed_keep(uintptr_t ctl, int32_t kind, float window) {
    return pad_pressed(ctl,kind_mask(kind),window) || key_pressed(ctl,kind,window);
}
uint8_t __attribute__((sysv_abi)) runtime_act_held(uintptr_t ctl, int32_t kind) {
    return pad_held(ctl,kind_mask(kind)) || key_held(ctl,kind);
}
uint8_t __attribute__((sysv_abi)) runtime_act_down(uintptr_t ctl, int32_t kind) {
    return (*(const volatile uint32_t*)(ctl+ISS_PAD_STATE) & kind_mask(kind)) || key_down(ctl,kind);
}
void __attribute__((sysv_abi)) runtime_act_consume(uintptr_t ctl, int32_t kind) {
    pad_consume(ctl,kind_mask(kind));
    key_consume(ctl,kind);
}
uint8_t __attribute__((sysv_abi)) runtime_act_script_pressed(uintptr_t object, int32_t kind, float window) {
    return runtime_act_pressed(script_ctl(object),kind,window);
}
uint8_t __attribute__((sysv_abi)) runtime_act_script_pressed_keep(uintptr_t object, int32_t kind, float window) {
    return runtime_act_pressed_keep(script_ctl(object),kind,window);
}
uint8_t __attribute__((sysv_abi)) runtime_act_script_down(uintptr_t object, int32_t kind) {
    return runtime_act_down(script_ctl(object),kind);
}

/* The set functions: kinds by index (ISS_ACT_SET_KINDS); the hits are consumed by kind. */
static int32_t set_kind(unsigned index) {
    return index<ISS_ACT_SET_COUNT ? ((const int32_t*)(native_image+ISS_ACT_SET_KINDS))[index] : -1;
}
static void consume_set(uintptr_t ctl, uint32_t set) {
    uint32_t mask=0;
    for (unsigned i=0;i<ISS_ACT_SET_COUNT;++i) if (set>>i & 1) { mask|=kind_mask(set_kind(i)); key_consume(ctl,set_kind(i)); }
    pad_consume(ctl,mask);
}
static uint32_t any_set(uintptr_t player, uint32_t set, float window, int held) {
    set&=*(const volatile uint32_t*)(native_image+ISS_ACT_ENABLED);
    const uintptr_t ctl=player_ctl(player);
    uint32_t hits=0;
    for (unsigned i=0;i<ISS_ACT_SET_COUNT;++i) {
        if (!(set>>i & 1)) continue;
        const int32_t kind=set_kind(i);
        if (held ? runtime_act_held(ctl,kind) : runtime_act_pressed_keep(ctl,kind,window)) hits|=1u<<i;
    }
    if (hits) consume_set(player_ctl(player),hits);
    return hits;
}
uint32_t __attribute__((sysv_abi)) runtime_act_any_pressed(uintptr_t player, uint32_t set, float window) {
    return any_set(player,set,window,0);
}
uint32_t __attribute__((sysv_abi)) runtime_act_any_held(uintptr_t player, uint32_t set) {
    return any_set(player,set,0.0f,1);
}
void __attribute__((sysv_abi)) runtime_act_consume_set(uintptr_t player, uint32_t set) {
    consume_set(player_ctl(player),set);
}

/* Bridged at the end of the pad read (saved RBX: the controller): the held actions' kinds go
 * down on the keyboard side. A controller with no pad open is left alone. */
void __attribute__((ms_abi)) runtime_native_pad(uintptr_t ctl, uintptr_t unused) {
    (void)unused;
    if (!native_image || *(const volatile int32_t*)(ctl+ISS_PAD_HANDLE)<0) return;
    keys_ctl=ctl;
    iss_action_update(&keys,iss_native_kinds(runtime_pad_native_take()),clock_now());
}
