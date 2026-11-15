/* SPDX-License-Identifier: GPL-2.0-or-later */
/* ISS_INPUT_TRACE=1 (iss_input_trace.h): records how the game asks for input. The hooks only
 * queue lines; runtime_input_trace_flush (the pad sampler, host thread) prints them. */
#include "iss_input_trace.h"
#include "iss_menu_aspect.h"
#include <stdatomic.h>
#include <stdio.h>

int runtime_menu_scene(void);
static uintptr_t trace_image;
static IssTraceSlot seen[ISS_TRACE_SLOTS];
#define TRACE_QUEUE 1024u
static struct { uint64_t key; uint32_t mask; } queue[TRACE_QUEUE];
static atomic_uint queue_head, queue_tail;
static atomic_int trace_scene;

static void record(unsigned kind, uint32_t value, uint32_t mask, uintptr_t ret) {
    const uint32_t caller=ret>=trace_image && ret-trace_image<0xffffffffu ? (uint32_t)(ret-trace_image) : 0xffffffffu;
    const uint64_t key=iss_trace_key(kind,value,caller,(unsigned)atomic_load_explicit(&trace_scene,memory_order_relaxed));
    if (!iss_trace_insert(seen,key)) return;
    const unsigned i=atomic_fetch_add(&queue_head,1);
    if (i-atomic_load(&queue_tail)>=TRACE_QUEUE) return;
    queue[i%TRACE_QUEUE].key=key; queue[i%TRACE_QUEUE].mask=mask;
}
void runtime_input_trace_attach(uintptr_t image) { trace_image=image; }
int runtime_input_trace_active(void) { return trace_image!=0; }

/* Replaces the button kind lookup: the same table read, recorded. With the native action keys
 * on, their lookup (runtime_native_keys.c) replaces it instead and notes each read here. */
void runtime_input_trace_note_btnk(int32_t kind, uint32_t mask, uintptr_t ret) {
    record(ISS_TRACE_BTNK,(uint32_t)kind,mask,ret);
}
uint32_t __attribute__((sysv_abi)) runtime_input_trace_btnk(int32_t kind) {
    if (kind==-1) return 0;
    const uint32_t mask=((const volatile uint32_t*)(trace_image+ISS_BTNK_TABLE))[kind];
    record(ISS_TRACE_BTNK,(uint32_t)kind,mask,(uintptr_t)__builtin_return_address(0));
    return mask;
}
/* Replaces IS_BUTTON_DOWN: the controller's level bits, recorded. */
uint8_t __attribute__((sysv_abi)) runtime_input_trace_down(const void *object, uint32_t mask) {
    uintptr_t ctl=*(const uintptr_t*)((const char*)object+0x28);
    if (!ctl) ctl=*(const uintptr_t*)(trace_image+ISS_PAD_DEFAULT);
    record(ISS_TRACE_DOWN,mask,mask,(uintptr_t)__builtin_return_address(0));
    return (*(const volatile uint32_t*)(ctl+0x20) & mask)!=0;
}
/* Bridge observers at the controller queries' entry: RDI controller, RSI mask, return PC. */
void __attribute__((ms_abi)) runtime_input_trace_pressed(uintptr_t ctl, uintptr_t mask, uintptr_t ret) {
    (void)ctl; record(ISS_TRACE_PRESSED,(uint32_t)mask,(uint32_t)mask,ret);
}
void __attribute__((ms_abi)) runtime_input_trace_held(uintptr_t ctl, uintptr_t mask, uintptr_t ret) {
    (void)ctl; record(ISS_TRACE_HELD,(uint32_t)mask,(uint32_t)mask,ret);
}

void runtime_input_trace_flush(void) {
    if (!trace_image) return;
    atomic_store(&trace_scene,runtime_menu_scene());
    static const char *const kinds[]={"","btnk","pressed","held","down"};
    static const char *const scenes[]={"unsupported","starting","no_hero","gameplay","paused","movie"};
    unsigned tail=atomic_load(&queue_tail);
    const unsigned head=atomic_load(&queue_head);
    if (head-tail>TRACE_QUEUE) tail=head-TRACE_QUEUE;
    for (;tail!=head;++tail) {
        const uint64_t key=queue[tail%TRACE_QUEUE].key;
        const unsigned kind=(unsigned)(key>>60), scene=(unsigned)(key>>56&15);
        const uint32_t value=(uint32_t)(key>>32&0xffffff), caller=(uint32_t)key;
        if (kind==ISS_TRACE_BTNK)
            printf("INPUT_TRACE btnk %u mask=%#x caller=%#x scene=%s\n",value,queue[tail%TRACE_QUEUE].mask,caller,
                scene<6 ? scenes[scene] : "?");
        else
            printf("INPUT_TRACE %s mask=%#x caller=%#x scene=%s\n",kind<5 ? kinds[kind] : "?",queue[tail%TRACE_QUEUE].mask,caller,
                scene<6 ? scenes[scene] : "?");
    }
    atomic_store(&queue_tail,tail);
}
