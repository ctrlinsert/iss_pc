// SPDX-License-Identifier: GPL-2.0-or-later
// Native renderer: what the full-screen post passes share (native_fullscreen.h).
#include "native_fullscreen.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

#include "video_core/amdgpu/regs.h"
#include "video_core/amdgpu/regs_shader.h"
#include "video_core/host_shaders/fs_tri_vert.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_pipeline_cache.h"
#include "video_core/renderer_vulkan/vk_runtime.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_shader_util.h"
#include "video_core/texture_cache/texture_cache.h"

namespace iss::native_render {

bool LogEnabled() {
    static const bool log = [] {
        const char* v = std::getenv("ISS_NATIVE_RENDER_LOG");
        return v && v[0] == '1';
    }();
    return log;
}

namespace {

/// The flattened user data of one draw, in the shape GetSharp reads (ResourceFlat, ResourceBase).
struct FlatView {
    const std::vector<u32>& flattened_ud_buf;
    VAddr pgm_base;
};

bool IsUnorm8(vk::Format f) {
    return f == vk::Format::eR8G8B8A8Unorm || f == vk::Format::eB8G8R8A8Unorm ||
           f == vk::Format::eA2B10G10R10UnormPack32 || f == vk::Format::eA2R10G10B10UnormPack32;
}

} // namespace

bool DrawInputs::Read(const Services& s, u32 want, const char*& why) {
    const auto* ps = s.regs.ProgramForStage(static_cast<u32>(Shader::HwStage::Fragment));
    if (!ps || !ps->Address<u32*>()) {
        why = "no pixel shader at the draw";
        return false;
    }
    const auto params = AmdGpu::GetParams(*ps);
    if (static_cast<u32>(params.hash) != want) {
        why = "the draw's pixel shader is not the pass's";
        return false;
    }
    Vulkan::Program* program = s.pipeline_cache.FindCompiledProgram(params.hash);
    if (!program) {
        why = "the engine's shader is not compiled yet (the engine draws this frame)";
        return false;
    }
    hash = params.hash;
    info = &program->info;
    pgm_base = params.Base();
    flat.assign(std::max<u32>(info->srt_info.flattened_bufsize_dw, Shader::NUM_USER_DATA_REGS), 0);
    std::memcpy(flat.data(), params.user_data.data(),
                std::min<size_t>(params.user_data.size_bytes(), Shader::NUM_USER_DATA_REGS * 4));
    if (info->srt_info.walker_func) info->srt_info.walker_func(params.user_data.data(), flat.data());
    const FlatView view{flat, pgm_base};
    images.clear();
    for (const auto& image : info->images) {
        images.push_back(image.GetSharp(view));
        if (!images.back().Valid()) {
            why = "an image descriptor of the draw is not valid";
            return false;
        }
    }
    return true;
}

bool DrawInputs::Constants(void* out, u32 bytes) const {
    const FlatView view{flat, pgm_base};
    for (const auto& buffer : info->buffers) {
        if (buffer.buffer_type != Shader::BufferType::Guest) continue;
        const AmdGpu::Buffer vsharp = buffer.GetSharp(view);
        if (!vsharp.base_address || vsharp.GetSize() < bytes) continue;
        std::memcpy(out, reinterpret_cast<const void*>(vsharp.base_address), bytes);
        return true;
    }
    return false;
}

bool Target::Find(const Services& s, const char*& why) {
    const auto& regs = s.regs;
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
    auto& cache = s.texture_cache;
    const AmdGpu::CbDbExtent hint = s.cb0_extent ? *s.cb0_extent : AmdGpu::CbDbExtent{};
    VideoCore::TextureCache::ImageDesc desc(cb0, hint);
    id = cache.FindImage(desc);
    if (!id) {
        why = "the colour target is not in the texture cache";
        return false;
    }
    auto& target_view = cache.FindRenderTarget(id, desc);
    const auto& image = cache.GetImage(id);
    format = target_view.info.format;
    if (!IsUnorm8(format) || image.info.num_samples != 1) {
        why = "the target's format is not an 8- or 10-bit UNORM one";
        return false;
    }
    view = *target_view.image_view;
    size = {image.info.size.width, image.info.size.height};
    const auto order = cb0.Swizzle();
    for (u32 i = 0; i < 4; ++i) swizzle[i] = static_cast<std::int32_t>(order.Map(i));
    return true;
}

FullScreenPass::FullScreenPass(const char* name_, std::span<const u32> spirv_, u32 push_bytes_,
                               std::vector<Filter> samplers)
    : name(name_), spirv(spirv_), push_bytes(push_bytes_), filters(std::move(samplers)) {}

bool FullScreenPass::Ready(const Services& s) {
    if (tried) return ok;
    tried = true;
    device = s.instance.GetDevice();
    std::vector<vk::DescriptorSetLayoutBinding> bindings(filters.size());
    for (u32 i = 0; i < bindings.size(); ++i) {
        bindings[i] = {.binding = i,
                       .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                       .descriptorCount = 1,
                       .stageFlags = vk::ShaderStageFlagBits::eFragment};
    }
    auto set = device.createDescriptorSetLayout({
        .flags = vk::DescriptorSetLayoutCreateFlagBits::ePushDescriptor,
        .bindingCount = static_cast<u32>(bindings.size()),
        .pBindings = bindings.data(),
    });
    if (set.result != vk::Result::eSuccess) return false;
    set_layout = set.value;
    const vk::PushConstantRange push{.stageFlags = vk::ShaderStageFlagBits::eFragment,
                                     .offset = 0,
                                     .size = push_bytes};
    auto pipeline_layout = device.createPipelineLayout({
        .setLayoutCount = 1,
        .pSetLayouts = &set_layout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &push,
    });
    if (pipeline_layout.result != vk::Result::eSuccess) return false;
    layout = pipeline_layout.value;
    auto lin = device.createSampler({
        .magFilter = vk::Filter::eLinear,
        .minFilter = vk::Filter::eLinear,
        .mipmapMode = vk::SamplerMipmapMode::eNearest,
        .addressModeU = vk::SamplerAddressMode::eClampToEdge,
        .addressModeV = vk::SamplerAddressMode::eClampToEdge,
        .addressModeW = vk::SamplerAddressMode::eClampToEdge,
        .maxLod = 0.0f,
    });
    auto near = device.createSampler({
        .magFilter = vk::Filter::eNearest,
        .minFilter = vk::Filter::eNearest,
        .mipmapMode = vk::SamplerMipmapMode::eNearest,
        .addressModeU = vk::SamplerAddressMode::eRepeat,
        .addressModeV = vk::SamplerAddressMode::eRepeat,
        .addressModeW = vk::SamplerAddressMode::eRepeat,
        .maxLod = 0.0f,
    });
    if (lin.result != vk::Result::eSuccess || near.result != vk::Result::eSuccess) return false;
    linear = lin.value;
    nearest = near.value;
    vert = Vulkan::CompileSPV(FS_TRI_VERT, device);
    frag = Vulkan::CompileSPV(spirv, device);
    if (!vert || !frag) return false;
    ok = true;
    return true;
}

vk::Pipeline FullScreenPass::PipelineFor(vk::Format format) {
    const auto it = pipelines.find(static_cast<VkFormat>(format));
    if (it != pipelines.end()) return it->second;
    const std::array<vk::PipelineShaderStageCreateInfo, 2> stages{{
        {.stage = vk::ShaderStageFlagBits::eVertex, .module = vert, .pName = "main"},
        {.stage = vk::ShaderStageFlagBits::eFragment, .module = frag, .pName = "main"},
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
        .layout = layout,
    };
    auto result = device.createGraphicsPipeline({}, info);
    const vk::Pipeline pipeline =
        result.result == vk::Result::eSuccess ? result.value : vk::Pipeline{};
    pipelines.emplace(static_cast<VkFormat>(format), pipeline);
    return pipeline;
}

bool FullScreenPass::FindImages(const Services& s, const DrawInputs& inputs,
                                std::vector<VideoCore::ImageId>& ids,
                                std::vector<vk::ImageView>& views, const char*& why) const {
    auto& cache = s.texture_cache;
    ids.clear();
    views.clear();
    for (u32 i = 0; i < inputs.images.size(); ++i) {
        VideoCore::TextureCache::ImageDesc desc(inputs.images[i], inputs.info->images[i]);
        if (desc.type != VideoCore::TextureCache::BindingType::Texture) {
            why = "an input image is bound for writing";
            return false;
        }
        const VideoCore::ImageId id = cache.FindImage(desc);
        if (!id) {
            why = "an input image is not in the texture cache";
            return false;
        }
        ids.push_back(id);
        views.push_back(*cache.FindTexture(id, desc).image_view);
    }
    return true;
}

bool FullScreenPass::Record(const Services& s, std::span<const VideoCore::ImageId> ids,
                            std::span<const vk::ImageView> views, const Target& target,
                            const void* push, const char*& why) {
    if (ids.size() != filters.size() || views.size() != filters.size()) {
        why = "the pass's image count does not match its shader";
        return false;
    }
    for (const auto id : ids) {
        if (id == target.id) {
            why = "the pass reads its own target";
            return false;
        }
    }
    const vk::Pipeline pipeline = PipelineFor(target.format);
    if (!pipeline) {
        why = "the native pipeline could not be made";
        return false;
    }
    auto& cache = s.texture_cache;
    // Outside the rasterizer's render pass: the inputs readable, the target writable.
    s.scheduler.EndRendering();
    for (const auto id : ids) {
        s.runtime.Transit(&cache.GetImage(id), vk::ImageLayout::eShaderReadOnlyOptimal,
                          vk::PipelineStageFlagBits2::eFragmentShader,
                          vk::AccessFlagBits2::eShaderSampledRead);
    }
    s.runtime.Transit(&cache.GetImage(target.id), vk::ImageLayout::eColorAttachmentOptimal,
                      vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                      vk::AccessFlagBits2::eColorAttachmentWrite);
    s.runtime.FlushBarriers();

    std::vector<vk::DescriptorImageInfo> image_infos(ids.size());
    std::vector<vk::WriteDescriptorSet> writes(ids.size());
    for (u32 i = 0; i < ids.size(); ++i) {
        image_infos[i] = {.sampler = filters[i] == Filter::Linear ? linear : nearest,
                          .imageView = views[i],
                          .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal};
        writes[i] = {.dstBinding = i,
                     .descriptorCount = 1,
                     .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                     .pImageInfo = &image_infos[i]};
    }
    const vk::RenderingAttachmentInfo color{
        .imageView = target.view,
        .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eDontCare,
        .storeOp = vk::AttachmentStoreOp::eStore,
    };
    const vk::Rect2D area{{0, 0}, {target.size.width, target.size.height}};
    const auto cmdbuf = s.scheduler.CommandBuffer();
    cmdbuf.beginRendering(vk::RenderingInfo{
        .renderArea = area, .layerCount = 1, .colorAttachmentCount = 1, .pColorAttachments = &color});
    cmdbuf.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);
    cmdbuf.pushDescriptorSetKHR(vk::PipelineBindPoint::eGraphics, layout, 0, writes);
    cmdbuf.pushConstants(layout, vk::ShaderStageFlagBits::eFragment, 0, push_bytes, push);
    cmdbuf.setViewport(0, vk::Viewport{0.0f, 0.0f, float(target.size.width),
                                       float(target.size.height), 0.0f, 1.0f});
    cmdbuf.setScissor(0, area);
    cmdbuf.draw(3, 1, 0, 0);
    cmdbuf.endRendering();
    return true;
}

} // namespace iss::native_render
