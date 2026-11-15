// SPDX-License-Identifier: GPL-2.0-or-later
// Native renderer, stage 2: the renderer side of a native pass (native_render_core.h explains the
// markers, the span check and the fallback).
//
// ISS_NATIVE_RENDER selects the passes (off by default). ISS_NATIVE_RENDER_LOG=1 prints every
// bracketed span the first few times it is seen (pass, draws, dispatches, verdict).
#pragma once

#include "native_render_core.h"

namespace Vulkan {
class Instance;
class Scheduler;
class Runtime;
} // namespace Vulkan

namespace VideoCore {
class TextureCache;
}

namespace AmdGpu {
union Regs;
}

namespace iss::native_render {

/// What a pass records with. The pass ends any render pass the rasterizer had open itself.
struct Services {
    const Vulkan::Instance& instance;
    Vulkan::Scheduler& scheduler;
    Vulkan::Runtime& runtime;
    VideoCore::TextureCache& texture_cache;
    /// The registers as the engine left them at BEGIN (its render target, viewport). A pass that
    /// reads its destination here declines when the span writes the colour targets
    /// (SpanResult::target_writes).
    const AmdGpu::Regs& regs;
};

struct Request {
    const Marker& begin;    ///< the BEGIN marker, with the payload the game hook wrote
    const SpanResult& span; ///< what the engine recorded for the pass (to be dropped)
};

/// A pass: records its work and returns true, or records nothing, sets `why` (a fixed string,
/// logged once per reason) and returns false, so the engine's own commands run.
using RunFn = bool (*)(const Services& services, const Request& request, const char*& why);

const Switches& Active();
/// Whether any pass is on (the command processor skips everything here otherwise).
bool AnyEnabled();

/// Command processor, at a BEGIN marker (`packet` its header, `limit` the end of the command
/// buffer). `blocked`: no native pass may run now (pause guard, split command processor, the
/// temporal upscaler's UI redirect). Returns the END marker when the pass ran natively: the
/// command processor then interprets up to it with draws and dispatches dropped. nullptr: the
/// span is interpreted as usual.
const u32* OnBegin(const Services& services, const u32* packet, const u32* limit, bool blocked);

/// Command processor, a draw or dispatch dropped inside a native span.
void NoteDropped(bool dispatch);

/// Periodic report (OnBegin calls it: at most every 10 seconds).
void Report();

} // namespace iss::native_render
