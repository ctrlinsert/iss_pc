// SPDX-License-Identifier: GPL-2.0-or-later
// Native PostBlend (native_postblend.h): the engine's composite draw replaced by our own shader.
//
// At the composite's draw the registers hold everything the engine's shader would read: its
// program (whose recompiled resource list says where each descriptor sits in the user data), the
// six images (distortion, scene, bloom, two grading tables, dither noise), the constant buffer
// with the frame's values, and colour target 0. The pass takes the images and the frame's values
// from there, records one full-screen triangle with iss_native_postblend.frag into the target,
// and computes every size from the real images (native_postblend_core.h). Anything it does not
// recognise (another shader, a different resource list, blending, an sRGB or float target) is
// declined, and the engine's draw runs as before.
#include "native_postblend.h"

#include <array>
#include <cstdio>
#include <vector>

#include "native_fullscreen.h"
#include "native_postblend_core.h"
#include "video_core/host_shaders/iss_native_postblend_frag.h"
#include "video_core/texture_cache/texture_cache.h"

namespace iss::native_render {
namespace {

constexpr u32 kImages = 6;
enum Slot : u32 { kDistortion, kScene, kBloom, kGradeA, kGradeB, kNoise };

FullScreenPass& Program() {
    using F = FullScreenPass::Filter;
    static FullScreenPass pass("postblend", ISS_NATIVE_POSTBLEND_FRAG, sizeof(PostBlendPush),
                               {F::Linear, F::Linear, F::Linear, F::Linear, F::Linear,
                                F::NearestRepeat});
    return pass;
}

} // namespace

bool PostBlendDraw(const Services& s, const DrawRequest& request, const char*& why) {
    if (request.dispatch || request.index != 0) {
        why = "the composite has one draw; this is another";
        return false;
    }
    DrawInputs inputs;
    if (!inputs.Read(s, kPostBlendShaderHash, why)) return false;
    if (inputs.images.size() != kImages) {
        why = "the composite shader's resource list is not the expected six images";
        return false;
    }
    for (u32 i = 0; i < kImages; ++i) {
        const bool table = inputs.images[i].GetType() == AmdGpu::ImageType::Color3D;
        if (table != (i == kGradeA || i == kGradeB)) {
            why = "the composite's images are not in the expected order (two 3D tables at 3 and 4)";
            return false;
        }
    }
    // The hook's first slot is the scene: the descriptor list must agree.
    const Marker& begin = request.begin;
    if (begin.payload_words >= 9 && begin.payload[0] &&
        DecodeTexture(begin.payload + 1).address != inputs.images[kScene].Address()) {
        why = "the composite's second image is not the scene the hook reported";
        return false;
    }
    // The frame's values: the composite's constant buffer (the first guest buffer it reads).
    std::array<float, kPostBlendConstants> constants{};
    if (!inputs.Constants(constants.data(), sizeof(constants))) {
        why = "the composite's constant buffer was not found";
        return false;
    }
    Target target;
    if (!target.Find(s, why)) return false;
    auto& pass = Program();
    if (!pass.Ready(s)) {
        why = "the native composite's Vulkan objects could not be made";
        return false;
    }
    std::vector<VideoCore::ImageId> ids;
    std::vector<vk::ImageView> views;
    if (!pass.FindImages(s, inputs, ids, views, why)) return false;
    auto& cache = s.texture_cache;
    const auto& scene = cache.GetImage(ids[kScene]);
    if (scene.info.size.width != target.size.width || scene.info.size.height != target.size.height) {
        why = "the scene is not the target's size";
        return false;
    }
    const auto& noise = cache.GetImage(ids[kNoise]);
    const PostBlendPush push = MakePostBlendPush(
        constants.data(), target.size, Extent{noise.info.size.width, noise.info.size.height},
        AmdGpu::NumComponents(inputs.images[kNoise].GetDataFmt()) >= 3, target.swizzle);

    static bool logged = false;
    if (LogEnabled() && !logged) {
        logged = true;
        std::fprintf(stderr, "NATIVE_RENDER postblend native: target %ux%u fmt=%d; game values:",
                     target.size.width, target.size.height, int(target.format));
        for (u32 i = 0; i < kPostBlendConstants; ++i) std::fprintf(stderr, " %g", constants[i]);
        std::fprintf(stderr, "; vignette %g,%g noise tiling %g,%g swizzle %d%d%d%d\n", push.f[16],
                     push.f[17], push.f[20], push.f[21], target.swizzle[0], target.swizzle[1],
                     target.swizzle[2], target.swizzle[3]);
    }
    return pass.Record(s, ids, views, target, &push, why);
}

} // namespace iss::native_render
