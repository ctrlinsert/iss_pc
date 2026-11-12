// Button prompt icons (src/iss_prompt_icons.h, gpu/shim/tlg_prompt_icons.cpp) on a synthetic
// language text package: no game data. Build: build.sh --test.
#include "../src/iss_prompt_icons.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <vector>

extern "C" const char* runtime_pad_key_label(uint32_t button) {
    return button == 0x4000 ? "SPACE" : button == 0x2000 ? "SHIFT" : nullptr; // cross, circle
}

namespace {
constexpr uint32_t kDoff = 0x100, kW = 32, kH = 32, kPixels = 0x400;
constexpr uint64_t kName = 0x1122334455667788ull;

void Put32(std::vector<uint8_t>& b, size_t at, uint32_t v) { std::memcpy(&b[at], &v, 4); }
void Put64(std::vector<uint8_t>& b, size_t at, uint64_t v) { std::memcpy(&b[at], &v, 8); }
void Put16(std::vector<uint8_t>& b, size_t at, uint16_t v) { std::memcpy(&b[at], &v, 2); }

// KCAP header, a font of 4 glyphs (U+F080..U+F083, 8x8 cells in the top row) and its 32x32 atlas.
std::vector<uint8_t> Package() {
    std::vector<uint8_t> b(kDoff + kPixels + kW * kH * 4, 0);
    std::memcpy(b.data(), "KCAP", 4);
    Put32(b, 0x28, kDoff);
    Put32(b, 0x2c, uint32_t(b.size() - kDoff));
    const size_t font = kDoff + 0x00, table = 0x40, texture = kDoff + 0x200;
    Put64(b, font + 0x18, kName);
    Put64(b, font + 0x28, table);
    Put32(b, font + 0x30, 4);
    for (uint16_t i = 0; i < 4; ++i) {
        const size_t g = kDoff + table + i * 32;
        Put16(b, g, uint16_t(0xF080 + i));
        Put16(b, g + 2, uint16_t(i * 8));
        Put16(b, g + 4, 0);
        Put16(b, g + 6, 8);
        Put16(b, g + 8, 8);
    }
    Put32(b, texture + 0x08, kPixels);
    Put32(b, texture + 0x0c, 0xAu << 20);
    Put32(b, texture + 0x10, (kW - 1) | (kH - 1) << 14);
    Put32(b, texture + 0x14, 13u << 20);
    Put64(b, texture + 0x28, kName);
    for (size_t i = 0; i < kW * kH * 4; ++i) b[kDoff + kPixels + i] = uint8_t(i * 7 + 3);
    return b;
}
} // namespace

int main() {
    assert(ipi_is_text_package("/app0/art/cache/lang_english_text.xpps"));
    assert(!ipi_is_text_package("/app0/art/cache/lang_english_text_p.xpps"));
    assert(!ipi_is_text_package("/app0/art/cache/lang_english_audio.xpps"));
    assert(!ipi_is_text_package("/app0/art/cache/core_common.sprig.xpps"));

    // Tiling: every texel has its own 4 bytes; row 0 (top) is the last tile row (bottom-up).
    std::set<size_t> seen;
    for (uint32_t y = 0; y < kH; ++y)
        for (uint32_t x = 0; x < kW; ++x) {
            const size_t at = ipi_texel_offset(x, y, kW, kH);
            assert(at % 4 == 0 && at < kW * kH * 4);
            assert(seen.insert(at).second);
        }
    assert(ipi_texel_offset(0, kH - 1, kW, kH) == 0);
    assert(ipi_texel_offset(1, kH - 1, kW, kH) == 4);
    assert(ipi_texel_offset(0, kH - 2, kW, kH) == 8);

    std::vector<uint8_t> file = Package();
    IpiAtlas atlas;
    assert(ipi_locate(file.data(), file.size(), &atlas));
    assert(atlas.pixels == kDoff + kPixels && atlas.width == kW && atlas.height == kH && atlas.count == 4);
    assert(atlas.glyphs[3].cp == 0xF083 && atlas.glyphs[3].x == 24 && atlas.glyphs[3].w == 8);
    std::vector<uint8_t> broken = file;
    Put32(broken, kDoff + 0x200 + 0x14, 14u << 20); // another tile mode: not the icon atlas
    assert(!ipi_locate(broken.data(), broken.size(), &atlas));

    char path[] = "/tmp/tlg-prompt-icons-XXXXXX";
    const int handle = mkstemp(path);
    assert(handle >= 0);
    FILE* f = fdopen(handle, "wb");
    assert(std::fwrite(file.data(), 1, file.size(), f) == file.size());
    std::fclose(f);

    // The game reads the package in two parts that split the atlas.
    std::vector<uint8_t> guest(file.size(), 0);
    const size_t split = kDoff + kPixels + 1000;
    iss_prompt_icons_open(7, path);
    std::memcpy(guest.data(), file.data(), split);
    iss_prompt_icons_part(7, guest.data(), 0, int64_t(split));
    std::memcpy(guest.data() + split, file.data() + split, file.size() - split);
    iss_prompt_icons_part(7, guest.data() + split, int64_t(split), int64_t(file.size() - split));
    iss_prompt_icons_close(7);
    assert(guest == file); // PlayStation (the default): the game's icons

    const auto texel = [&](const std::vector<uint8_t>& b, uint32_t x, uint32_t y) {
        return b.data() + kDoff + kPixels + ipi_texel_offset(x, y, kW, kH);
    };
    iss_prompt_icons_device(IPI_STYLE_XBOX);
    assert(std::memcmp(guest.data(), file.data(), kDoff + kPixels) == 0); // outside the atlas: untouched
    for (uint32_t x = 0; x < kW; ++x)                                    // rows below the cells: untouched
        for (uint32_t y = 8; y < kH; ++y) assert(std::memcmp(texel(guest, x, y), texel(file, x, y), 4) == 0);
    assert(texel(guest, 4, 4)[0] == 255 && texel(guest, 4, 4)[3] > 0);  // a white disc in each cell
    assert(texel(guest, 0, 0)[3] == 0);                                   // its corner is transparent
    std::vector<uint8_t> xbox = guest;

    iss_prompt_icons_device(IPI_STYLE_KEYBOARD);
    assert(guest != xbox);
    iss_prompt_icons_device(IPI_STYLE_PS);
    assert(guest == file);

    // Memory the game reused for something else is left alone.
    guest[kDoff + kPixels + 10] ^= 0xff;
    const std::vector<uint8_t> reused = guest;
    iss_prompt_icons_device(IPI_STYLE_XBOX);
    assert(std::memcmp(guest.data(), reused.data(), split) == 0);
    assert(std::memcmp(guest.data() + split, reused.data() + split, guest.size() - split) != 0);

    std::remove(path);
    std::puts("Prompt icons: locate, tiling, Xbox/keyboard redraw and restore PASS");
}
