// SPDX-License-Identifier: GPL-2.0-or-later
// Native renderer: what the full-screen post passes (PostBlend, FXAA, ...) share.
//
// At the engine's draw the registers hold everything its shader would read. DrawInputs finds the
// draw's program, its image descriptors and its first constant buffer from there (the recompiled
// program's resource list says where each one sits in the user data). FullScreenPass holds the
// Vulkan objects of one of our own fragment shaders (push descriptors for its images, push
// constants for its values, a pipeline per target format) and records one full-screen triangle
// into colour target 0.
#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

#include "native_render.h"
#include "video_core/amdgpu/resource.h"
#include "video_core/renderer_vulkan/vk_common.h"
#include "video_core/texture_cache/types.h"

namespace Shader {
struct Info;
}

namespace iss::native_render {

bool LogEnabled();

/// The engine's draw, as its pixel shader sees it.
struct DrawInputs {
    u64 hash{};                       ///< the program hash (low 32 bits name the shader)
    const Shader::Info* info{};       ///< the recompiled program's resource list
    std::vector<u32> flat;            ///< the flattened user data (ResourceFlat)
    VAddr pgm_base{};
    std::vector<AmdGpu::Image> images; ///< the image descriptors, in the program's order

    /// The draw's pixel shader must have `hash` (low 32 bits) and be compiled already (the first
    /// frame the engine draws it compiles it).
    bool Read(const Services& s, u32 hash, const char*& why);
    /// The first `bytes` of the first guest constant buffer at least that large.
    bool Constants(void* out, u32 bytes) const;
};

/// The draw's colour target 0, as the rasterizer would bind it.
struct Target {
    VideoCore::ImageId id{};
    vk::ImageView view{};
    vk::Format format{};
    Extent size{};
    std::array<std::int32_t, 4> swizzle{0, 1, 2, 3}; ///< which (r,g,b,a) output channel i takes

    /// Plain writes only (mask 0xF, no blending) to a single-sample UNORM 8/10-bit target.
    bool Find(const Services& s, const char*& why);
};

/// One of our fragment shaders drawn as a full-screen triangle.
class FullScreenPass {
public:
    enum class Filter : u8 { Linear, NearestRepeat };

    FullScreenPass(const char* name, std::span<const u32> spirv, u32 push_bytes,
                   std::vector<Filter> samplers);

    /// Makes the Vulkan objects on first use; false when they cannot be made.
    bool Ready(const Services& s);
    /// The images (texture cache ids, sampled in binding order) read from `inputs`.
    bool FindImages(const Services& s, const DrawInputs& inputs,
                    std::vector<VideoCore::ImageId>& ids, std::vector<vk::ImageView>& views,
                    const char*& why) const;
    /// Records the draw: inputs to shader-read, the target to colour-attachment, one triangle.
    bool Record(const Services& s, std::span<const VideoCore::ImageId> ids,
                std::span<const vk::ImageView> views, const Target& target, const void* push,
                const char*& why);

private:
    vk::Pipeline PipelineFor(vk::Format format);

    const char* name;
    std::span<const u32> spirv;
    u32 push_bytes;
    std::vector<Filter> filters;
    bool tried{}, ok{};
    vk::Device device{};
    vk::DescriptorSetLayout set_layout{};
    vk::PipelineLayout layout{};
    vk::Sampler linear{}, nearest{};
    vk::ShaderModule vert{}, frag{};
    std::unordered_map<VkFormat, vk::Pipeline> pipelines;
};

} // namespace iss::native_render
