// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_render.h"

#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace iss::native_render {
namespace {

// The pass implementations, by pass number. A pass without one is declined ("not implemented"),
// so a hook can land before its pass and be checked with ISS_NATIVE_RENDER=probe.
constexpr std::array<RunFn, kPassCount> kRun = {nullptr, nullptr};

struct PassStats {
    u64 begins{}, ran{}, declined{}, blocked{};
    std::array<u64, 4> scans{};
    u64 dropped_draws{}, dropped_dispatches{};
    u32 logged{};
};

struct State {
    Switches switches;
    bool log{};
    std::array<PassStats, kPassCount> stats{};
    u32 current{}; ///< the pass whose span is being dropped (0: none)
    u64 reports{};
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
    if (span.verdict == Scan::Unsafe)
        std::fprintf(stderr, " opcode=%02x word=%08x", span.bad_opcode, span.bad_word);
    std::fprintf(stderr, " -> %s\n", outcome);
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
    const SpanResult span = ScanSpan(packet + 1 + body, limit, begin);
    ++stats.scans[static_cast<u32>(span.verdict)];
    if (!wanted || begin.pass == static_cast<u32>(Pass::Probe)) {
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
    const char* why = "not implemented";
    if (!run || !run(services, Request{begin, span}, why)) {
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
    LogSpan(stats, begin, span, "native");
    return span.end_marker;
}

void NoteDropped(bool dispatch) {
    auto& s = GetState();
    auto& stats = s.stats[s.current < kPassCount ? s.current : 0];
    if (dispatch) ++stats.dropped_dispatches;
    else ++stats.dropped_draws;
}

void Report() {
    auto& s = GetState();
    if (!s.switches.mask || ++s.reports % 600 != 0) return;
    for (u32 p = 1; p < kPassCount; ++p) {
        const auto& st = s.stats[p];
        if (!st.begins) continue;
        std::fprintf(stderr,
                     "NATIVE_RENDER report pass=%.*s begins=%llu native=%llu declined=%llu "
                     "blocked=%llu no_end=%llu unsafe=%llu dropped_draws=%llu "
                     "dropped_dispatches=%llu\n",
                     int(kPassNames[p].size()), kPassNames[p].data(),
                     (unsigned long long)st.begins, (unsigned long long)st.ran,
                     (unsigned long long)st.declined, (unsigned long long)st.blocked,
                     (unsigned long long)st.scans[static_cast<u32>(Scan::NoEnd)],
                     (unsigned long long)st.scans[static_cast<u32>(Scan::Unsafe)],
                     (unsigned long long)st.dropped_draws,
                     (unsigned long long)st.dropped_dispatches);
    }
}

} // namespace iss::native_render
