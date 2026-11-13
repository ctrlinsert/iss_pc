/* SPDX-License-Identifier: GPL-2.0-or-later
 * Camera aspect setter observer: see iss_camera_aspect.h. */
#include "iss_camera_aspect.h"
#include "iss_native_resolution.h"
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>

static uintptr_t image_base;
static atomic_int mode;
static atomic_flag log_lock = ATOMIC_FLAG_INIT;
static atomic_uint calls, fixes;
#define SEEN_MAX 96u
static struct { uint32_t caller, vtable, aspect_milli; } seen[SEEN_MAX];
static unsigned seen_count;

void runtime_camera_aspect_attach(uintptr_t base, int value) {
    image_base = base;
    atomic_store(&mode, value);
}

static uint32_t rva(uintptr_t address) {
    return address >= image_base && address - image_base < 0x10000000u ? (uint32_t)(address - image_base) : 0xffffffffu;
}

/* Logs each distinct (caller, camera type, aspect) once; returns 1 for a new one. */
static int first_sighting(uint32_t caller, uint32_t vtable, float aspect) {
    const uint32_t milli = isfinite(aspect) ? (uint32_t)lroundf(fabsf(aspect) * 1000.0f) : 0xffffffffu;
    while (atomic_flag_test_and_set_explicit(&log_lock, memory_order_acquire)) {}
    int fresh = 0;
    unsigned i = 0;
    for (; i < seen_count; i++)
        if (seen[i].caller == caller && seen[i].vtable == vtable && seen[i].aspect_milli == milli) break;
    if (i == seen_count && seen_count < SEEN_MAX) {
        seen[seen_count].caller = caller; seen[seen_count].vtable = vtable; seen[seen_count].aspect_milli = milli;
        seen_count++; fresh = 1;
    }
    atomic_flag_clear_explicit(&log_lock, memory_order_release);
    return fresh;
}

/* Bridge mode 7 at the setter's entry: RDI camera, saved YMM0 (the incoming aspect), return PC. */
void __attribute__((ms_abi)) runtime_camera_aspect_setter(void *camera, float *saved_xmm0, uint64_t return_pc) {
    const int current = atomic_load(&mode);
    if (current == ISS_CAMERA_ASPECT_OFF || !image_base || !camera || !saved_xmm0) return;
    atomic_fetch_add(&calls, 1);
    unsigned char *cam = camera;
    uintptr_t vtable;
    float incoming = saved_xmm0[0], old_aspect, tan_x, tan_y;
    memcpy(&vtable, cam, sizeof(vtable));
    memcpy(&old_aspect, cam + ISS_CAMERA_ASPECT_FIELD, 4);
    memcpy(&tan_x, cam + ISS_CAMERA_TAN_X, 4);
    memcpy(&tan_y, cam + ISS_CAMERA_TAN_Y, 4);
    const IssNativePlan *plan = runtime_native_plan();
    const uint32_t width = plan ? plan->width : 0, height = plan ? plan->height : 0;
    float aspect = incoming, next_tan_x = tan_x;
    const int fixable = iss_camera_aspect_fix(incoming, old_aspect, tan_x, tan_y, width, height, &aspect, &next_tan_x);
    const char *action = "kept";
    if (fixable && current == ISS_CAMERA_ASPECT_FIX) {
        memcpy(cam + ISS_CAMERA_TAN_X, &next_tan_x, 4);
        saved_xmm0[0] = aspect;   /* the setter's own store writes it to the camera */
        atomic_fetch_add(&fixes, 1);
        action = "fixed";
    } else if (fixable) action = "would-fix";
    const uint32_t caller = rva((uintptr_t)return_pc), type = rva(vtable);
    if (first_sighting(caller, type, incoming))
        fprintf(stderr, "CAMERA_ASPECT cam=%p vt=%#x caller=%#x in=%.4f old=%.4f tan_x=%.4f tan_y=%.4f -> %.4f %s (calls %u, fixes %u)\n",
                camera, type, caller, (double)incoming, (double)old_aspect, (double)tan_x, (double)tan_y,
                (double)aspect, action, atomic_load(&calls), atomic_load(&fixes));
}
