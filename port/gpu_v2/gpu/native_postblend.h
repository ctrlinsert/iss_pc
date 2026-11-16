// SPDX-License-Identifier: GPL-2.0-or-later
// Native renderer: the colour composite before anti-aliasing (Pass::PostBlend), with our own
// shader (host_shaders/iss_native_postblend.frag). native_postblend.cpp explains the inputs.
#pragma once

#include "native_render.h"

namespace iss::native_render {

/// Pass::PostBlend's draw handler (DrawFn): the composite's one full-screen draw.
bool PostBlendDraw(const Services& services, const DrawRequest& request, const char*& why);

} // namespace iss::native_render
