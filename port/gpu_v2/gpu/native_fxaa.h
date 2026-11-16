// SPDX-License-Identifier: GPL-2.0-or-later
// Native renderer: the anti-aliasing pass in FXAA mode (Pass::Fxaa), with our own shader
// (host_shaders/iss_native_fxaa.frag).
#pragma once

#include "native_render.h"

namespace iss::native_render {

/// Pass::Fxaa's draw handler (DrawFn): the pass's one full-screen draw.
bool FxaaDraw(const Services& services, const DrawRequest& request, const char*& why);

} // namespace iss::native_render
