/* SPDX-License-Identifier: GPL-2.0-or-later */
#undef NDEBUG
#include <assert.h>
#include "iss_camera_aspect.h"

int main(void) {
    const float a169 = 16.0f / 9.0f, tan_y = 0.5773503f;
    float aspect = 0, tan_x = 0;
    /* 32:9: a 16:9 camera becomes 32:9 and its horizontal tangent follows. */
    assert(iss_camera_aspect_fix(a169, a169, tan_y * a169, tan_y, 7680, 2160, &aspect, &tan_x));
    assert(fabsf(aspect - 32.0f / 9.0f) < 1e-5f && fabsf(tan_x - tan_y * 32.0f / 9.0f) < 1e-5f);
    /* 21:9 (3440x1440) and 16:10 (2560x1600): the real ratio, never a fixed one. */
    assert(iss_camera_aspect_fix(a169, a169, tan_y * a169, tan_y, 3440, 1440, &aspect, &tan_x));
    assert(fabsf(aspect - 3440.0f / 1440.0f) < 1e-5f);
    assert(iss_camera_aspect_fix(a169, a169, tan_y * a169, tan_y, 2560, 1600, &aspect, &tan_x));
    assert(fabsf(aspect - 1.6f) < 1e-5f && fabsf(tan_x - tan_y * 1.6f) < 1e-5f);
    /* tan_x may still follow the camera's previous aspect. */
    assert(iss_camera_aspect_fix(a169, 1.0f, tan_y, tan_y, 7680, 2160, &aspect, &tan_x));
    /* A 16:9 output is left exactly as the game set it. */
    assert(!iss_camera_aspect_fix(a169, a169, tan_y * a169, tan_y, 1920, 1080, &aspect, &tan_x));
    assert(!iss_camera_aspect_fix(a169, a169, tan_y * a169, tan_y, 3840, 2160, &aspect, &tan_x));
    /* Other aspects (square shadow maps, already-native cameras) are not touched. */
    assert(!iss_camera_aspect_fix(1.0f, 1.0f, tan_y, tan_y, 7680, 2160, &aspect, &tan_x));
    assert(!iss_camera_aspect_fix(32.0f / 9.0f, a169, tan_y * a169, tan_y, 7680, 2160, &aspect, &tan_x));
    /* Unknown layouts are refused: tangent not following the aspect, bad values. */
    assert(!iss_camera_aspect_fix(a169, a169, 0.9f, tan_y, 7680, 2160, &aspect, &tan_x));
    assert(!iss_camera_aspect_fix(a169, a169, tan_y * a169, 0.0f, 7680, 2160, &aspect, &tan_x));
    assert(!iss_camera_aspect_fix(a169, a169, NAN, tan_y, 7680, 2160, &aspect, &tan_x));
    assert(!iss_camera_aspect_fix(a169, a169, tan_y * a169, tan_y, 0, 2160, &aspect, &tan_x));
    assert(iss_camera_aspect_mode(NULL) == ISS_CAMERA_ASPECT_TRACE);
    assert(iss_camera_aspect_mode("fix") == ISS_CAMERA_ASPECT_FIX);
    assert(iss_camera_aspect_mode("off") == ISS_CAMERA_ASPECT_OFF);
    return 0;
}
