/* SPDX-License-Identifier: GPL-2.0-or-later
 * Camera aspect setter observer (CUSA00223/CUSA00004 01.07, RVA 0x581b70).
 * The native patch gives the main camera the output aspect, but other cameras can still be
 * handed the 16:9 value. Shadow and light culling cameras are suspected: on a 32:9 output
 * shadows and lights end at the centre 16:9. This observer logs every aspect the setter
 * receives and, when asked, replaces an exact 16:9 value with the output aspect.
 * Kept free of the runtime so tests/test_camera_aspect.c checks the decision alone. */
#ifndef ISS_CAMERA_ASPECT_H
#define ISS_CAMERA_ASPECT_H
#include <math.h>
#include <stdint.h>
#include <string.h>

#define ISS_CAMERA_ASPECT_SITE 0x581b70u
#define ISS_CAMERA_ASPECT_STOLEN 17u   /* push rbp; mov rbp,rsp; push rbx; push rax; mov rbx,rdi; vmovss [rbx+0x59c],xmm0 */
#define ISS_CAMERA_ASPECT_FIELD 0x59cu
#define ISS_CAMERA_TAN_X 0x5a0u
#define ISS_CAMERA_TAN_Y 0x5a4u

enum { ISS_CAMERA_ASPECT_OFF = 0, ISS_CAMERA_ASPECT_TRACE = 1, ISS_CAMERA_ASPECT_FIX = 2 };

/* ISS_CAMERA_ASPECT: off, trace (default) or fix. */
static inline int iss_camera_aspect_mode(const char *value) {
    if (!value || !*value || !strcmp(value, "trace")) return ISS_CAMERA_ASPECT_TRACE;
    if (!strcmp(value, "fix")) return ISS_CAMERA_ASPECT_FIX;
    if (!strcmp(value, "off") || !strcmp(value, "0")) return ISS_CAMERA_ASPECT_OFF;
    return ISS_CAMERA_ASPECT_TRACE;
}

static inline int iss_camera_aspect_near(float a, float b, float tolerance) {
    return fabsf(a - b) <= tolerance * fmaxf(1.0f, fabsf(b));
}

/* Decides the replacement for one setter call. incoming: the aspect passed in; old_aspect,
 * tan_x, tan_y: the camera's fields before the call. Returns 1 with *aspect and *next_tan_x
 * when incoming is exactly 16:9, the output is not 16:9 and the camera's horizontal tangent
 * follows its vertical one (tan_x = tan_y * old or incoming aspect). Works for any output
 * aspect, wider or narrower than 16:9; a 16:9 output is never changed. */
static inline int iss_camera_aspect_fix(float incoming, float old_aspect, float tan_x, float tan_y,
                                        uint32_t width, uint32_t height, float *aspect, float *next_tan_x) {
    const float sixteen_nine = 16.0f / 9.0f;
    if (!aspect || !next_tan_x || !width || !height) return 0;
    if (!isfinite(incoming) || !isfinite(tan_x) || !isfinite(tan_y)) return 0;
    if (!iss_camera_aspect_near(incoming, sixteen_nine, 0.001f)) return 0;
    const float output = (float)width / (float)height;
    if (!isfinite(output) || output < 0.5f || output > 4.0f) return 0;
    if (iss_camera_aspect_near(output, sixteen_nine, 0.002f)) return 0;
    if (tan_y < 0.01f || tan_y > 10.0f) return 0;
    const int follows_incoming = iss_camera_aspect_near(tan_x, tan_y * incoming, 0.002f);
    const int follows_old = isfinite(old_aspect) && iss_camera_aspect_near(tan_x, tan_y * old_aspect, 0.002f);
    if (!follows_incoming && !follows_old) return 0;
    *aspect = output;
    *next_tan_x = tan_y * output;
    return 1;
}
#ifdef __cplusplus
extern "C" {
#endif
void runtime_camera_aspect_attach(uintptr_t base, int mode);
void __attribute__((ms_abi)) runtime_camera_aspect_setter(void *camera, float *saved_xmm0, uint64_t return_pc);
#ifdef __cplusplus
}
#endif
#endif
