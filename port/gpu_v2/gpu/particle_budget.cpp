// SPDX-License-Identifier: GPL-2.0-or-later
// Diagnostic: the game's per-frame particle budget (ISS_PARTICLE_BUDGET_LOG=<csv path>).
//
// The game's GPU particle system hands each emitter a range of particle slots from a running
// total, and an emitter updated once that total passes 0x1ffff is skipped for the frame (it draws
// nothing). A thread samples the total continuously and writes, per guest frame, the highest value
// seen and how many samples were over the limit, so a run shows whether flickering particles
// coincide with an exhausted budget. Reads only the game's own CPU data; changes nothing.
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <thread>

#include "common/perf_stats.h"
#include "common/types.h"

#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#endif

namespace iss::particle_budget {
namespace {
constexpr u64 kG = 0x7d3eb10;                ///< the particle system's globals (1.07 eboot)
constexpr u64 kTotalOffset = kG + 0x68;      ///< the running slot total
constexpr u32 kLimit = 0x1ffff;
// The per-frame result arena: two halves of kHalf bytes at [G], the half in use [G+0x28], the
// bump cursor [G+0x10] and the end [G+0x18]. An emitter whose 0x30-byte block does not fit is
// skipped for the frame.
constexpr u64 kHalf = 0x1e00000;
}

void Start(u64 image_base) {
    const char* path = std::getenv("ISS_PARTICLE_BUDGET_LOG");
    if (!path || !*path || !image_base) return;
    std::FILE* file = std::fopen(path, "w");
    if (!file) return;
    std::fputs("frame,max_total,last_total,samples,over_limit_samples,arena_peak_used,arena_min_free\n", file);
    std::fprintf(stderr, "PARTICLE_BUDGET log=%s limit=0x%x\n", path, kLimit);
    std::thread([file, total = reinterpret_cast<const volatile u32*>(image_base + kTotalOffset),
                 g = reinterpret_cast<const volatile u64*>(image_base + kG)] {
        u64 frame = Common::Perf::GuestFrameCounter().load(std::memory_order_relaxed);
        u32 max_total = 0, last = 0;
        u64 samples = 0, over = 0, lines = 0;
        u64 peak_used = 0, min_free = ~u64{0};
        while (lines < 200000) {
            const u32 v = *total;
            const u64 base = g[0], cursor = g[2], end = g[3];
            const u32 half = *reinterpret_cast<const volatile u32*>(g + 5);
            const u64 start = base + u64(half & 1) * kHalf;
            if (base && cursor >= start && cursor <= start + kHalf) {
                peak_used = std::max(peak_used, cursor - start);
                if (end >= cursor) min_free = std::min(min_free, end - cursor);
            }
            const u64 now = Common::Perf::GuestFrameCounter().load(std::memory_order_relaxed);
            if (now != frame) {
                std::fprintf(file, "%llu,%u,%u,%llu,%llu,%llu,%lld\n", (unsigned long long)frame,
                             max_total, last, (unsigned long long)samples,
                             (unsigned long long)over, (unsigned long long)peak_used,
                             min_free == ~u64{0} ? -1LL : (long long)min_free);
                if ((++lines & 255) == 0) std::fflush(file);
                frame = now;
                max_total = 0;
                samples = over = 0;
                peak_used = 0;
                min_free = ~u64{0};
            }
            if (v > max_total) max_total = v;
            last = v;
            ++samples;
            if (v > kLimit) ++over;
#if defined(__x86_64__) || defined(_M_X64)
            for (int i = 0; i < 64; ++i) _mm_pause();
#endif
        }
        std::fclose(file);
    }).detach();
}

} // namespace iss::particle_budget
