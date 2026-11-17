// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_render.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "native_fxaa.h"
#include "native_postblend.h"
#include "video_core/amdgpu/regs.h"
#include "video_core/amdgpu/regs_shader.h"

namespace iss::native_render {
namespace {

// The pass implementations, by pass number. A pass without one is declined ("not implemented"),
// so a hook can land before its pass and be checked with ISS_NATIVE_RENDER=probe.
constexpr std::array<RunFn, kPassCount> kRun = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
// Passes that replace their span's draws one at a time (DrawFn): accepted at BEGIN when the span
// is safe, and each draw falls back to the engine's on its own when the pass declines it.
constexpr std::array<DrawFn, kPassCount> kDraw = {nullptr, nullptr, nullptr, &FxaaDraw,
                                                  &PostBlendDraw, nullptr};

struct PassStats {
    u64 begins{}, ran{}, declined{}, blocked{};
    std::array<u64, 4> scans{};
    u64 dropped_draws{}, dropped_dispatches{};
    u64 native_draws{}, fallback_draws{};
    u32 logged{}, draw_logged{};
};

struct State {
    Switches switches;
    bool log{};
    std::array<PassStats, kPassCount> stats{};
    u32 current{}; ///< the pass whose span is being dropped (0: none)
    Marker current_begin{};   ///< its BEGIN marker (the payload lives in the command buffer)
    u32 current_draws{}, current_dispatches{};
    std::chrono::steady_clock::time_point last_report{std::chrono::steady_clock::now()};
    State() {
        switches = ParseSwitches(std::getenv("ISS_NATIVE_RENDER"));
        const char* log_env = std::getenv("ISS_NATIVE_RENDER_LOG");
        log = log_env && log_env[0] == '1';
        if (switches.mask || switches.unknown) {
            std::fprintf(stderr, "NATIVE_RENDER passes:");
            for (u32 p = 1; p < kPassCount; ++p)
                if ((switches.mask >> p) & 1) std::fprintf(stderr, " %.*s", int(kPassNames[p].size()),
                                                           kPassNames[p].data());
            if (switches.unknown) std::fprintf(stderr, " (%u unknown name(s) ignored)", switches.unknown);
            std::fprintf(stderr, "\n");
        }
    }
};

State& GetState() {
    static State state;
    return state;
}

void LogSpan(PassStats& stats, const Marker& begin, const SpanResult& span, const char* outcome) {
    const auto& s = GetState();
    if (!s.log || stats.logged >= 8) return;
    ++stats.logged;
    const auto name = PassName(begin.pass);
    const auto verdict = kScanNames[static_cast<u32>(span.verdict)];
    std::fprintf(stderr,
                 "NATIVE_RENDER span pass=%.*s seq=%u payload=%u packets=%u draws=%u dispatches=%u "
                 "scan=%.*s",
                 int(name.size()), name.data(), begin.sequence, begin.payload_words, span.packets,
                 span.draws, span.dispatches, int(verdict.size()), verdict.data());
    if (span.verdict == Scan::Unsafe) {
        std::fprintf(stderr, " opcode=%02x word=%08x unsafe_packets=%u", span.bad_opcode, span.bad_word,
                     span.unsafe_packets);
        // The first unsafe packet's words, to tell what it does (a copy's source and size...).
        if (span.bad_packet && span.bad_opcode < 0x100) {
            const u32 n = std::min<u32>(((span.bad_packet[0] >> 16) & 0x3FFF) + 2, 8);
            std::fprintf(stderr, " packet=");
            for (u32 i = 0; i < n; ++i) std::fprintf(stderr, "%s%08x", i ? "," : "", span.bad_packet[i]);
        }
    }
    // The engine shader's hash (its OrbShdr block): the name shader dumps use (ps_0x<hash>).
    unsigned long long ps_hash = 0;
    if (span.ps_address) {
        const auto* info = AmdGpu::FindBinaryInfo(reinterpret_cast<const u32*>(span.ps_address));
        if (info) ps_hash = info->shader_hash;
    }
    std::fprintf(stderr, " target_writes=%u ps=%010llx ps_hash=%016llx", span.target_writes,
                 (unsigned long long)span.ps_address, ps_hash);
    // Every draw's and dispatch's shader hash, in order: a multi-stage pass (SMAA) names each stage.
    const auto hash_of = [](u64 address) -> unsigned long long {
        if (!address) return 0;
        const auto* info = AmdGpu::FindBinaryInfo(reinterpret_cast<const u32*>(address));
        return info ? info->shader_hash : 0;
    };
    const u32 nd = std::min(span.draws, SpanResult::kMaxDrawShaders);
    if (nd) {
        std::fprintf(stderr, " draw_ps=");
        for (u32 i = 0; i < nd; ++i) std::fprintf(stderr, "%s%llx", i ? "," : "", hash_of(span.draw_ps[i]));
    }
    const u32 nc = std::min(span.dispatches, SpanResult::kMaxDrawShaders);
    if (nc) {
        std::fprintf(stderr, " dispatch_cs=");
        for (u32 i = 0; i < nc; ++i)
            std::fprintf(stderr, "%s%llx", i ? "," : "", hash_of(span.dispatch_cs[i]));
    }
    std::fprintf(stderr, " -> %s\n", outcome);
}

/// The images a pass would use: the source T#s of the payload and colour target 0 at BEGIN.
void LogImages(PassStats& stats, const Marker& begin, const AmdGpu::Regs& regs) {
    if (!GetState().log || stats.logged >= 8) return;
    const auto print = [](const char* what, const Texture& t) {
        std::fprintf(stderr, " %s=%010llx %ux%u fmt=%u/%u type=%u", what,
                     (unsigned long long)t.address, t.width, t.height, t.data_format, t.num_format,
                     t.type);
    };
    std::fprintf(stderr, "NATIVE_RENDER images pass=%.*s seq=%u", int(PassName(begin.pass).size()),
                 PassName(begin.pass).data(), begin.sequence);
    // Passes whose payload is image slots (bound, T#): print the bound ones.
    const bool slots = begin.pass == static_cast<u32>(Pass::PostBlend) ||
                       begin.pass == static_cast<u32>(Pass::Smaa);

    if (!slots && begin.payload_words >= 8) print("src", DecodeTexture(begin.payload));
    if (begin.pass == static_cast<u32>(Pass::Copy) && begin.payload_words >= 9)
        std::fprintf(stderr, " mode=%d", int(begin.payload[8]));
    if (begin.pass == static_cast<u32>(Pass::Fxaa) && begin.payload_words >= 17 && begin.payload[8])
        print("second", DecodeTexture(begin.payload + 9));
    if (slots) {
        static constexpr const char* kSlot[3] = {"slot0", "slot1", "slot2"};
        for (u32 i = 0; i < 3 && 9 * i + 9 <= begin.payload_words; ++i)
            if (begin.payload[9 * i]) print(kSlot[i], DecodeTexture(begin.payload + 9 * i + 1));
        if (begin.pass == static_cast<u32>(Pass::PostBlend) && begin.payload_words >= 28) {
            float value;
            std::memcpy(&value, begin.payload + 27, 4);
            std::fprintf(stderr, " value=%g", value);
        }
    }
    const auto& cb = regs.color_buffers[0];
    if (cb) {
        std::fprintf(stderr, " cb0=%010llx pitch=%u height=%u fmt=%u/%u", (unsigned long long)cb.Address(),
                     cb.Pitch(), cb.Height(), unsigned(cb.GetDataFmt()), unsigned(cb.GetNumberFmt()));
    } else {
        std::fprintf(stderr, " cb0=none");
    }
    std::fprintf(stderr, "\n");
}

} // namespace

const Switches& Active() {
    return GetState().switches;
}

bool AnyEnabled() {
    return GetState().switches.mask != 0;
}

const u32* OnBegin(const Services& services, const u32* packet, const u32* limit, bool blocked) {
    auto& s = GetState();
    if (!s.switches.mask || !packet || limit <= packet) return nullptr;
    Report();
    Marker begin;
    if (!ParseMarker(packet, std::size_t(limit - packet), begin) || begin.kind != kBegin)
        return nullptr;
    const bool probe = s.switches.Enabled(Pass::Probe);
    const bool wanted = begin.pass < kPassCount && s.switches.Enabled(static_cast<Pass>(begin.pass));
    if (!wanted && !probe) return nullptr;
    auto& stats = s.stats[begin.pass < kPassCount ? begin.pass : 0];
    ++stats.begins;
    const u32 body = ((packet[0] >> 16) & 0x3FFF) + 1;
    // The probe scans the whole span (every unsafe packet, the draws and shader after them).
    const bool probing = !wanted || begin.pass == static_cast<u32>(Pass::Probe);
    const SpanResult span = ScanSpan(packet + 1 + body, limit, begin, probing);
    ++stats.scans[static_cast<u32>(span.verdict)];
    if (probing) {
        LogImages(stats, begin, services.regs);
        LogSpan(stats, begin, span, "probe");
        return nullptr;
    }
    if (blocked) {
        ++stats.blocked;
        LogSpan(stats, begin, span, "blocked");
        return nullptr;
    }
    if (span.verdict != Scan::Ok) {
        ++stats.declined;
        LogSpan(stats, begin, span, "interpreted");
        return nullptr;
    }
    const RunFn run = kRun[begin.pass];
    const DrawFn draw = kDraw[begin.pass];
    const char* why = "not implemented";
    const bool accepted = run ? run(services, Request{begin, span}, why) : draw != nullptr;
    if (!accepted) {
        ++stats.declined;
        if (s.log && stats.logged < 8) {
            const auto name = PassName(begin.pass);
            std::fprintf(stderr, "NATIVE_RENDER declined pass=%.*s: %s\n", int(name.size()),
                         name.data(), why);
        }
        LogSpan(stats, begin, span, "declined");
        return nullptr;
    }
    ++stats.ran;
    s.current = begin.pass;
    s.current_begin = begin;
    s.current_draws = 0;
    s.current_dispatches = 0;
    LogSpan(stats, begin, span, "native");
    return span.end_marker;
}

bool OnDraw(const Services& services, bool dispatch) {
    auto& s = GetState();
    const u32 pass = s.current < kPassCount ? s.current : 0;
    const DrawFn draw = kDraw[pass];
    if (!draw) return true; // the pass ran for the whole span at BEGIN
    auto& stats = s.stats[pass];
    const u32 index = dispatch ? s.current_dispatches++ : s.current_draws++;
    const char* why = "declined";
    if (draw(services, DrawRequest{s.current_begin, index, dispatch}, why)) {
        ++stats.native_draws;
        return true;
    }
    ++stats.fallback_draws;
    if (s.log && stats.draw_logged < 8) {
        ++stats.draw_logged;
        const auto name = PassName(pass);
        std::fprintf(stderr, "NATIVE_RENDER fallback pass=%.*s %s=%u: %s\n", int(name.size()),
                     name.data(), dispatch ? "dispatch" : "draw", index, why);
    }
    return false;
}

void NoteDropped(bool dispatch) {
    auto& s = GetState();
    auto& stats = s.stats[s.current < kPassCount ? s.current : 0];
    if (dispatch) ++stats.dropped_dispatches;
    else ++stats.dropped_draws;
}

void Report() {
    auto& s = GetState();
    if (!s.switches.mask) return;
    const auto now = std::chrono::steady_clock::now();
    if (now - s.last_report < std::chrono::seconds(10)) return;
    s.last_report = now;
    for (u32 p = 1; p < kPassCount; ++p) {
        const auto& st = s.stats[p];
        if (!st.begins) continue;
        std::fprintf(stderr,
                     "NATIVE_RENDER report pass=%.*s begins=%llu native=%llu declined=%llu "
                     "blocked=%llu no_end=%llu unsafe=%llu dropped_draws=%llu "
                     "dropped_dispatches=%llu native_draws=%llu fallback_draws=%llu\n",
                     int(kPassNames[p].size()), kPassNames[p].data(),
                     (unsigned long long)st.begins, (unsigned long long)st.ran,
                     (unsigned long long)st.declined, (unsigned long long)st.blocked,
                     (unsigned long long)st.scans[static_cast<u32>(Scan::NoEnd)],
                     (unsigned long long)st.scans[static_cast<u32>(Scan::Unsafe)],
                     (unsigned long long)st.dropped_draws,
                     (unsigned long long)st.dropped_dispatches,
                     (unsigned long long)st.native_draws, (unsigned long long)st.fallback_draws);
    }
}

} // namespace iss::native_render
