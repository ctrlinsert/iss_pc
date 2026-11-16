// SPDX-License-Identifier: GPL-2.0-or-later
// Native FXAA (native_fxaa.h): the engine's FXAA draw replaced by our own shader.
//
// The engine's FXAA reads one image, the composite's output with luma in alpha, and a small
// constant buffer with its tuning. The pass takes both from the draw (native_fullscreen.h) and
// draws iss_native_fxaa.frag into the same target, with the texel step from the target's real
// size (native_fxaa_core.h). Anything else (a second input image, blending, another format) is
// declined and the engine's draw runs.
#include "native_fxaa.h"

#include <array>
#include <cstdio>
#include <vector>

#include "native_fullscreen.h"
#include "native_fxaa_core.h"
#include "video_core/host_shaders/iss_native_fxaa_frag.h"
#include "video_core/texture_cache/texture_cache.h"

namespace iss::native_render {
namespace {

FullScreenPass& Program() {
    static FullScreenPass pass("fxaa", ISS_NATIVE_FXAA_FRAG, sizeof(FxaaPush),
                               {FullScreenPass::Filter::Linear});
    return pass;
}

} // namespace

bool FxaaDraw(const Services& s, const DrawRequest& request, const char*& why) {
    if (request.dispatch || request.index != 0) {
        why = "FXAA has one draw; this is another";
        return false;
    }
    DrawInputs inputs;
    if (!inputs.Read(s, kFxaaShaderHash, why)) return false;
    if (inputs.images.size() != 1) {
        why = "the FXAA shader reads more than one image (the variant with a second input)";
        return false;
    }
    const Marker& begin = request.begin;
    if (begin.payload_words >= 8 && DecodeTexture(begin.payload).address != inputs.images[0].Address()) {
        why = "the FXAA input is not the image the hook reported";
        return false;
    }
    std::array<float, kFxaaConstants> constants{};
    if (!inputs.Constants(constants.data(), sizeof(constants))) {
        why = "the FXAA constant buffer was not found";
        return false;
    }
    Target target;
    if (!target.Find(s, why)) return false;
    auto& pass = Program();
    if (!pass.Ready(s)) {
        why = "the native FXAA Vulkan objects could not be made";
        return false;
    }
    std::vector<VideoCore::ImageId> ids;
    std::vector<vk::ImageView> views;
    if (!pass.FindImages(s, inputs, ids, views, why)) return false;
    const auto& source = s.texture_cache.GetImage(ids[0]);
    if (source.info.size.width != target.size.width ||
        source.info.size.height != target.size.height) {
        why = "the FXAA input is not the target's size";
        return false;
    }
    const FxaaPush push = MakeFxaaPush(constants.data(), target.size, target.swizzle);

    static bool logged = false;
    if (LogEnabled() && !logged) {
        logged = true;
        std::fprintf(stderr,
                     "NATIVE_RENDER fxaa native: target %ux%u fmt=%d; game values: 1/size %g,%g "
                     "subpix %g threshold %g min %g\n",
                     target.size.width, target.size.height, int(target.format), constants[0],
                     constants[1], constants[4], constants[5], constants[6]);
    }
    return pass.Record(s, ids, views, target, &push, why);
}

} // namespace iss::native_render
