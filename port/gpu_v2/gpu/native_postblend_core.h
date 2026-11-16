// SPDX-License-Identifier: GPL-2.0-or-later
// Native PostBlend: the push constants of iss_native_postblend.frag, from the game's per-frame
// values and the real sizes. Header only, so port/tests checks it without Vulkan.
//
// The game's values are the composite's constant buffer as dwords (a value per dword). The ones
// used: 0 exposure, 1 knee, 2/3 low exponent and gain, 4/5 high exponent and gain, 6 grey amount,
// 7 floor, 8 distortion strength, 10 grade blend, 12/13 and 14/15 grade table scale and offset,
// 16/17 vignette scale per axis, 18/19 vignette slope and bias.
//
// Sizes and shapes are not taken from the game's values, which assume its own output: the vignette
// keeps the game's vertical scale and gets its horizontal one from the target's aspect (round in
// pixels at 16:10, 16:9, 21:9, 32:9), and the dither noise tiles 1:1 with the target's pixels.
#pragma once

#include <array>
#include <cstdint>

#include "native_render_core.h"

namespace iss::native_render {

inline constexpr u32 kPostBlendShaderHash = 0x46335d73u; ///< low 32 bits of the engine program hash
inline constexpr u32 kPostBlendConstants = 20;            ///< game values read (dwords 0..19)

struct PostBlendPush {
    std::array<float, 24> f{};
    std::array<std::int32_t, 4> swizzle{0, 1, 2, 3};
};
static_assert(sizeof(PostBlendPush) == 112, "the shader's push constant block");

/// `cb`: the first kPostBlendConstants dwords of the game's constants, as floats. `swizzle[i]`:
/// which of (r, g, b, a) output channel i takes (the target's channel order).
inline PostBlendPush MakePostBlendPush(const float* cb, Extent target, Extent noise, bool noise_rgb,
                                       const std::array<std::int32_t, 4>& swizzle) {
    PostBlendPush p;
    auto& f = p.f;
    // tone_a, tone_b
    f[0] = cb[0];
    f[1] = cb[1];
    f[2] = cb[2];
    f[3] = cb[3];
    f[4] = cb[4];
    f[5] = cb[5];
    f[6] = cb[6];
    f[7] = cb[7];
    // grade, grade_vig
    f[8] = cb[8];
    f[9] = cb[10];
    f[10] = cb[12];
    f[11] = cb[13];
    f[12] = cb[14];
    f[13] = cb[15];
    f[14] = cb[18];
    f[15] = cb[19];
    // shape: the vignette round in pixels at the target's aspect, then 1 / size
    const TexelSize inv = InverseSize(target);
    f[16] = cb[17] * Aspect(target);
    f[17] = cb[17];
    f[18] = inv.x;
    f[19] = inv.y;
    // noise: one noise texel per target pixel
    f[20] = noise.width ? float(target.width) / float(noise.width) : 1.0f;
    f[21] = noise.height ? float(target.height) / float(noise.height) : 1.0f;
    f[22] = noise_rgb ? 1.0f : 0.0f;
    f[23] = 0.0f;
    p.swizzle = swizzle;
    return p;
}

} // namespace iss::native_render
