// SPDX-License-Identifier: GPL-2.0-or-later
// Native renderer, stage 2: the parts of a native pass that do not touch Vulkan (markers, the
// per-pass switch, the check that a pass's commands may be dropped, size math). Header only, so
// port/tests builds it without the renderer.
//
// How a native pass runs (native_render.h has the renderer side):
//
//   1. A game hook at the start of one of the engine's render passes writes a BEGIN marker into the
//      draw command buffer, and one at its end writes the END marker. Both are PM4 NOPs, so a
//      renderer that does not know them, or a pass that is switched off, is unaffected.
//   2. The command processor, at BEGIN, scans the commands up to the matching END (ScanSpan). Only
//      when every packet between is one whose effect is either register state or the pass's own
//      draws and dispatches does the native pass run.
//   3. The native pass records its own Vulkan work (our shaders, sizes from the images it is
//      given). If it declines (an image it cannot resolve, a format it does not handle), the
//      command processor interprets the span as before: that is the fallback.
//   4. If it ran, the command processor still interprets every packet of the span, so the
//      registers, render target hints and everything after the span see exactly the state the
//      engine left, but it drops the span's draws and dispatches (suppression), then goes on
//      after END.
//
// Nothing here is derived from game code: pass ids are our own numbering, and which engine
// routine a pass brackets lives in the game hooks (port/src), checked against the eboot's bytes.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

namespace iss::native_render {
using u32 = std::uint32_t;
using u64 = std::uint64_t;

// ------------------------------------------------------------------------------------- markers
/// 'I', 'S', 'N' and the kind in the low byte. Distinct from the other port markers: 'ISS'
/// (engine), 'ISG' (G-buffer), 'ISH' (shadow), 'ITT' (temporal upscaler).
inline constexpr u32 kMagic = 0x49534E00u;
inline constexpr u32 kMagicMask = 0xFFFFFF00u;
enum Kind : u32 { kBegin = 1, kEnd = 2 };
/// Body dwords: magic|kind, pass, sequence, then up to kMaxPayload words the hook fills
/// (the guest addresses and sizes of the pass's images, as each pass defines them).
inline constexpr u32 kHeaderWords = 3;
inline constexpr u32 kMaxPayload = 29;

struct Marker {
    u32 kind{};
    u32 pass{};
    u32 sequence{};
    const u32* payload{};
    u32 payload_words{};
};

inline constexpr u32 Type3(u32 opcode, u32 body_dwords) {
    return (3u << 30) | (((body_dwords - 1) & 0x3FFFu) << 16) | ((opcode & 0xFFu) << 8);
}

/// `packet` is a PM4 packet header; `available` the dwords from it to the end of the buffer.
inline bool ParseMarker(const u32* packet, std::size_t available, Marker& out) {
    if (!packet || available < 1 + kHeaderWords) return false;
    const u32 header = packet[0];
    if ((header >> 30) != 3 || ((header >> 8) & 0xFF) != 0x10) return false;
    const u32 body = ((header >> 16) & 0x3FFF) + 1;
    if (body < kHeaderWords || body > kHeaderWords + kMaxPayload || body + 1 > available)
        return false;
    if ((packet[1] & kMagicMask) != kMagic) return false;
    const u32 kind = packet[1] & 0xFF;
    if (kind != kBegin && kind != kEnd) return false;
    out = {kind, packet[2], packet[3], packet + 1 + kHeaderWords, body - kHeaderWords};
    return true;
}

/// Writes a marker (header included) into `out`; returns the dwords written, 0 if it does not fit.
inline u32 WriteMarker(u32* out, std::size_t capacity, u32 kind, u32 pass, u32 sequence,
                       const u32* payload, u32 payload_words) {
    if (payload_words > kMaxPayload) return 0;
    const u32 body = kHeaderWords + payload_words;
    if (capacity < body + 1) return 0;
    out[0] = Type3(0x10, body);
    out[1] = kMagic | kind;
    out[2] = pass;
    out[3] = sequence;
    if (payload_words) std::memcpy(out + 4, payload, payload_words * sizeof(u32));
    return body + 1;
}

// -------------------------------------------------------------------------------------- passes
/// Our numbering, stable: a number is never reused, so hooks, logs and settings agree across
/// versions. A pass gets its number when its game hook is written.
enum class Pass : u32 {
    None = 0,
    /// Diagnostic: bracketed spans are scanned and reported, nothing native runs and nothing is
    /// dropped. Lets a new hook be checked before its pass exists.
    Probe = 1,
    /// CFxCopyTexture: a full-screen copy (frame capture). Payload: source T# (8), mode.
    Copy = 2,
    /// CFxFXAA: the anti-aliasing pass in FXAA mode. Payload: source T# (8), has-second, second T# (8).
    Fxaa = 3,
    Count,
};
inline constexpr u32 kPassCount = static_cast<u32>(Pass::Count);
static_assert(kPassCount <= 64, "the switch mask is 64 bits");

inline constexpr std::array<std::string_view, kPassCount> kPassNames = {"none", "probe", "copy",
                                                                        "fxaa"};

inline std::string_view PassName(u32 pass) {
    return pass < kPassCount ? kPassNames[pass] : std::string_view{"unknown"};
}

// ------------------------------------------------------------------------------------- switch
/// ISS_NATIVE_RENDER: which passes run natively. Empty, "0", "off" or "none": none (the default).
/// "all": every pass. Otherwise names separated by commas or spaces; "-name" removes one (so
/// "all,-bloom" is every pass but bloom). Unknown names are counted, not fatal.
struct Switches {
    u64 mask{};
    u32 unknown{};
    bool Enabled(Pass pass) const {
        const u32 i = static_cast<u32>(pass);
        return i != 0 && i < kPassCount && ((mask >> i) & 1);
    }
};

inline Switches ParseSwitches(const char* text) {
    Switches out;
    if (!text) return out;
    const std::string_view all{text};
    const u64 every = (kPassCount >= 64 ? ~0ull : ((1ull << kPassCount) - 1)) & ~1ull;
    std::size_t i = 0;
    while (i < all.size()) {
        while (i < all.size() && (all[i] == ',' || all[i] == ' ' || all[i] == '\t')) ++i;
        std::size_t j = i;
        while (j < all.size() && all[j] != ',' && all[j] != ' ' && all[j] != '\t') ++j;
        std::string_view word = all.substr(i, j - i);
        i = j;
        if (word.empty()) continue;
        const bool remove = word.front() == '-';
        if (remove) word.remove_prefix(1);
        if (word == "0" || word == "off" || word == "none") {
            out.mask = 0;
            continue;
        }
        u64 bits = 0;
        if (word == "all" || word == "1" || word == "on") {
            bits = every;
        } else {
            for (u32 p = 1; p < kPassCount; ++p)
                if (word == kPassNames[p]) bits = 1ull << p;
        }
        if (!bits) {
            ++out.unknown;
            continue;
        }
        out.mask = remove ? (out.mask & ~bits) : (out.mask | bits);
    }
    return out;
}

// --------------------------------------------------------------------------------- span check
enum class Scan : u32 {
    Ok,        ///< END found; every packet between may be interpreted with its work dropped
    NoEnd,     ///< no matching END before the end of the buffer (the hook pair is broken)
    Unsafe,    ///< a packet whose effect depends on the dropped work (bad_opcode says which)
    Malformed, ///< a packet runs past the buffer
};

inline constexpr std::array<std::string_view, 4> kScanNames = {"ok", "no-end", "unsafe",
                                                               "malformed"};

struct SpanResult {
    Scan verdict{Scan::NoEnd};
    const u32* end_marker{}; ///< the END marker's header (Ok)
    const u32* resume{};     ///< the packet after END (Ok)
    u32 bad_opcode{};        ///< Unsafe: the opcode (a NOP: 0x10, bad_word its first data word)
    u32 bad_word{};
    u32 draws{}, dispatches{}, packets{};
    /// Context register writes inside the span to the colour target registers (CB_COLOR0..7).
    /// A pass that takes its destination from the registers at BEGIN declines when it is not 0:
    /// the engine changed its target inside the span.
    u32 target_writes{};
};

/// The colour target registers (CB_COLOR0_BASE .. CB_COLOR7_*), context register offsets.
inline constexpr u32 kColorTargetFirst = 0x318;
inline constexpr u32 kColorTargetEnd = 0x390;

/// Whether a NOP with this first data word may sit inside a dropped span. Plain padding, the
/// CB/DB size hints, the gnmdriver sharp/debug hints and embedded data may. Any other marker (flip, engine,
/// G-buffer, shadow, temporal, another native BEGIN) may not: something acts on it.
inline bool QuietNop(u32 body_dwords, u32 first_word) {
    if (body_dwords == 0 || first_word == 0) return true;
    if (first_word >= 0x68750001u && first_word <= 0x6875000Eu) return true; // debug, sharp hints
    // Data the engine placed in the command buffer (constants its shaders read): the NOP's first
    // word is 0x68750000 | (alignment log2 << 12), then padding and the data. Nothing acts on it.
    if ((first_word & 0xFFFF0FFFu) == 0x68750000u) return true;
    return false;
}

/// From `p` (the packet after the BEGIN marker) to `limit`, the END of `begin`'s pass and
/// sequence. Packets allowed between: register writes, index and instance state, direct and
/// indirect draws and dispatches (dropped), quiet NOPs, CONTEXT_CONTROL, CLEAR_STATE, SET_BASE and
/// EVENT_WRITE of cache and pipeline events. Anything that writes or waits on memory, chains to
/// another buffer, or counts samples is Unsafe: dropping the work around it would change what
/// it sees.
inline SpanResult ScanSpan(const u32* p, const u32* limit, const Marker& begin) {
    SpanResult r;
    while (p < limit) {
        const u32 header = *p;
        const u32 type = header >> 30;
        if (type == 2) { // type-2 filler
            ++p;
            ++r.packets;
            continue;
        }
        if (type != 3) {
            r.verdict = Scan::Unsafe;
            r.bad_opcode = 0x100 | type;
            return r;
        }
        const u32 body = ((header >> 16) & 0x3FFF) + 1;
        const u32 opcode = (header >> 8) & 0xFF;
        if (std::size_t(limit - p) < std::size_t(body) + 1) {
            r.verdict = Scan::Malformed;
            r.bad_opcode = opcode;
            return r;
        }
        ++r.packets;
        bool ok = false;
        switch (opcode) {
        case 0x10: { // NOP
            Marker m;
            if (ParseMarker(p, std::size_t(limit - p), m)) {
                if (m.kind == kEnd && m.pass == begin.pass && m.sequence == begin.sequence) {
                    r.verdict = Scan::Ok;
                    r.end_marker = p;
                    r.resume = p + 1 + body;
                    return r;
                }
                ok = false; // nested or mismatched native marker
            } else {
                ok = QuietNop(body, p[1]);
            }
            if (!ok) r.bad_word = p[1];
            break;
        }
        case 0x69: { // SET_CONTEXT_REG
            const u32 first = p[1] & 0xFFFF;
            const u32 last = first + (body - 1); // one past
            if (first < kColorTargetEnd && last > kColorTargetFirst) ++r.target_writes;
            ok = true;
            break;
        }
        case 0x68: // SET_CONFIG_REG
        case 0x76: // SET_SH_REG
        case 0x79: // SET_UCONFIG_REG
        case 0x2A: // INDEX_TYPE
        case 0x2F: // NUM_INSTANCES
        case 0x26: // INDEX_BASE
        case 0x13: // INDEX_BUFFER_SIZE
        case 0x11: // SET_BASE
        case 0x28: // CONTEXT_CONTROL
        case 0x12: // CLEAR_STATE
            ok = true;
            break;
        case 0x27: // DRAW_INDEX_2
        case 0x2D: // DRAW_INDEX_AUTO
        case 0x35: // DRAW_INDEX_OFFSET_2
        case 0x24: // DRAW_INDIRECT
        case 0x25: // DRAW_INDEX_INDIRECT
            ok = true;
            ++r.draws;
            break;
        case 0x15: // DISPATCH_DIRECT
        case 0x16: // DISPATCH_INDIRECT
            ok = true;
            ++r.dispatches;
            break;
        case 0x46: { // EVENT_WRITE: cache flushes and partial flushes only, no sample counts
            const u32 index = (p[1] >> 8) & 0xF;
            ok = index == 0 || index == 4 || index == 7;
            r.bad_word = p[1];
            break;
        }
        default:
            ok = false;
            break;
        }
        if (!ok) {
            r.verdict = Scan::Unsafe;
            r.bad_opcode = opcode;
            return r;
        }
        p += 1 + body;
    }
    r.verdict = Scan::NoEnd;
    return r;
}

// ----------------------------------------------------------------------- image descriptors
/// The fields of a texture descriptor (T#, 8 dwords, as the game hooks copy it into a payload)
/// that a native pass needs to find the image in the texture cache.
struct Texture {
    u64 address{};     ///< guest address of the image's first byte
    u32 width{}, height{};
    u32 data_format{}, num_format{};
    u32 type{};        ///< 8 = 1D .. 13 = 2D array, 14/15 multisampled
    bool Valid() const { return address != 0 && width != 0 && height != 0; }
};

inline Texture DecodeTexture(const u32* t) {
    Texture x;
    x.address = (u64(t[0]) | (u64(t[1] & 0x3F) << 32)) << 8;
    x.data_format = (t[1] >> 20) & 0x3F;
    x.num_format = (t[1] >> 26) & 0xF;
    x.width = (t[2] & 0x3FFF) + 1;
    x.height = ((t[2] >> 14) & 0x3FFF) + 1;
    x.type = (t[3] >> 28) & 0xF;
    return x;
}

// ------------------------------------------------------------------------------- size helpers
/// Groups to cover `size` items with groups of `group` (rounded up; 0 for an empty size).
inline constexpr u32 Groups(u32 size, u32 group) {
    return group ? (size + group - 1) / group : 0;
}

/// A rectangle of an image in pixels.
struct Extent {
    u32 width{}, height{};
    constexpr bool Empty() const { return width == 0 || height == 0; }
};

/// The size of mip/downsample level `level` of `base` (each level halves, rounded down, at least
/// 1), so chains (bloom, depth of field) follow the real target size, not a fixed 1920x1080.
inline constexpr Extent Level(Extent base, u32 level) {
    Extent e = base;
    for (u32 i = 0; i < level; ++i) {
        e.width = e.width > 1 ? e.width / 2 : 1;
        e.height = e.height > 1 ? e.height / 2 : 1;
    }
    return e;
}

/// 1 / size, for shaders that address texels: what a native pass passes in its push constants
/// instead of a constant written for one resolution.
struct TexelSize {
    float x{}, y{};
};
inline TexelSize InverseSize(Extent e) {
    return {e.width ? 1.0f / float(e.width) : 0.0f, e.height ? 1.0f / float(e.height) : 0.0f};
}

/// Width over height, the aspect a pass uses for shapes that must stay round (vignette, radial
/// blur) at any output: 16:10, 16:9, 21:9, 32:9.
inline float Aspect(Extent e) {
    return e.height ? float(e.width) / float(e.height) : 1.0f;
}

} // namespace iss::native_render
