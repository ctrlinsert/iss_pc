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

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_map>
#include <vector>

#include "native_postblend_core.h"
#include "video_core/amdgpu/regs.h"
#include "video_core/amdgpu/regs_shader.h"
#include "video_core/host_shaders/fs_tri_vert.h"
#include "video_core/host_shaders/iss_native_postblend_frag.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_pipeline_cache.h"
#include "video_core/renderer_vulkan/vk_runtime.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_shader_util.h"
#include "video_core/texture_cache/texture_cache.h"

namespace iss::native_render {
namespace {

constexpr u32 kImages = 6;
enum Slot : u32 { kDistortion, kScene, kBloom, kGradeA, kGradeB, kNoise };

/// The flattened user data of one draw, in the shape GetSharp reads (ResourceFlat, ResourceBase).
struct FlatView {
    std::vector<u32> flattened_ud_buf;
    VAddr pgm_base{};
};

/// Vulkan objects, made once on first use and kept for the process (the device outlives every
/// draw; nothing is destroyed at exit, when the device may already be gone).
struct Objects {
    bool tried{}, ok{};
    vk::Device device{};
    vk::DescriptorSetLayout set_layout{};
    vk::PipelineLayout layout{};
    vk::Sampler clamp{}, noise{};
    vk::ShaderModule vert{}, frag{};
    std::unordered_map<VkFormat, vk::Pipeline> pipelines;
};

Objects& GetObjects() {
    static Objects objects;
    return objects;
}

bool Log() {
    static const bool log = [] {
        const char* v = std::getenv("ISS_NATIVE_RENDER_LOG");
        return v && v[0] == '1';
    }();
    return log;
}

bool Create(Objects& o, const Vulkan::Instance& instance) {
    o.tried = true;
    o.device = instance.GetDevice();
    std::array<vk::DescriptorSetLayoutBinding, kImages> bindings{};
    for (u32 i = 0; i < kImages; ++i) {
        bindings[i] = {.binding = i,
                       .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                       .descriptorCount = 1,
                       .stageFlags = vk::ShaderStageFlagBits::eFragment};
    }
    auto set_layout = o.device.createDescriptorSetLayout({
        .flags = vk::DescriptorSetLayoutCreateFlagBits::ePushDescriptor,
        .bindingCount = kImages,
        .pBindings = bindings.data(),
    });
    if (set_layout.result != vk::Result::eSuccess) return false;
    o.set_layout = set_layout.value;
    const vk::PushConstantRange push{.stageFlags = vk::ShaderStageFlagBits::eFragment,
                                     .offset = 0,
                                     .size = sizeof(PostBlendPush)};
    auto layout = o.device.createPipelineLayout({
        .setLayoutCount = 1,
        .pSetLayouts = &o.set_layout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &push,
    });
    if (layout.result != vk::Result::eSuccess) return false;
    o.layout = layout.value;
    auto clamp = o.device.createSampler({
        .magFilter = vk::Filter::eLinear,
        .minFilter = vk::Filter::eLinear,
        .mipmapMode = vk::SamplerMipmapMode::eNearest,
        .addressModeU = vk::SamplerAddressMode::eClampToEdge,
        .addressModeV = vk::SamplerAddressMode::eClampToEdge,
        .addressModeW = vk::SamplerAddressMode::eClampToEdge,
        .maxLod = 0.0f,
    });
    auto noise = o.device.createSampler({
        .magFilter = vk::Filter::eNearest,
        .minFilter = vk::Filter::eNearest,
        .mipmapMode = vk::SamplerMipmapMode::eNearest,
        .addressModeU = vk::SamplerAddressMode::eRepeat,
        .addressModeV = vk::SamplerAddressMode::eRepeat,
        .addressModeW = vk::SamplerAddressMode::eRepeat,
        .maxLod = 0.0f,
    });
    if (clamp.result != vk::Result::eSuccess || noise.result != vk::Result::eSuccess) return false;
    o.clamp = clamp.value;
    o.noise = noise.value;
    o.vert = Vulkan::CompileSPV(FS_TRI_VERT, o.device);
    o.frag = Vulkan::CompileSPV(ISS_NATIVE_POSTBLEND_FRAG, o.device);
    if (!o.vert || !o.frag) return false;
    o.ok = true;
    return true;
}

vk::Pipeline PipelineFor(Objects& o, vk::Format format) {
    const auto it = o.pipelines.find(static_cast<VkFormat>(format));
    if (it != o.pipelines.end()) return it->second;
    const std::array<vk::PipelineShaderStageCreateInfo, 2> stages{{
        {.stage = vk::ShaderStageFlagBits::eVertex, .module = o.vert, .pName = "main"},
        {.stage = vk::ShaderStageFlagBits::eFragment, .module = o.frag, .pName = "main"},
    }};
    const vk::PipelineVertexInputStateCreateInfo vertex_input{};
    const vk::PipelineInputAssemblyStateCreateInfo assembly{
        .topology = vk::PrimitiveTopology::eTriangleList};
    const vk::PipelineViewportStateCreateInfo viewport{.viewportCount = 1, .scissorCount = 1};
    const vk::PipelineRasterizationStateCreateInfo raster{
        .polygonMode = vk::PolygonMode::eFill,
        .cullMode = vk::CullModeFlagBits::eNone,
        .lineWidth = 1.0f,
    };
    const vk::PipelineMultisampleStateCreateInfo multisample{
        .rasterizationSamples = vk::SampleCountFlagBits::e1};
    const vk::PipelineColorBlendAttachmentState attachment{
        .blendEnable = false,
        .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                          vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
    };
    const vk::PipelineColorBlendStateCreateInfo blend{.attachmentCount = 1,
                                                      .pAttachments = &attachment};
    const std::array<vk::DynamicState, 2> dynamic_states{vk::DynamicState::eViewport,
                                                         vk::DynamicState::eScissor};
    const vk::PipelineDynamicStateCreateInfo dynamic{
        .dynamicStateCount = static_cast<u32>(dynamic_states.size()),
        .pDynamicStates = dynamic_states.data()};
    const vk::PipelineRenderingCreateInfo rendering{.colorAttachmentCount = 1,
                                                    .pColorAttachmentFormats = &format};
    const vk::GraphicsPipelineCreateInfo info{
        .pNext = &rendering,
        .stageCount = static_cast<u32>(stages.size()),
        .pStages = stages.data(),
        .pVertexInputState = &vertex_input,
        .pInputAssemblyState = &assembly,
        .pViewportState = &viewport,
        .pRasterizationState = &raster,
        .pMultisampleState = &multisample,
        .pColorBlendState = &blend,
        .pDynamicState = &dynamic,
        .layout = o.layout,
    };
    auto result = o.device.createGraphicsPipeline({}, info);
    const vk::Pipeline pipeline = result.result == vk::Result::eSuccess ? result.value : vk::Pipeline{};
    o.pipelines.emplace(static_cast<VkFormat>(format), pipeline);
    return pipeline;
}

bool IsUnorm8(vk::Format f) {
    return f == vk::Format::eR8G8B8A8Unorm || f == vk::Format::eB8G8R8A8Unorm ||
           f == vk::Format::eA2B10G10R10UnormPack32 || f == vk::Format::eA2R10G10B10UnormPack32;
}

} // namespace

bool PostBlendDraw(const Services& s, const DrawRequest& request, const char*& why) {
    if (request.dispatch || request.index != 0) {
        why = "the composite has one draw; this is another";
        return false;
    }
    const auto& regs = s.regs;
    const auto* ps = regs.ProgramForStage(static_cast<u32>(Shader::HwStage::Fragment));
    if (!ps || !ps->Address<u32*>()) {
        why = "no pixel shader at the draw";
        return false;
    }
    const auto params = AmdGpu::GetParams(*ps);
    if (static_cast<u32>(params.hash) != kPostBlendShaderHash) {
        why = "the draw's pixel shader is not the composite's";
        return false;
    }
    Vulkan::Program* program = s.pipeline_cache.FindCompiledProgram(params.hash);
    if (!program) {
        why = "the engine's composite shader is not compiled yet (the engine draws this frame)";
        return false;
    }
    const auto& info = program->info;
    if (info.images.size() != kImages) {
        why = "the composite shader's resource list is not the expected six images";
        return false;
    }
    const auto& cb0 = regs.color_buffers[0];
    if (!cb0) {
        why = "no colour target";
        return false;
    }
    if (regs.color_target_mask.GetMask(0) != 0xF || regs.blend_control[0].enable) {
        why = "the target is written with a mask or blending";
        return false;
    }
    if (cb0.GetNumberFmt() != AmdGpu::NumberFormat::Unorm) {
        why = "the target is not UNORM";
        return false;
    }

    // The descriptors, where the engine's shader would read them.
    FlatView view;
    view.pgm_base = params.Base();
    view.flattened_ud_buf.assign(std::max<u32>(info.srt_info.flattened_bufsize_dw,
                                               Shader::NUM_USER_DATA_REGS),
                                 0);
    std::memcpy(view.flattened_ud_buf.data(), params.user_data.data(),
                std::min<size_t>(params.user_data.size_bytes(), Shader::NUM_USER_DATA_REGS * 4));
    if (info.srt_info.walker_func) {
        info.srt_info.walker_func(params.user_data.data(), view.flattened_ud_buf.data());
    }
    std::array<AmdGpu::Image, kImages> sharps{};
    for (u32 i = 0; i < kImages; ++i) {
        sharps[i] = info.images[i].GetSharp(view);
        if (!sharps[i].Valid()) {
            why = "an image descriptor of the composite is not valid";
            return false;
        }
        const bool table = sharps[i].GetType() == AmdGpu::ImageType::Color3D;
        if (table != (i == kGradeA || i == kGradeB)) {
            why = "the composite's images are not in the expected order (two 3D tables at 3 and 4)";
            return false;
        }
    }
    // The hook's first slot is the scene: the descriptor list must agree.
    const Marker& begin = request.begin;
    if (begin.payload_words >= 9 && begin.payload[0] &&
        DecodeTexture(begin.payload + 1).address != sharps[kScene].Address()) {
        why = "the composite's second image is not the scene the hook reported";
        return false;
    }

    // The frame's values: the composite's constant buffer (the first guest buffer it reads).
    std::array<float, kPostBlendConstants> constants{};
    bool have_constants = false;
    for (const auto& buffer : info.buffers) {
        if (buffer.buffer_type != Shader::BufferType::Guest) continue;
        const AmdGpu::Buffer vsharp = buffer.GetSharp(view);
        if (!vsharp.base_address || vsharp.GetSize() < kPostBlendConstants * 4) continue;
        std::memcpy(constants.data(), reinterpret_cast<const void*>(vsharp.base_address),
                    sizeof(constants));
        have_constants = true;
        break;
    }
    if (!have_constants) {
        why = "the composite's constant buffer was not found";
        return false;
    }

    auto& o = GetObjects();
    if (!o.tried) Create(o, s.instance);
    if (!o.ok) {
        why = "the native composite's Vulkan objects could not be made";
        return false;
    }

    // The images, as the rasterizer would find them for this draw.
    auto& cache = s.texture_cache;
    std::array<VideoCore::ImageId, kImages> ids{};
    std::array<vk::ImageView, kImages> views{};
    for (u32 i = 0; i < kImages; ++i) {
        VideoCore::TextureCache::ImageDesc desc(sharps[i], info.images[i]);
        if (desc.type != VideoCore::TextureCache::BindingType::Texture) {
            why = "a composite image is bound for writing";
            return false;
        }
        ids[i] = cache.FindImage(desc);
        if (!ids[i]) {
            why = "a composite image is not in the texture cache";
            return false;
        }
        views[i] = *cache.FindTexture(ids[i], desc).image_view;
    }
    const AmdGpu::CbDbExtent hint = s.cb0_extent ? *s.cb0_extent : AmdGpu::CbDbExtent{};
    VideoCore::TextureCache::ImageDesc target_desc(cb0, hint);
    const VideoCore::ImageId target_id = cache.FindImage(target_desc);
    if (!target_id) {
        why = "the colour target is not in the texture cache";
        return false;
    }
    for (u32 i = 0; i < kImages; ++i) {
        if (ids[i] == target_id) {
            why = "the composite reads its own target";
            return false;
        }
    }
    auto& target_view = cache.FindRenderTarget(target_id, target_desc);
    auto& target = cache.GetImage(target_id);
    const vk::Format format = target_view.info.format;
    if (!IsUnorm8(format) || target.info.num_samples != 1) {
        why = "the target's format is not an 8- or 10-bit UNORM one";
        return false;
    }
    const Extent size{target.info.size.width, target.info.size.height};
    const auto& scene = cache.GetImage(ids[kScene]);
    if (scene.info.size.width != size.width || scene.info.size.height != size.height) {
        why = "the scene is not the target's size";
        return false;
    }
    const vk::Pipeline pipeline = PipelineFor(o, format);
    if (!pipeline) {
        why = "the native composite pipeline could not be made";
        return false;
    }

    const auto& noise = cache.GetImage(ids[kNoise]);
    const auto swizzle = cb0.Swizzle();
    std::array<std::int32_t, 4> order{};
    for (u32 i = 0; i < 4; ++i) order[i] = static_cast<std::int32_t>(swizzle.Map(i));
    const PostBlendPush push = MakePostBlendPush(
        constants.data(), size, Extent{noise.info.size.width, noise.info.size.height},
        AmdGpu::NumComponents(sharps[kNoise].GetDataFmt()) >= 3, order);

    static bool logged = false;
    if (Log() && !logged) {
        logged = true;
        std::fprintf(stderr,
                     "NATIVE_RENDER postblend native: target %ux%u fmt=%d; game values:", size.width,
                     size.height, int(format));
        for (u32 i = 0; i < kPostBlendConstants; ++i) std::fprintf(stderr, " %g", constants[i]);
        std::fprintf(stderr, "; vignette %g,%g noise tiling %g,%g swizzle %d%d%d%d\n", push.f[16],
                     push.f[17], push.f[20], push.f[21], order[0], order[1], order[2], order[3]);
    }

    // Outside the rasterizer's render pass: the inputs readable, the target writable.
    s.scheduler.EndRendering();
    for (u32 i = 0; i < kImages; ++i) {
        s.runtime.Transit(&cache.GetImage(ids[i]), vk::ImageLayout::eShaderReadOnlyOptimal,
                          vk::PipelineStageFlagBits2::eFragmentShader,
                          vk::AccessFlagBits2::eShaderSampledRead);
    }
    s.runtime.Transit(&target, vk::ImageLayout::eColorAttachmentOptimal,
                      vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                      vk::AccessFlagBits2::eColorAttachmentWrite);
    s.runtime.FlushBarriers();

    std::array<vk::DescriptorImageInfo, kImages> image_infos{};
    std::array<vk::WriteDescriptorSet, kImages> writes{};
    for (u32 i = 0; i < kImages; ++i) {
        image_infos[i] = {.sampler = i == kNoise ? o.noise : o.clamp,
                          .imageView = views[i],
                          .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal};
        writes[i] = {.dstBinding = i,
                     .descriptorCount = 1,
                     .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                     .pImageInfo = &image_infos[i]};
    }
    const vk::RenderingAttachmentInfo color{
        .imageView = *target_view.image_view,
        .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eDontCare,
        .storeOp = vk::AttachmentStoreOp::eStore,
    };
    const vk::Rect2D area{{0, 0}, {size.width, size.height}};
    const auto cmdbuf = s.scheduler.CommandBuffer();
    cmdbuf.beginRendering(vk::RenderingInfo{
        .renderArea = area, .layerCount = 1, .colorAttachmentCount = 1, .pColorAttachments = &color});
    cmdbuf.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);
    cmdbuf.pushDescriptorSetKHR(vk::PipelineBindPoint::eGraphics, o.layout, 0, writes);
    cmdbuf.pushConstants(o.layout, vk::ShaderStageFlagBits::eFragment, 0, sizeof(push), &push);
    cmdbuf.setViewport(0, vk::Viewport{0.0f, 0.0f, float(size.width), float(size.height), 0.0f, 1.0f});
    cmdbuf.setScissor(0, area);
    cmdbuf.draw(3, 1, 0, 0);
    cmdbuf.endRendering();
    return true;
}

} // namespace iss::native_render
