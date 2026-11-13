// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// Tiled lighting planes (src/iss_native_light_tiles.h): when the port has grown the game's
// per-tile plane block, the lighting compute shader must read the row table at its new offset.
// The shader computes the row-plane offset as wgID.y*16 + 3504 with one SOP2 literal
// (s_add_u32 s20, vcc_hi, 0xdb0), right after the column one (s_add_u32 s0, vcc_lo, 0x120).
// Only that literal changes; the program hash, and so every cache key, stays the original's,
// and the binary version (vk_pipeline_serialization.cpp) records the row offset.
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>
#include "common/types.h"

extern "C" unsigned iss_light_tiles_row_base(void);

namespace iss::light_tiles {
constexpr u32 kShaderHash = 0xce1675e0u; // low 32 bits of the lighting pass's program hash
constexpr u32 kRetailRowBase = 0xdb0u;

inline u32 RowBase() {
    return iss_light_tiles_row_base();
}

inline bool Applies(u64 pgm_hash) {
    return u32(pgm_hash) == kShaderHash && RowBase() != kRetailRowBase;
}

/// `code` itself, or a copy in `storage` with the row-table literal moved. The copy is made only
/// when the exact four-dword sequence is found once; anything else leaves the shader untouched.
inline std::span<const u32> Apply(u64 pgm_hash, std::span<const u32> code, std::vector<u32>& storage) {
    if (!Applies(pgm_hash)) return code;
    static constexpr u32 kPattern[4] = {0x8100ff6au, 0x00000120u, 0x8114ff6bu, kRetailRowBase};
    size_t found = SIZE_MAX;
    for (size_t i = 0; i + 4 <= code.size(); ++i) {
        if (std::memcmp(code.data() + i, kPattern, sizeof(kPattern)) != 0) continue;
        if (found != SIZE_MAX) return code; // ambiguous: leave it alone
        found = i;
    }
    if (found == SIZE_MAX) return code;
    storage.assign(code.begin(), code.end());
    storage[found + 3] = RowBase();
    return storage;
}
} // namespace iss::light_tiles
