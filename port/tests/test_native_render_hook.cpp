// SPDX-License-Identifier: GPL-2.0-or-later
// The native renderer's game hooks (src/iss_native_render_hook.h): the marker contract shared with the
// renderer, the marker writer on a fake command buffer (with and without growing), the wrappers (the
// game's routine always runs, with its arguments untouched, between BEGIN and END), and the
// signature check and call redirection against the real eboot when ISS_TEST_EBOOT_ELF names the
// unwrapped 1.07 ELF (skipped otherwise: the eboot is not in the repository).
#include "../src/iss_native_render_hook.h"
#include "../gpu_v2/gpu/native_render_core.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace nr = iss::native_render;

static int failures = 0;
#define CHECK(condition)                                                                       \
    do {                                                                                       \
        if (!(condition)) {                                                                    \
            std::printf("FAIL line %d: %s\n", __LINE__, #condition);                           \
            ++failures;                                                                        \
        }                                                                                      \
    } while (0)

static_assert(ISS_NR_MAGIC == nr::kMagic);
static_assert(unsigned(ISS_NR_BEGIN) == unsigned(nr::kBegin) && unsigned(ISS_NR_END) == unsigned(nr::kEnd));
static_assert(ISS_NR_HEADER_WORDS == nr::kHeaderWords && ISS_NR_MAX_PAYLOAD == nr::kMaxPayload);
static_assert(ISS_NR_PASS_COPY == static_cast<unsigned>(nr::Pass::Copy));
static_assert(ISS_NR_PASS_FXAA == static_cast<unsigned>(nr::Pass::Fxaa));
static_assert(ISS_NR_COPY_PAYLOAD <= ISS_NR_MAX_PAYLOAD && ISS_NR_FXAA_PAYLOAD <= ISS_NR_MAX_PAYLOAD);

namespace {

// A render context as the hooks see it: the DrawCommandBuffer writer at +8 with end at writer+8,
// cursor at writer+0x10, grow at writer+0x18, its argument at writer+0x20.
struct FakeContext {
    std::vector<uint32_t> buffer;
    unsigned char ctx[0x40]{};
    int grows = 0;
    explicit FakeContext(size_t dwords) : buffer(dwords + 64) { Reset(dwords); }
    void Reset(size_t dwords) {
        uint32_t* cursor = buffer.data();
        uint32_t* end = buffer.data() + dwords;
        std::memcpy(ctx + 8 + 8, &end, 8);
        std::memcpy(ctx + 8 + 0x10, &cursor, 8);
        void* self = this;
        std::memcpy(ctx + 8 + 0x20, &self, 8);
    }
    uint32_t* Cursor() const {
        uint32_t* c;
        std::memcpy(&c, ctx + 8 + 0x10, 8);
        return c;
    }
    size_t Written() const { return size_t(Cursor() - buffer.data()); }
};

ISS_NATIVE_RENDER_ABI char Grow(void* writer, uint32_t dwords, void* argument) {
    auto* f = static_cast<FakeContext*>(argument);
    ++f->grows;
    uint32_t* end = f->buffer.data() + f->buffer.size();
    uint32_t* cursor;
    std::memcpy(&cursor, static_cast<unsigned char*>(writer) + 0x10, 8);
    if (size_t(end - cursor) < dwords) return 0;
    std::memcpy(static_cast<unsigned char*>(writer) + 8, &end, 8);
    return 1;
}

void TestWriter() {
    FakeContext f(64);
    const uint32_t payload[2] = {7, 8};
    CHECK(iss_nr_write_marker(f.ctx, ISS_NR_BEGIN, ISS_NR_PASS_FXAA, 11, payload, 2) == 1);
    nr::Marker m;
    CHECK(nr::ParseMarker(f.buffer.data(), f.Written(), m));
    CHECK(m.kind == nr::kBegin && m.pass == ISS_NR_PASS_FXAA && m.sequence == 11);
    CHECK(m.payload_words == 2 && m.payload[1] == 8);
    CHECK(f.Written() == 1 + ISS_NR_HEADER_WORDS + 2);
    // No room and no grow function: nothing written.
    FakeContext full(2);
    CHECK(iss_nr_write_marker(full.ctx, ISS_NR_END, 1, 1, nullptr, 0) == 0);
    CHECK(full.Written() == 0);
    // No room, a grow function that makes room: written after growing.
    auto grow = &Grow;
    std::memcpy(full.ctx + 8 + 0x18, &grow, 8);
    CHECK(iss_nr_write_marker(full.ctx, ISS_NR_END, 1, 1, nullptr, 0) == 1);
    CHECK(full.grows == 1 && full.Written() == 4);
    // Payload over the limit, or no context: refused.
    std::vector<uint32_t> big(ISS_NR_MAX_PAYLOAD + 1);
    CHECK(iss_nr_write_marker(f.ctx, ISS_NR_BEGIN, 1, 1, big.data(), uint32_t(big.size())) == 0);
    CHECK(iss_nr_write_marker(nullptr, ISS_NR_BEGIN, 1, 1, nullptr, 0) == 0);
}

// The "game routines" the wrappers call: they record what they got and write one draw packet,
// standing for the commands the engine records between the markers.
struct Seen {
    int calls = 0;
    uint64_t args[8]{};
} seen;

void Emit(unsigned char* ctx) {
    uint32_t* cursor;
    std::memcpy(&cursor, ctx + 8 + 0x10, 8);
    const uint32_t draw[3] = {nr::Type3(0x2D, 2), 3, 2}; // DRAW_INDEX_AUTO
    std::memcpy(cursor, draw, sizeof(draw));
    cursor += 3;
    std::memcpy(ctx + 8 + 0x10, &cursor, 8);
}

ISS_NATIVE_RENDER_ABI void GameCopy(void* effect, unsigned char* ctx, const void* src, uint64_t rcx,
                                    uint64_t r8, uint64_t r9, uint64_t s0, uint64_t s1) {
    ++seen.calls;
    const uint64_t a[8] = {uint64_t(uintptr_t(effect)), uint64_t(uintptr_t(ctx)),
                           uint64_t(uintptr_t(src)), rcx, r8, r9, s0, s1};
    std::memcpy(seen.args, a, sizeof(a));
    Emit(ctx);
}

ISS_NATIVE_RENDER_ABI void GameFxaa(void* effect, unsigned char* ctx, uint64_t rdx, const void* src,
                                    const void* second, uint64_t r9) {
    ++seen.calls;
    const uint64_t a[6] = {uint64_t(uintptr_t(effect)), uint64_t(uintptr_t(ctx)), rdx,
                           uint64_t(uintptr_t(src)), uint64_t(uintptr_t(second)), r9};
    std::memcpy(seen.args, a, sizeof(a));
    Emit(ctx);
}

void TestWrappers() {
    iss_nr_state.copy = &GameCopy;
    iss_nr_state.fxaa = &GameFxaa;
    const uint32_t tsharp[8] = {0x01234567u, 0, (1920u - 1) | ((1080u - 1) << 14), 1, 2, 3, 4, 5};
    const uint32_t second[8] = {9, 9, 9, 9, 9, 9, 9, 9};

    // Inactive: the game's routine runs, nothing else is written.
    iss_nr_state.active = 0;
    {
        FakeContext f(256);
        seen = {};
        iss_nr_fxaa_wrapper(reinterpret_cast<void*>(0x10), f.ctx, 0x20, tsharp, nullptr, 0x30);
        CHECK(seen.calls == 1 && f.Written() == 3);
    }
    iss_nr_state.active = 1;
    {
        FakeContext f(256);
        seen = {};
        iss_nr_fxaa_wrapper(reinterpret_cast<void*>(0x10), f.ctx, 0x20, tsharp, second, 0x30);
        CHECK(seen.calls == 1);
        CHECK(seen.args[0] == 0x10 && seen.args[2] == 0x20 && seen.args[5] == 0x30);
        CHECK(seen.args[3] == uint64_t(uintptr_t(tsharp)) && seen.args[4] == uint64_t(uintptr_t(second)));
        // BEGIN, the game's draw, END; and the renderer's scan accepts the span.
        nr::Marker begin;
        CHECK(nr::ParseMarker(f.buffer.data(), f.Written(), begin));
        CHECK(begin.kind == nr::kBegin && begin.pass == ISS_NR_PASS_FXAA);
        CHECK(begin.payload_words == ISS_NR_FXAA_PAYLOAD);
        CHECK(std::memcmp(begin.payload, tsharp, 32) == 0);
        CHECK(begin.payload[8] == 1 && std::memcmp(begin.payload + 9, second, 32) == 0);
        const uint32_t* after = f.buffer.data() + 1 + ISS_NR_HEADER_WORDS + ISS_NR_FXAA_PAYLOAD;
        const auto span = nr::ScanSpan(after, f.buffer.data() + f.Written(), begin);
        CHECK(span.verdict == nr::Scan::Ok && span.draws == 1);
        CHECK(span.resume == f.buffer.data() + f.Written());
    }
    {
        FakeContext f(256);
        seen = {};
        iss_nr_copy_wrapper(reinterpret_cast<void*>(0x10), f.ctx, tsharp, 0x40, 0x50, 0xFFFFFFFFu, 1, 1);
        CHECK(seen.calls == 1 && seen.args[3] == 0x40 && seen.args[4] == 0x50);
        CHECK(seen.args[5] == 0xFFFFFFFFu && seen.args[6] == 1 && seen.args[7] == 1);
        nr::Marker begin;
        CHECK(nr::ParseMarker(f.buffer.data(), f.Written(), begin));
        CHECK(begin.pass == ISS_NR_PASS_COPY && begin.payload_words == ISS_NR_COPY_PAYLOAD);
        CHECK(begin.payload[8] == 0xFFFFFFFFu);
        const uint32_t* after = f.buffer.data() + 1 + ISS_NR_HEADER_WORDS + ISS_NR_COPY_PAYLOAD;
        CHECK(nr::ScanSpan(after, f.buffer.data() + f.Written(), begin).verdict == nr::Scan::Ok);
    }
    {
        // BEGIN does not fit: the routine still runs, no END is written alone.
        FakeContext f(2);
        seen = {};
        iss_nr_fxaa_wrapper(nullptr, f.ctx, 0, tsharp, nullptr, 0);
        CHECK(seen.calls == 1 && f.Written() == 3);
    }
    iss_nr_state.active = 0;
}

int reports = 0;
void CountReport() { ++reports; }

void TestSitesAndReport() {
    // Calls are counted by the return address: the instruction after the game's call.
    static unsigned char fake_image[1];
    iss_nr_state = {};
    iss_nr_state.image = fake_image;
    iss_nr_note_site(fake_image + iss_nr_sites[1].at + 5);
    iss_nr_note_site(fake_image + iss_nr_sites[1].at + 5);
    iss_nr_note_site(fake_image + iss_nr_sites[0].at + 5);
    iss_nr_note_site(fake_image + iss_nr_sites[0].at + 4); // not a site
    CHECK(iss_nr_state.site_calls[0] == 1 && iss_nr_state.site_calls[1] == 2);
    for (int i = 2; i < ISS_NR_SITE_COUNT; ++i) CHECK(iss_nr_state.site_calls[i] == 0);
    // The report runs at the first call, then every ISS_NR_REPORT_EVERY.
    iss_nr_state.report = &CountReport;
    for (int i = 0; i < 2 * ISS_NR_REPORT_EVERY; ++i) iss_nr_next_sequence();
    CHECK(reports == 3);
    iss_nr_state = {};
}

// The real eboot: segments placed at their virtual addresses, as the loader does.
std::vector<unsigned char> LoadElf(const char* path) {
    std::vector<unsigned char> file;
    FILE* f = std::fopen(path, "rb");
    if (!f) return {};
    std::fseek(f, 0, SEEK_END);
    file.resize(size_t(std::ftell(f)));
    std::fseek(f, 0, SEEK_SET);
    if (std::fread(file.data(), 1, file.size(), f) != file.size()) file.clear();
    std::fclose(f);
    if (file.size() < 64) return {};
    uint64_t phoff;
    uint16_t phnum;
    std::memcpy(&phoff, file.data() + 0x20, 8);
    std::memcpy(&phnum, file.data() + 0x38, 2);
    uint64_t top = 0;
    struct Load {
        uint64_t offset, vaddr, filesz;
    };
    std::vector<Load> loads;
    for (uint16_t i = 0; i < phnum; ++i) {
        const unsigned char* ph = file.data() + phoff + i * 56;
        uint32_t type;
        uint64_t offset, vaddr, filesz, memsz;
        std::memcpy(&type, ph, 4);
        std::memcpy(&offset, ph + 8, 8);
        std::memcpy(&vaddr, ph + 16, 8);
        std::memcpy(&filesz, ph + 32, 8);
        std::memcpy(&memsz, ph + 40, 8);
        if (type != 1 && type != 0x61000010) continue;
        if (vaddr + filesz > 0x10000000 || offset + filesz > file.size()) return {};
        loads.push_back({offset, vaddr, filesz});
        top = std::max(top, vaddr + filesz);
    }
    std::vector<unsigned char> image(static_cast<size_t>(top));
    for (const auto& l : loads) std::memcpy(image.data() + l.vaddr, file.data() + l.offset, l.filesz);
    return image;
}

int TestRealEboot() {
    const char* path = std::getenv("ISS_TEST_EBOOT_ELF");
    if (!path || !*path) return 0;
    auto image = LoadElf(path);
    if (image.empty()) {
        std::printf("FAIL: cannot load %s\n", path);
        ++failures;
        return 1;
    }
    CHECK(iss_nr_signature_match(image.data(), image.size()) == 1);
    // A changed byte at a site or in a routine's prologue: refused, nothing written.
    auto changed = image;
    changed[0x192cfa + 1] ^= 1;
    CHECK(iss_nr_signature_match(changed.data(), changed.size()) == 0);
    changed = image;
    changed[ISS_NR_FXAA_DRAW + 3] ^= 1;
    CHECK(iss_nr_signature_match(changed.data(), changed.size()) == 0);
    // Install with the stubs right after the image (in reach of every call).
    std::vector<unsigned char> mem(image.size() + 4096);
    std::memcpy(mem.data(), image.data(), image.size());
    unsigned char* stubs = mem.data() + image.size();
    CHECK(iss_nr_install_hooks(mem.data(), image.size(), stubs) == 1);
    for (int i = 0; i < ISS_NR_SITE_COUNT; ++i) {
        const auto& s = iss_nr_sites[i];
        CHECK(mem[s.at] == 0xe8);
        int32_t rel;
        std::memcpy(&rel, mem.data() + s.at + 1, 4);
        CHECK(mem.data() + s.at + 5 + rel == stubs + i * ISS_NR_STUB_BYTES);
        uintptr_t to;
        std::memcpy(&to, stubs + i * ISS_NR_STUB_BYTES + 6, 8);
        const uintptr_t expected = s.pass == ISS_NR_PASS_FXAA ? uintptr_t(&iss_nr_fxaa_wrapper)
                                                              : uintptr_t(&iss_nr_copy_wrapper);
        CHECK(to == expected);
        // Only the call's displacement changed.
        CHECK(std::memcmp(mem.data() + s.at + 5, image.data() + s.at + 5, 16) == 0);
    }
    CHECK(iss_nr_state.copy == reinterpret_cast<IssNrCopyFn>(uintptr_t(mem.data() + ISS_NR_COPY_DRAW)));
    iss_nr_state = {};
    return 1;
}

} // namespace

int main() {
    TestWriter();
    TestWrappers();
    TestSitesAndReport();
    const int real = TestRealEboot();
    if (failures) {
        std::printf("native_render_hook: %d failure(s)\n", failures);
        return 1;
    }
    std::printf("native_render_hook: ok%s\n", real ? "" : " (real eboot checks skipped: set ISS_TEST_EBOOT_ELF)");
    return 0;
}
