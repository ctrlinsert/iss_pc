/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Native keyboard and mouse actions (iss_native_keys.h): the game-side half. probe.c replaces
 * the action kind lookup with runtime_native_lookup and bridges the end of the pad read to
 * runtime_native_pad; runtime_pad.c samples the keys. */
#include "iss_native_keys.h"
#include "iss_input_trace.h"
#include <stdatomic.h>

uint32_t runtime_pad_native_take(void);
int runtime_pad_native_signatures(uint64_t out[ISS_NA_COUNT]);
void runtime_input_trace_note_btnk(int32_t kind, uint32_t mask, uintptr_t ret);
int runtime_input_trace_active(void);

static uintptr_t native_image;
static uint32_t table_seen[ISS_NATIVE_KINDS], action_bit[ISS_NA_COUNT];
static _Atomic uint32_t vbit[ISS_NATIVE_KINDS];
static int split_gen=-1;

void runtime_native_keys_attach(uintptr_t image) { native_image=image; }

/* The split follows the control scheme (the table is refilled when it changes) and the key map. */
static void refresh(const volatile uint32_t *table) {
    uint32_t now[ISS_NATIVE_KINDS];
    for (unsigned k=0;k<ISS_NATIVE_KINDS;++k) now[k]=table[k];
    uint64_t sig[ISS_NA_COUNT];
    const int gen=runtime_pad_native_signatures(sig);
    if (gen==split_gen && !memcmp(now,table_seen,sizeof(now))) return;
    split_gen=gen; memcpy(table_seen,now,sizeof(now));
    uint32_t bits[ISS_NATIVE_KINDS];
    iss_native_split(now,sig,bits,action_bit);
    for (unsigned k=0;k<ISS_NATIVE_KINDS;++k) atomic_store_explicit(&vbit[k],bits[k],memory_order_relaxed);
}

/* Replaces the action kind lookup: the table's mask plus the kind's split bits. */
uint32_t __attribute__((sysv_abi)) runtime_native_lookup(int32_t kind) {
    if (kind==-1) return 0;
    uint32_t mask=((const volatile uint32_t*)(native_image+ISS_BTNK_TABLE))[kind];
    if ((uint32_t)kind<ISS_NATIVE_KINDS) mask|=atomic_load_explicit(&vbit[kind],memory_order_relaxed);
    if (runtime_input_trace_active()) runtime_input_trace_note_btnk(kind,mask,(uintptr_t)__builtin_return_address(0));
    return mask;
}

/* Bridged at the end of the pad read (saved RBX: the controller): adds the held actions. A
 * controller with no pad open, or one the game is not reading, is left alone. */
void __attribute__((ms_abi)) runtime_native_pad(uintptr_t ctl, uintptr_t unused) {
    (void)unused;
    if (!native_image || *(const volatile int32_t*)(ctl+ISS_PAD_HANDLE)<0) return;
    const volatile uint32_t *table=(const volatile uint32_t*)(native_image+ISS_BTNK_TABLE);
    refresh(table);
    const uint32_t actions=runtime_pad_native_take();
    if (!actions) return;
    *(volatile uint32_t*)(ctl+ISS_PAD_STATE)|=iss_native_state(table_seen,action_bit,actions);
}
