// SPDX-License-Identifier: GPL-2.0-or-later
// Native renderer: FXAA (native_fxaa.cpp).
//
// Our own implementation of the public FXAA quality algorithm (Lottes, 2009-2011): find the
// local contrast from luma (carried in alpha by the composite before it), skip pixels below the
// threshold, pick the edge direction, walk along the edge both ways to its ends, and shift the
// sample across the edge by how far this pixel sits from the nearer end; add a sub-pixel blend for
// single-pixel detail. The tuning (sub-pixel amount, thresholds) is the game's; the texel step is
// the real target's.
#version 450

layout(location = 0) out vec4 out_color;

layout(set = 0, binding = 0) uniform sampler2D src; // linear, clamped

layout(push_constant) uniform Params {
    vec2 texel;          // 1 / target size
    float subpix;        // sub-pixel blend amount
    float edge_threshold;
    float edge_threshold_min;
    float pad0, pad1, pad2;
    ivec4 swizzle;       // which component each output channel takes (the target's channel order)
} p;

// The walk along the edge: growing steps, in texels.
const int kSteps = 5;
const float kStep[kSteps] = float[](1.0, 1.5, 2.0, 4.0, 12.0);

float Luma(vec2 uv) { return textureLod(src, uv, 0.0).a; }
float LumaAt(vec2 uv, vec2 texels) { return textureLod(src, uv + texels * p.texel, 0.0).a; }

vec4 Out(vec4 v) { return vec4(v[p.swizzle.x], v[p.swizzle.y], v[p.swizzle.z], v[p.swizzle.w]); }

void main() {
    const vec2 uv = gl_FragCoord.xy * p.texel;
    const vec4 centre = textureLod(src, uv, 0.0);
    const float m = centre.a;
    const float n = LumaAt(uv, vec2(0.0, -1.0));
    const float s = LumaAt(uv, vec2(0.0, 1.0));
    const float w = LumaAt(uv, vec2(-1.0, 0.0));
    const float e = LumaAt(uv, vec2(1.0, 0.0));

    const float hi = max(max(max(n, s), max(w, e)), m);
    const float lo = min(min(min(n, s), min(w, e)), m);
    const float range = hi - lo;
    if (range < max(p.edge_threshold_min, hi * p.edge_threshold)) {
        out_color = Out(centre);
        return;
    }

    const float nw = LumaAt(uv, vec2(-1.0, -1.0));
    const float ne = LumaAt(uv, vec2(1.0, -1.0));
    const float sw = LumaAt(uv, vec2(-1.0, 1.0));
    const float se = LumaAt(uv, vec2(1.0, 1.0));

    // Sub-pixel: how far the centre stands out from its neighbourhood's average.
    const float average = (2.0 * (n + s + w + e) + (nw + ne + sw + se)) / 12.0;
    const float contrast = clamp(abs(average - m) / range, 0.0, 1.0);
    const float smooth_contrast = (3.0 - 2.0 * contrast) * contrast * contrast;
    const float sub_offset = smooth_contrast * smooth_contrast * p.subpix;

    // Edge direction: a horizontal edge changes most from top to bottom.
    const float across_rows = abs(0.25 * nw - 0.5 * w + 0.25 * sw) +
                              abs(0.5 * n - m + 0.5 * s) +
                              abs(0.25 * ne - 0.5 * e + 0.25 * se);
    const float across_cols = abs(0.25 * nw - 0.5 * n + 0.25 * ne) +
                              abs(0.5 * w - m + 0.5 * e) +
                              abs(0.25 * sw - 0.5 * s + 0.25 * se);
    const bool horizontal = across_rows >= across_cols;

    // The side of the edge with the larger step, and a step across it.
    const float before = horizontal ? n : w;
    const float after = horizontal ? s : e;
    const float d_before = abs(before - m);
    const float d_after = abs(after - m);
    const bool toward_before = d_before >= d_after;
    const float gradient = 0.25 * max(d_before, d_after);
    float across = horizontal ? p.texel.y : p.texel.x;
    float local = 0.5 * (m + (toward_before ? before : after));
    if (toward_before) across = -across;

    // Start on the edge itself, half a texel across.
    vec2 edge_uv = uv;
    if (horizontal) edge_uv.y += 0.5 * across; else edge_uv.x += 0.5 * across;
    const vec2 along = horizontal ? vec2(p.texel.x, 0.0) : vec2(0.0, p.texel.y);

    // Walk both ways until the luma along the edge leaves the local average.
    vec2 uv_neg = edge_uv - along * kStep[0];
    vec2 uv_pos = edge_uv + along * kStep[0];
    float end_neg = Luma(uv_neg) - local;
    float end_pos = Luma(uv_pos) - local;
    bool done_neg = abs(end_neg) >= gradient;
    bool done_pos = abs(end_pos) >= gradient;
    for (int i = 1; i < kSteps && !(done_neg && done_pos); ++i) {
        if (!done_neg) {
            uv_neg -= along * kStep[i];
            end_neg = Luma(uv_neg) - local;
            done_neg = abs(end_neg) >= gradient;
        }
        if (!done_pos) {
            uv_pos += along * kStep[i];
            end_pos = Luma(uv_pos) - local;
            done_pos = abs(end_pos) >= gradient;
        }
    }

    const float dist_neg = horizontal ? uv.x - uv_neg.x : uv.y - uv_neg.y;
    const float dist_pos = horizontal ? uv_pos.x - uv.x : uv_pos.y - uv.y;
    const bool nearer_neg = dist_neg < dist_pos;
    const float nearest = min(dist_neg, dist_pos);
    const float span = dist_neg + dist_pos;

    // Shift only when the nearer end really is where the edge turns away from this pixel.
    const bool centre_below = m - local < 0.0;
    const bool good = ((nearer_neg ? end_neg : end_pos) < 0.0) != centre_below;
    const float edge_offset = good ? 0.5 - nearest / span : 0.0;
    const float offset = max(edge_offset, sub_offset);

    vec2 final_uv = uv;
    if (horizontal) final_uv.y += offset * across; else final_uv.x += offset * across;
    vec4 result = textureLod(src, final_uv, 0.0);
    out_color = Out(result);
}
