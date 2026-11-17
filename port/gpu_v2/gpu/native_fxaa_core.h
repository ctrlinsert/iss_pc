// SPDX-License-Identifier: GPL-2.0-or-later
// Native FXAA: the push constants of iss_native_fxaa.frag, from the game's tuning values and the
// real target size. Header only, so port/tests checks it without Vulkan.
//
// The game's FXAA constants are 8 dwords: 0/1 the reciprocal source size, 4 sub-pixel amount,
// 5 edge threshold, 6 minimum edge threshold (the stock FXAA quality tuning, 0.75 / 0.166 /
// 0.0833). The tuning comes from the game; the texel step comes from the real target, never from
// its 0/1.
#pragma once

#include <array>
#include <cstdint>

#include "native_render_core.h"

namespace iss::native_render {

inline constexpr u32 kFxaaShaderHash = 0x9c75f6a3u; ///< low 32 bits of the engine program hash
inline constexpr u32 kFxaaConstants = 8;            ///< game values read (dwords 0..7)

struct FxaaPush {
    std::array<float, 8> f{}; ///< 1/w, 1/h, sub-pixel, edge threshold, edge threshold min, 0, 0, 0
    std::array<std::int32_t, 4> swizzle{0, 1, 2, 3};
};
static_assert(sizeof(FxaaPush) == 48, "the shader's push constant block");

/// `cb`: the first kFxaaConstants dwords of the game's constants, as floats.
inline FxaaPush MakeFxaaPush(const float* cb, Extent target,
                             const std::array<std::int32_t, 4>& swizzle) {
    FxaaPush p;
    const TexelSize inv = InverseSize(target);
    p.f[0] = inv.x;
    p.f[1] = inv.y;
    p.f[2] = cb[4];
    p.f[3] = cb[5];
    p.f[4] = cb[6];
    p.swizzle = swizzle;
    return p;
}

} // namespace iss::native_render
