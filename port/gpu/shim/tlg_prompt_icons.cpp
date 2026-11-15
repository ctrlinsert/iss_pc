// SPDX-License-Identifier: GPL-2.0-or-later
// tlg: Xbox and keyboard button prompts (src/iss_prompt_icons.h has the format).
//
// The game's prompts are glyphs U+F080.. of one font in lang_<language>_text.xpps, drawn from a
// single RGBA8 atlas. When the game reads that package, the atlas bytes that land in guest memory
// are rewritten here: the PS4 icon cells are redrawn as Xbox buttons or as keyboard keys (the key
// bound to each PS4 button, runtime_pad.c), and the rest of the atlas (logo, cursors, d-pad) is
// left as the game's. Nothing is shipped and no file is changed: the icons are drawn by code,
// and the original texels are kept, so switching back to PlayStation restores them.
//
// TLG_PROMPTS=auto (default: the device used last; a PlayStation pad keeps the game's icons),
// ps, xbox or keyboard. When the style changes, atlases already in guest memory are rewritten;
// the texture cache sees the CPU write and uploads the texture again.

#include "../../src/iss_prompt_icons.h"

#include <algorithm>
#include <atomic>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace {

// PS4 pad bits (libScePad, runtime_pad.c).
enum : uint32_t {
    L3 = 0x2, R3 = 0x4, OPTIONS = 0x8, L2 = 0x100, R2 = 0x200, L1 = 0x400, R1 = 0x800,
    TRIANGLE = 0x1000, CIRCLE = 0x2000, CROSS = 0x4000, SQUARE = 0x8000, TOUCHPAD = 0x100000,
    CLICK_RIGHT = 0x80000000,
};

// 5x7 capitals, digits and a few signs (own design, one string of 7 rows per character).
struct Char5x7 { char c; const char* rows[7]; };
constexpr Char5x7 kFont[] = {
    {'A', {".###.", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"}},
    {'B', {"####.", "#...#", "#...#", "####.", "#...#", "#...#", "####."}},
    {'C', {".###.", "#...#", "#....", "#....", "#....", "#...#", ".###."}},
    {'D', {"####.", "#...#", "#...#", "#...#", "#...#", "#...#", "####."}},
    {'E', {"#####", "#....", "#....", "####.", "#....", "#....", "#####"}},
    {'F', {"#####", "#....", "#....", "####.", "#....", "#....", "#...."}},
    {'G', {".###.", "#...#", "#....", "#.###", "#...#", "#...#", ".####"}},
    {'H', {"#...#", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"}},
    {'I', {"#####", "..#..", "..#..", "..#..", "..#..", "..#..", "#####"}},
    {'J', {"..###", "...#.", "...#.", "...#.", "#..#.", "#..#.", ".##.."}},
    {'K', {"#...#", "#..#.", "#.#..", "##...", "#.#..", "#..#.", "#...#"}},
    {'L', {"#....", "#....", "#....", "#....", "#....", "#....", "#####"}},
    {'M', {"#...#", "##.##", "#.#.#", "#.#.#", "#...#", "#...#", "#...#"}},
    {'N', {"#...#", "##..#", "#.#.#", "#..##", "#...#", "#...#", "#...#"}},
    {'O', {".###.", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."}},
    {'P', {"####.", "#...#", "#...#", "####.", "#....", "#....", "#...."}},
    {'Q', {".###.", "#...#", "#...#", "#...#", "#.#.#", "#..#.", ".##.#"}},
    {'R', {"####.", "#...#", "#...#", "####.", "#.#..", "#..#.", "#...#"}},
    {'S', {".####", "#....", "#....", ".###.", "....#", "....#", "####."}},
    {'T', {"#####", "..#..", "..#..", "..#..", "..#..", "..#..", "..#.."}},
    {'U', {"#...#", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."}},
    {'V', {"#...#", "#...#", "#...#", "#...#", "#...#", ".#.#.", "..#.."}},
    {'W', {"#...#", "#...#", "#...#", "#.#.#", "#.#.#", "##.##", "#...#"}},
    {'X', {"#...#", "#...#", ".#.#.", "..#..", ".#.#.", "#...#", "#...#"}},
    {'Y', {"#...#", "#...#", ".#.#.", "..#..", "..#..", "..#..", "..#.."}},
    {'Z', {"#####", "....#", "...#.", "..#..", ".#...", "#....", "#####"}},
    {'0', {".###.", "#...#", "#..##", "#.#.#", "##..#", "#...#", ".###."}},
    {'1', {"..#..", ".##..", "..#..", "..#..", "..#..", "..#..", ".###."}},
    {'2', {".###.", "#...#", "....#", "...#.", "..#..", ".#...", "#####"}},
    {'3', {"####.", "....#", "....#", ".###.", "....#", "....#", "####."}},
    {'4', {"...#.", "..##.", ".#.#.", "#..#.", "#####", "...#.", "...#."}},
    {'5', {"#####", "#....", "####.", "....#", "....#", "#...#", ".###."}},
    {'6', {".###.", "#....", "#....", "####.", "#...#", "#...#", ".###."}},
    {'7', {"#####", "....#", "...#.", "..#..", ".#...", ".#...", ".#..."}},
    {'8', {".###.", "#...#", "#...#", ".###.", "#...#", "#...#", ".###."}},
    {'9', {".###.", "#...#", "#...#", ".####", "....#", "....#", ".###."}},
    {'-', {".....", ".....", ".....", "#####", ".....", ".....", "....."}},
    {'+', {".....", "..#..", "..#..", "#####", "..#..", "..#..", "....."}},
    {'/', {"....#", "....#", "...#.", "..#..", ".#...", "#....", "#...."}},
    {'.', {".....", ".....", ".....", ".....", ".....", ".##..", ".##.."}},
    {'^', {"..#..", ".#.#.", "#...#", ".....", ".....", ".....", "....."}},
};
const Char5x7* FindChar(char c) {
    for (const auto& f : kFont) if (f.c == c) return &f;
    return nullptr;
}

// One glyph cell drawn as coverage (0..1) of white; shapes are in cell units (0..1 square).
struct Shape {
    enum Kind { Disc, Pill, Key } kind;
    std::string label;
};

// Signed distance to a rounded rectangle centred at the origin (half extents hx, hy, radius r).
float RoundRect(float x, float y, float hx, float hy, float r) {
    const float qx = std::fabs(x) - hx + r, qy = std::fabs(y) - hy + r;
    return std::hypot(std::max(qx, 0.0f), std::max(qy, 0.0f)) + std::min(std::max(qx, qy), 0.0f) - r;
}

// Text coverage at (x, y) for a label centred in the box [-hx, hx] x [-hy, hy].
float TextAt(const std::string& text, float x, float y, float hx, float hy) {
    const int n = int(text.size());
    if (!n) return 0;
    // Character cell 6x7 units (5 + 1 spacing); fit the line into the box.
    const float units_w = 6.0f * n - 1.0f, units_h = 7.0f;
    const float unit = std::min(2.0f * hx / units_w, 2.0f * hy / units_h);
    const float u = (x + units_w * unit / 2) / unit, v = (y + units_h * unit / 2) / unit;
    if (u < 0 || v < 0 || u >= units_w || v >= units_h) return 0;
    const int col = int(u), row = int(v), ch = col / 6, cx = col % 6;
    if (cx == 5) return 0;
    const Char5x7* f = FindChar(text[size_t(ch)]);
    return f && f->rows[row][cx] == '#' ? 1.0f : 0.0f;
}

// Draws `shape` into a w x h RGBA block (rows top-down), 4x4 supersampled.
void Draw(const Shape& shape, int w, int h, std::vector<uint8_t>& out) {
    out.assign(size_t(w) * h * 4, 0);
    const float s = float(std::min(w, h));
    const float aspect_x = float(w) / s, aspect_y = float(h) / s;
    for (int py = 0; py < h; ++py) {
        for (int px = 0; px < w; ++px) {
            float sum = 0;
            for (int sy = 0; sy < 4; ++sy) {
                for (int sx = 0; sx < 4; ++sx) {
                    // Cell coordinates centred at 0, unit = the cell's short side.
                    const float x = ((px + (sx + 0.5f) / 4) / s) - aspect_x / 2;
                    const float y = ((py + (sy + 0.5f) / 4) / s) - aspect_y / 2;
                    float body = 0, hx = 0, hy = 0;
                    switch (shape.kind) {
                    case Shape::Disc:
                        body = std::hypot(x, y) <= 0.46f ? 1.0f : 0.0f;
                        hx = 0.25f, hy = 0.22f;
                        break;
                    case Shape::Pill: {
                        const float bx = aspect_x / 2 - 0.04f, by = std::min(0.30f, aspect_y / 2 - 0.04f);
                        body = RoundRect(x, y, bx, by, by) <= 0 ? 1.0f : 0.0f;
                        hx = bx - 0.12f, hy = by * 0.55f;
                        break;
                    }
                    case Shape::Key: {
                        const float bx = aspect_x / 2 - 0.04f, by = aspect_y / 2 - 0.04f;
                        body = RoundRect(x, y, bx, by, 0.12f) <= 0 ? 1.0f : 0.0f;
                        hx = bx - 0.08f, hy = std::min(by - 0.08f, 0.24f);
                        break;
                    }
                    }
                    // The label is cut out of the white body, as the game's own icons are.
                    sum += body * (1.0f - TextAt(shape.label, x, y, hx, hy));
                }
            }
            uint8_t* p = &out[(size_t(py) * w + px) * 4];
            p[0] = p[1] = p[2] = 255;
            p[3] = uint8_t(std::lround(sum / 16.0f * 255.0f));
        }
    }
}

// Xbox layout of the PS4 icons; code points not listed keep the game's icon.
bool XboxShape(uint16_t cp, Shape& out) {
    struct Entry { uint16_t cp; Shape::Kind kind; const char* label; };
    static constexpr Entry kXbox[] = {
        {0xF080, Shape::Disc, "B"}, {0xF081, Shape::Disc, "X"}, {0xF082, Shape::Disc, "Y"},
        {0xF083, Shape::Disc, "A"}, {0xF084, Shape::Key, "LB"}, {0xF085, Shape::Key, "LT"},
        {0xF086, Shape::Key, "RB"}, {0xF087, Shape::Key, "RT"}, {0xF088, Shape::Pill, "MENU"},
        {0xF089, Shape::Pill, "MENU"}, {0xF08B, Shape::Disc, "LS"}, {0xF08D, Shape::Disc, "RS"},
        {0xF098, Shape::Pill, "VIEW"}, {0xF0E0, Shape::Key, "LB"}, {0xF0E1, Shape::Key, "LT"},
        {0xF0E2, Shape::Key, "RB"}, {0xF0E3, Shape::Key, "RT"}, {0xF0E4, Shape::Disc, "LS"},
        {0xF0E5, Shape::Disc, "RS"}, {0xF0E6, Shape::Pill, "MENU"},
    };
    for (const auto& e : kXbox) if (e.cp == cp) { out = {e.kind, e.label}; return true; }
    return false;
}

// Keyboard: each PS4 icon becomes the key bound to that button.
bool KeyboardShape(uint16_t cp, Shape& out) {
    struct Entry { uint16_t cp; uint32_t button; const char* fallback; };
    static constexpr Entry kKeys[] = {
        {0xF080, CIRCLE, nullptr}, {0xF081, SQUARE, nullptr}, {0xF082, TRIANGLE, nullptr},
        {0xF083, CROSS, nullptr}, {0xF084, L1, nullptr}, {0xF085, L2, nullptr}, {0xF086, R1, nullptr},
        {0xF087, R2, nullptr}, {0xF088, OPTIONS, nullptr}, {0xF089, OPTIONS, nullptr},
        {0xF08A, 0, "WASD"}, {0xF08B, L3, nullptr}, {0xF08C, 0, "MOUSE"}, {0xF08D, R3, nullptr},
        {0xF098, TOUCHPAD, nullptr}, {0xF0E0, L1, nullptr}, {0xF0E1, L2, nullptr},
        {0xF0E2, R1, nullptr}, {0xF0E3, R2, nullptr}, {0xF0E4, L3, nullptr}, {0xF0E5, R3, nullptr},
        {0xF0E6, OPTIONS, nullptr},
    };
    for (const auto& e : kKeys) {
        if (e.cp != cp) continue;
        const char* label = e.fallback;
        if (e.button) {
            label = runtime_pad_key_label(e.button);
            if (!label && e.button == TOUCHPAD) label = runtime_pad_key_label(CLICK_RIGHT);
        }
        if (!label) return false;
        out = {Shape::Key, label};
        return true;
    }
    return false;
}

struct Segment { uint64_t file_begin, file_end; uint8_t* guest; };
struct Package {
    IpiAtlas atlas{};
    bool found = false;
    std::vector<uint8_t> original;                  // the game's atlas texels (tiled)
    std::array<std::vector<uint8_t>, 3> styled;     // per style, built on first use
    std::vector<Segment> segments;                  // where the atlas is in guest memory
};

std::mutex mutex;
std::map<std::string, Package> packages;            // by host path
std::map<int, std::string> open_files;              // fd -> host path
int applied_style = IPI_STYLE_PS;                   // what the segments hold (under mutex)
std::atomic<int> applied_style_seen{IPI_STYLE_PS};  // applied_style, read without the mutex
std::atomic<int> wanted_style{IPI_STYLE_PS};        // the device used last
std::atomic<int> forced_style{-2};                  // TLG_PROMPTS: -2 unread, -1 auto

int Forced() {
    int forced = forced_style.load(std::memory_order_relaxed);
    if (forced != -2) return forced;
    const char* v = std::getenv("TLG_PROMPTS");
    forced = -1;
    if (v && !std::strcmp(v, "ps")) forced = IPI_STYLE_PS;
    else if (v && !std::strcmp(v, "xbox")) forced = IPI_STYLE_XBOX;
    else if (v && !std::strcmp(v, "keyboard")) forced = IPI_STYLE_KEYBOARD;
    else if (v && *v && std::strcmp(v, "auto")) std::printf("Prompts: TLG_PROMPTS=%s unknown (auto|ps|xbox|keyboard): auto\n", v);
    forced_style.store(forced, std::memory_order_relaxed);
    return forced;
}
int Effective() {
    const int forced = Forced();
    return forced >= 0 ? forced : wanted_style.load(std::memory_order_relaxed);
}

const std::vector<uint8_t>& Styled(Package& p, int style) {
    if (style == IPI_STYLE_PS) return p.original;
    auto& out = p.styled[size_t(style)];
    if (!out.empty()) return out;
    out = p.original;
    const IpiAtlas& a = p.atlas;
    std::vector<uint8_t> cell;
    int drawn = 0;
    for (uint32_t i = 0; i < a.count; ++i) {
        const IpiGlyph& g = a.glyphs[i];
        Shape shape;
        if (!(style == IPI_STYLE_XBOX ? XboxShape(g.cp, shape) : KeyboardShape(g.cp, shape))) continue;
        Draw(shape, g.w, g.h, cell);
        for (int y = 0; y < g.h; ++y)
            for (int x = 0; x < g.w; ++x)
                std::memcpy(&out[ipi_texel_offset(g.x + x, g.y + y, a.width, a.height)],
                            &cell[(size_t(y) * g.w + x) * 4], 4);
        ++drawn;
    }
    std::printf("Prompts: %s icons drawn (%d glyphs)\n", style == IPI_STYLE_XBOX ? "Xbox" : "keyboard", drawn);
    return out;
}

// The part of segment `s` that holds atlas texels: guest pointer, atlas offset and length.
struct Span { uint8_t* guest; size_t at, size; };
Span AtlasSpan(const Package& p, const Segment& s) {
    const uint64_t begin = std::max(s.file_begin, p.atlas.pixels);
    const uint64_t end = std::min(s.file_end, p.atlas.pixels + p.original.size());
    if (begin >= end) return {nullptr, 0, 0};
    return {s.guest + (begin - s.file_begin), size_t(begin - p.atlas.pixels), size_t(end - begin)};
}
// Copies the style's texels into the part of the segment that holds the atlas.
void Apply(Package& p, const Segment& s, int style) {
    const Span span = AtlasSpan(p, s);
    if (span.size) std::memcpy(span.guest, Styled(p, style).data() + span.at, span.size);
}
// Whether the segment still holds the texels written for `style` (the game may have freed the
// package and reused the memory: then it is left alone).
bool Holds(Package& p, const Segment& s, int style) {
    const Span span = AtlasSpan(p, s);
    return span.size && !std::memcmp(span.guest, Styled(p, style).data() + span.at, span.size);
}

// Rewrites every atlas in guest memory to `style` (mutex held).
void Switch(int style) {
    if (style == applied_style) return;
    int rewritten = 0;
    for (auto& [path, p] : packages) {
        std::erase_if(p.segments, [&](const Segment& s) { return !Holds(p, s, applied_style); });
        for (const auto& s : p.segments) { Apply(p, s, style); ++rewritten; }
    }
    applied_style = style;
    applied_style_seen.store(style, std::memory_order_relaxed);
    if (rewritten) std::printf("Prompts: switched to %s\n", style == IPI_STYLE_PS ? "PlayStation" : style == IPI_STYLE_XBOX ? "Xbox" : "keyboard");
}

} // namespace

extern "C" void iss_prompt_icons_open(int fd, const char* host_path) {
    std::lock_guard guard(mutex);
    auto& p = packages[host_path];
    if (!p.found && p.original.empty()) {
        FILE* f = std::fopen(host_path, "rb");
        std::vector<uint8_t> bytes;
        if (f) {
            std::fseek(f, 0, SEEK_END);
            const long n = std::ftell(f);
            std::fseek(f, 0, SEEK_SET);
            if (n > 0) {
                bytes.resize(size_t(n));
                if (std::fread(bytes.data(), 1, bytes.size(), f) != bytes.size()) bytes.clear();
            }
            std::fclose(f);
        }
        p.found = !bytes.empty() && ipi_locate(bytes.data(), bytes.size(), &p.atlas);
        if (p.found) {
            const size_t n = size_t(p.atlas.width) * p.atlas.height * 4;
            p.original.assign(bytes.begin() + long(p.atlas.pixels), bytes.begin() + long(p.atlas.pixels + n));
            std::printf("Prompts: %s: icon font with %u glyphs, %ux%u atlas\n", host_path, p.atlas.count,
                        p.atlas.width, p.atlas.height);
        } else {
            p.original.assign(1, 0); // looked at, nothing to do
            std::printf("Prompts: %s: no icon font found; prompts unchanged\n", host_path);
        }
    }
    if (p.found) open_files[fd] = host_path;
}

extern "C" void iss_prompt_icons_part(int fd, void* buffer, int64_t offset, int64_t size) {
    if (size <= 0 || offset < 0) return;
    std::lock_guard guard(mutex);
    const auto it = open_files.find(fd);
    if (it == open_files.end()) return;
    Package& p = packages[it->second];
    const uint64_t begin = uint64_t(offset), end = begin + uint64_t(size);
    if (end <= p.atlas.pixels || begin >= p.atlas.pixels + p.original.size()) return;
    const Segment s{begin, end, static_cast<uint8_t*>(buffer)};
    // The game may read the package again into new memory: keep only the newest copy of a range.
    std::erase_if(p.segments, [&](const Segment& o) { return o.file_begin < end && begin < o.file_end; });
    p.segments.push_back(s);
    Apply(p, s, applied_style); // every live segment holds applied_style
    Switch(Effective());
}

extern "C" void iss_prompt_icons_close(int fd) {
    std::lock_guard guard(mutex);
    open_files.erase(fd);
}

extern "C" void iss_prompt_icons_device(int style) {
    wanted_style.store(style, std::memory_order_relaxed);
    style = Effective();
    if (style == applied_style_seen.load(std::memory_order_relaxed)) return;
    std::lock_guard guard(mutex);
    Switch(style);
}
