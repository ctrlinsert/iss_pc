// SPDX-License-Identifier: GPL-2.0-or-later
// Diagnostic: the game's per-frame particle budget (ISS_PARTICLE_BUDGET_LOG=<csv path>).
//
// The game's GPU particle system hands each emitter a range of particle slots from a running
// total, and an emitter updated once that total passes 0x1ffff is skipped for the frame (it draws
// nothing). A thread samples the total continuously and writes, per guest frame, the highest value
// seen and how many samples were over the limit, so a run shows whether flickering particles
// coincide with an exhausted budget. Reads only the game's own CPU data; changes nothing.
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
constexpr u64 kTotalOffset = 0x7d3eb10 + 0x68; ///< the running slot total (1.07 eboot)
constexpr u32 kLimit = 0x1ffff;
}

void Start(u64 image_base) {
    const char* path = std::getenv("ISS_PARTICLE_BUDGET_LOG");
    if (!path || !*path || !image_base) return;
    std::FILE* file = std::fopen(path, "w");
    if (!file) return;
    std::fputs("frame,max_total,last_total,samples,over_limit_samples\n", file);
    std::fprintf(stderr, "PARTICLE_BUDGET log=%s limit=0x%x\n", path, kLimit);
    std::thread([file, total = reinterpret_cast<const volatile u32*>(image_base + kTotalOffset)] {
        u64 frame = Common::Perf::GuestFrameCounter().load(std::memory_order_relaxed);
        u32 max_total = 0, last = 0;
        u64 samples = 0, over = 0, lines = 0;
        while (lines < 200000) {
            const u32 v = *total;
            const u64 now = Common::Perf::GuestFrameCounter().load(std::memory_order_relaxed);
            if (now != frame) {
                std::fprintf(file, "%llu,%u,%u,%llu,%llu\n", (unsigned long long)frame, max_total,
                             last, (unsigned long long)samples, (unsigned long long)over);
                if ((++lines & 255) == 0) std::fflush(file);
                frame = now;
                max_total = 0;
                samples = over = 0;
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
