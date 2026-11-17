// SPDX-License-Identifier: GPL-2.0-or-later
// Native renderer: the colour composite before anti-aliasing (native_postblend.cpp).
//
// Our own shader. The scene and bloom are combined, toned and graded with the game's per-frame
// values (exposure, contrast curve, grading tables, vignette), and every size comes from the real
// target: texel steps from its size, and the dither noise tiles 1:1 with its pixels.
#version 450

layout(location = 0) out vec4 out_color;

layout(set = 0, binding = 0) uniform sampler2D distortion_tex;
layout(set = 0, binding = 1) uniform sampler2D scene_tex;
layout(set = 0, binding = 2) uniform sampler2D bloom_tex;
layout(set = 0, binding = 3) uniform sampler3D grade_a_tex;
layout(set = 0, binding = 4) uniform sampler3D grade_b_tex;
layout(set = 0, binding = 5) uniform sampler2D noise_tex;

layout(push_constant) uniform Params {
    vec4 tone_a;     // exposure, knee, low exponent, low gain
    vec4 tone_b;     // high exponent, high gain, grey amount, floor
    vec4 grade;      // distortion strength, grade blend, grade A scale, grade A offset
    vec4 grade_vig;  // grade B scale, grade B offset, vignette slope, vignette bias
    vec4 shape;      // vignette scale x, y; 1 / target width, 1 / target height
    vec4 noise;      // noise tiling x, y; 1 = the noise has colour channels; unused
    ivec4 swizzle;   // which component each output channel takes (the target's channel order)
} p;

const vec3 kLuma = vec3(0.2126, 0.7152, 0.0722); // Rec. 709

// Narkowicz's fit of the ACES filmic curve (2015, public), then the sRGB transfer function.
vec3 Filmic(vec3 x) {
    x = clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
    return mix(1.055 * pow(x, vec3(1.0 / 2.4)) - 0.055, 12.92 * x, lessThan(x, vec3(0.0031308)));
}

void main() {
    const vec2 uv = gl_FragCoord.xy * p.shape.zw;
    const vec2 suv = uv + texture(distortion_tex, uv).rg * p.grade.x;

    vec3 c = texture(scene_tex, suv).rgb * p.tone_a.x + texture(bloom_tex, suv).rgb;

    // Desaturation toward grey, and a floor that lifts each channel to a share of the luma.
    float y = dot(c, kLuma);
    c = mix(c, vec3(y), p.tone_b.z);
    c = max(c, vec3(p.tone_b.w * y));

    // Contrast: a power curve on luma with its own gain and exponent below and above the knee,
    // applied as a ratio so hue is kept.
    y = dot(c, kLuma);
    if (y > 1e-6) {
        const float shaped = y < p.tone_a.y ? p.tone_a.w * pow(y, p.tone_a.z)
                                            : p.tone_b.y * pow(y, p.tone_b.x);
        c *= shaped / y;
    }

    // Vignette, with the game's per-axis scale.
    const float r = length((uv - 0.5) * p.shape.xy);
    const float t = clamp(p.grade_vig.z * r + p.grade_vig.w, 0.0, 1.0);
    c = max(c * (t + t * t - t * t * t), vec3(0.0));

    c = Filmic(c);

    // Grading tables, the second one blended in by the game's amount.
    vec3 graded = texture(grade_a_tex, c * p.grade.z + p.grade.w).rgb;
    if (p.grade.y > 0.0) {
        graded = mix(graded, texture(grade_b_tex, c * p.grade_vig.x + p.grade_vig.y).rgb, p.grade.y);
    }

    // Alpha carries luma for the anti-aliasing pass after this one.
    const float alpha = dot(graded, kLuma);
    const vec4 n = texture(noise_tex, uv * p.noise.xy);
    const vec3 dither = p.noise.z > 0.5 ? n.rgb : n.rrr;
    graded += (dither - 0.5) / 255.0;

    const vec4 v = vec4(graded, alpha);
    out_color = vec4(v[p.swizzle.x], v[p.swizzle.y], v[p.swizzle.z], v[p.swizzle.w]);
}
