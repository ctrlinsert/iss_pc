// SPDX-License-Identifier: GPL-2.0-or-later
// The pure parts of the native renderer (gpu_v2/gpu/native_render_core.h): the BEGIN/END markers,
// the ISS_NATIVE_RENDER switch, the check that a pass's span may be dropped, and the size math every
// native pass uses so it works at any output size and aspect.
#include "../gpu_v2/gpu/native_render_core.h"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace iss::native_render;

static int failures = 0;
#define CHECK(condition)                                                                       \
    do {                                                                                       \
        if (!(condition)) {                                                                    \
            std::printf("FAIL line %d: %s\n", __LINE__, #condition);                           \
            ++failures;                                                                        \
        }                                                                                      \
    } while (0)

namespace {

constexpr u32 kProbe = static_cast<u32>(Pass::Probe);

// A command buffer builder for the span tests.
struct Dcb {
    std::vector<u32> words;
    void Packet(u32 opcode, std::initializer_list<u32> body) {
        words.push_back(Type3(opcode, u32(body.size())));
        words.insert(words.end(), body);
    }
    void Mark(u32 kind, u32 pass, u32 seq, std::initializer_list<u32> payload = {}) {
        std::vector<u32> p(payload);
        u32 tmp[1 + kHeaderWords + kMaxPayload];
        const u32 n = WriteMarker(tmp, std::size(tmp), kind, pass, seq, p.data(), u32(p.size()));
        words.insert(words.end(), tmp, tmp + n);
    }
    void Draw() { Packet(0x27, {0xFFFF, 0x1000, 0, 3, 0}); }     // DRAW_INDEX_2
    void DrawAuto() { Packet(0x2D, {3, 2}); }                     // DRAW_INDEX_AUTO
    void Dispatch() { Packet(0x15, {120, 68, 1, 1}); }            // DISPATCH_DIRECT
    void SetContext(u32 reg, u32 value) { Packet(0x69, {reg, value}); }
    void SetSh(u32 reg, u32 value) { Packet(0x76, {reg, value}); }
    const u32* At(std::size_t i) const { return words.data() + i; }
    const u32* End() const { return words.data() + words.size(); }
};

SpanResult ScanFrom(const Dcb& dcb, std::size_t begin_at) {
    Marker m;
    if (!ParseMarker(dcb.At(begin_at), dcb.words.size() - begin_at, m)) return {};
    const u32 body = ((dcb.words[begin_at] >> 16) & 0x3FFF) + 1;
    return ScanSpan(dcb.At(begin_at + 1 + body), dcb.End(), m);
}

void TestMarkers() {
    const u32 payload[3] = {0x12340000u, 0x1, 7680};
    u32 words[16]{};
    const u32 n = WriteMarker(words, 16, kBegin, kProbe, 42, payload, 3);
    CHECK(n == 1 + kHeaderWords + 3);
    CHECK(words[0] == Type3(0x10, kHeaderWords + 3));
    Marker m;
    CHECK(ParseMarker(words, n, m));
    CHECK(m.kind == kBegin && m.pass == kProbe && m.sequence == 42);
    CHECK(m.payload_words == 3 && m.payload[2] == 7680);
    // Too short for its own body, wrong magic, wrong kind, not a NOP: rejected.
    CHECK(!ParseMarker(words, n - 1, m));
    words[1] = 0x49535301u; // the engine marker 'ISS'
    CHECK(!ParseMarker(words, n, m));
    words[1] = kMagic | 7;
    CHECK(!ParseMarker(words, n, m));
    words[1] = kMagic | kEnd;
    words[0] = Type3(0x76, kHeaderWords + 3);
    CHECK(!ParseMarker(words, n, m));
    // Payload larger than the limit is refused when writing.
    std::vector<u32> big(kMaxPayload + 1);
    u32 room[64];
    CHECK(WriteMarker(room, 64, kBegin, kProbe, 0, big.data(), u32(big.size())) == 0);
    CHECK(WriteMarker(room, 3, kBegin, kProbe, 0, nullptr, 0) == 0);
}

void TestSwitches() {
    CHECK(ParseSwitches(nullptr).mask == 0);
    CHECK(ParseSwitches("").mask == 0);
    CHECK(ParseSwitches("0").mask == 0);
    CHECK(ParseSwitches("off").mask == 0);
    const auto all = ParseSwitches("all");
    CHECK(all.Enabled(Pass::Probe));
    CHECK(!all.Enabled(Pass::None)); // bit 0 is never a pass
    CHECK(ParseSwitches("probe").Enabled(Pass::Probe));
    CHECK(!ParseSwitches("all,-probe").Enabled(Pass::Probe));
    CHECK(!ParseSwitches("probe off").Enabled(Pass::Probe));
    const auto unknown = ParseSwitches(" bloom , probe,,tonemapx");
    CHECK(unknown.Enabled(Pass::Probe));
    CHECK(unknown.unknown == 2);
}

void TestSpanOk() {
    Dcb dcb;
    dcb.SetContext(0x100, 1);
    const std::size_t begin = dcb.words.size();
    dcb.Mark(kBegin, kProbe, 5, {0xABC});
    dcb.SetContext(0x318, 0x1234); // a render target register
    dcb.Packet(0x10, {0x68750005u}); // sharp hint NOP
    dcb.Packet(0x10, {0x68754000u, 0, 0, 0x3F400000u, 0x3E2A0000u}); // embedded data, 16-aligned
    dcb.SetSh(0x0C, 0xDEAD);
    dcb.Draw();
    dcb.words.push_back(0x80000000u); // type-2 filler
    dcb.DrawAuto();
    dcb.Packet(0x46, {0x00000416u}); // EVENT_WRITE CS_PARTIAL_FLUSH (index 4)
    dcb.Dispatch();
    const std::size_t end = dcb.words.size();
    dcb.Mark(kEnd, kProbe, 5);
    dcb.Draw(); // after END: not part of the span
    const auto r = ScanFrom(dcb, begin);
    CHECK(r.verdict == Scan::Ok);
    CHECK(r.end_marker == dcb.At(end));
    CHECK(r.resume == dcb.At(end + 1 + kHeaderWords));
    CHECK(r.draws == 2 && r.dispatches == 1);
    CHECK(r.target_writes == 1); // 0x318 is CB_COLOR0_BASE
    CHECK(r.ps_address == 0);
}

void TestPixelShader() {
    Dcb dcb;
    dcb.Mark(kBegin, kProbe, 3);
    dcb.Packet(0x76, {0x08, 0x00123456u, 0x12}); // SPI_SHADER_PGM_LO/HI_PS
    dcb.Draw();
    dcb.Packet(0x76, {0x07, 0, 0x00ABCDEFu});     // a second program, LO only (HI 0)
    dcb.Mark(kEnd, kProbe, 3);
    CHECK(ScanFrom(dcb, 0).ps_address == 0xABCDEF00ull);
    Dcb first;
    first.Mark(kBegin, kProbe, 3);
    first.Packet(0x76, {0x08, 0x00123456u, 0x12});
    first.Mark(kEnd, kProbe, 3);
    CHECK(ScanFrom(first, 0).ps_address == ((0x12ull << 40) | (0x123456ull << 8)));
}

void TestTargetWrites() {
    // Writes below, across the start of, and past the colour target registers.
    Dcb dcb;
    dcb.Mark(kBegin, kProbe, 9);
    dcb.SetContext(0x317, 1);                     // just below
    dcb.Packet(0x69, {0x390, 1, 2});              // just past
    dcb.Draw();
    dcb.Mark(kEnd, kProbe, 9);
    CHECK(ScanFrom(dcb, 0).target_writes == 0);
    Dcb across;
    across.Mark(kBegin, kProbe, 9);
    across.Packet(0x69, {0x316, 1, 2, 3});        // 0x316..0x318: touches CB_COLOR0_BASE
    across.Mark(kEnd, kProbe, 9);
    CHECK(ScanFrom(across, 0).target_writes == 1);
}

void TestTexture() {
    // A 7680x2160 2D image at 0x12_3456_7800, data format 10, number format 9.
    const u64 address = 0x1234567800ull;
    u32 t[8]{};
    t[0] = u32(address >> 8);
    t[1] = u32((address >> 40) & 0x3F) | (10u << 20) | (9u << 26) | (3u << 30);
    t[2] = (7680 - 1) | ((2160 - 1) << 14);
    t[3] = 9u << 28;
    const Texture x = DecodeTexture(t);
    CHECK(x.address == address);
    CHECK(x.width == 7680 && x.height == 2160);
    CHECK(x.data_format == 10 && x.num_format == 9 && x.type == 9);
    CHECK(x.Valid());
    const u32 zero[8]{};
    CHECK(!DecodeTexture(zero).Valid());
}

void TestSpanUnsafe() {
    struct Case {
        const char* what;
        void (*add)(Dcb&);
        u32 opcode;
    };
    const Case cases[] = {
        {"write data", [](Dcb& d) { d.Packet(0x37, {0x500, 0x1000, 0, 1}); }, 0x37},
        {"wait reg mem", [](Dcb& d) { d.Packet(0x3C, {0x13, 0x1000, 0, 1, 1, 4}); }, 0x3C},
        {"dma", [](Dcb& d) { d.Packet(0x50, {0, 0, 0, 0, 0, 64}); }, 0x50},
        {"eop fence", [](Dcb& d) { d.Packet(0x47, {0x528, 0x1000, 0x20000000, 1, 0}); }, 0x47},
        {"indirect buffer", [](Dcb& d) { d.Packet(0x3F, {0x1000, 0, 16}); }, 0x3F},
        {"occlusion count", [](Dcb& d) { d.Packet(0x46, {0x00000101u, 0x1000, 0}); }, 0x46},
        {"flip marker", [](Dcb& d) { d.Packet(0x10, {0x68750777u, 0}); }, 0x10},
        {"flip with label", [](Dcb& d) { d.Packet(0x10, {0x68750778u, 0, 0}); }, 0x10},
        {"shadow marker", [](Dcb& d) { d.Packet(0x10, {0x49534801u, 0, 0}); }, 0x10},
        {"nested begin", [](Dcb& d) { d.Mark(kBegin, kProbe, 6); }, 0x10},
    };
    for (const auto& c : cases) {
        Dcb dcb;
        dcb.Mark(kBegin, kProbe, 5);
        dcb.Draw();
        c.add(dcb);
        dcb.Draw();
        dcb.Mark(kEnd, kProbe, 5);
        const auto r = ScanFrom(dcb, 0);
        if (r.verdict != Scan::Unsafe || r.bad_opcode != c.opcode) {
            std::printf("FAIL unsafe case '%s': verdict %u opcode %02x\n", c.what,
                        unsigned(r.verdict), r.bad_opcode);
            ++failures;
        }
    }
}

void TestKeepGoing() {
    // The probe's scan goes past unsafe packets to END: it counts them, keeps the first, and
    // still sees the draws and the pixel shader after them. The verdict stays Unsafe.
    Dcb dcb;
    dcb.Mark(kBegin, kProbe, 4);
    dcb.Packet(0x50, {0x40000000u, 0, 0, 0x1000, 0, 64}); // DMA_DATA
    dcb.Packet(0x37, {0x500, 0x1000, 0, 1});              // WRITE_DATA
    dcb.Packet(0x76, {0x08, 0x00123456u, 0});
    dcb.Draw();
    dcb.Mark(kEnd, kProbe, 4);
    const auto stop = ScanFrom(dcb, 0);
    CHECK(stop.verdict == Scan::Unsafe && stop.bad_opcode == 0x50 && stop.draws == 0);
    Marker m;
    CHECK(ParseMarker(dcb.At(0), dcb.words.size(), m));
    const u32 body = ((dcb.words[0] >> 16) & 0x3FFF) + 1;
    const auto all = ScanSpan(dcb.At(1 + body), dcb.End(), m, true);
    CHECK(all.verdict == Scan::Unsafe && all.bad_opcode == 0x50 && all.unsafe_packets == 2);
    CHECK(all.bad_packet == dcb.At(1 + body));
    CHECK(all.draws == 1 && all.ps_address == 0x12345600ull && all.end_marker != nullptr);
    // ACQUIRE_MEM (a cache sync) is allowed.
    Dcb sync;
    sync.Mark(kBegin, kProbe, 5);
    sync.Packet(0x58, {0x28000000u, 0xFFFFFFFFu, 0, 0, 0, 10});
    sync.Draw();
    sync.Mark(kEnd, kProbe, 5);
    CHECK(ScanFrom(sync, 0).verdict == Scan::Ok);
}

void TestSpanBroken() {
    // No END, an END of another sequence or pass: no span.
    Dcb a;
    a.Mark(kBegin, kProbe, 1);
    a.Draw();
    CHECK(ScanFrom(a, 0).verdict == Scan::NoEnd);
    a.Mark(kEnd, kProbe, 2);
    a.Draw();
    CHECK(ScanFrom(a, 0).verdict == Scan::Unsafe); // a foreign native marker is not skipped
    // A packet that runs past the buffer.
    Dcb b;
    b.Mark(kBegin, kProbe, 1);
    b.words.push_back(Type3(0x69, 8));
    b.words.push_back(0x100);
    CHECK(ScanFrom(b, 0).verdict == Scan::Malformed);
}

void TestSizes() {
    CHECK(Groups(0, 8) == 0);
    CHECK(Groups(1920, 8) == 240);
    CHECK(Groups(7680, 16) == 480);
    CHECK(Groups(2161, 16) == 136);
    CHECK(Groups(5, 0) == 0);
    // Downsample chains follow the real size: 7680x2160 down 4 levels, odd sizes round down.
    const Extent uw{7680, 2160};
    CHECK(Level(uw, 4).width == 480 && Level(uw, 4).height == 135);
    CHECK(Level(uw, 5).width == 240 && Level(uw, 5).height == 67);
    CHECK(Level({3, 1}, 3).width == 1 && Level({3, 1}, 3).height == 1);
    // Aspect at the shapes the port must support, narrower and wider than 16:9.
    struct Shape {
        Extent e;
        float aspect;
    };
    const Shape shapes[] = {{{1280, 800}, 1.6f},      {{1920, 1080}, 16.0f / 9.0f},
                            {{3440, 1440}, 43.0f / 18.0f}, {{5120, 2160}, 64.0f / 27.0f},
                            {{7680, 2160}, 32.0f / 9.0f}};
    for (const auto& s : shapes) {
        CHECK(std::fabs(Aspect(s.e) - s.aspect) < 1e-5f);
        const auto t = InverseSize(s.e);
        CHECK(std::fabs(t.x * float(s.e.width) - 1.0f) < 1e-6f);
        CHECK(std::fabs(t.y * float(s.e.height) - 1.0f) < 1e-6f);
    }
    CHECK(InverseSize({0, 0}).x == 0.0f);
}

} // namespace

int main() {
    TestMarkers();
    TestSwitches();
    TestSpanOk();
    TestTargetWrites();
    TestPixelShader();
    TestTexture();
    TestSpanUnsafe();
    TestSpanBroken();
    TestKeepGoing();
    TestSizes();
    if (failures) {
        std::printf("native_render: %d failure(s)\n", failures);
        return 1;
    }
    std::printf("native_render: ok\n");
    return 0;
}
