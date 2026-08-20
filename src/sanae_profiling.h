// sanae_profiling.h — RAII timing instrumentation for the Sanae revamp
// Phase 0.1 of SANAE_REVAMP_PLAN.md
//
// GATING: instrumentation is controlled by the "Sanae/Profiling/Enabled"
// option (agi::OptionValue, default false). When disabled, timer acquisition
// (steady_clock::now()) is skipped; only the profiling option check/branch
// remains. This is NOT literally zero overhead — one option lookup per timer
// construction — but it is cheap enough for non-hotpaths. For OnPaint-level
// frequency the option lookup is a single pointer dereference through the
// option cache.
//
// The gate is checked once per timer construction (one OPT_GET lookup,
// which is a fast map lookup in agi::Options).
//
// To enable profiling:
//   - Set "Sanae/Profiling/Enabled" : true in config.json, OR
//   - In Preferences → Sanae → Profiling (UI to be added in Phase 4), OR
//   - Programmatically: OPT_SET("Sanae/Profiling/Enabled", true)
//
// LOG_D messages are emitted with section "sanae/profile/..." and can be
// further filtered by the standard agi::log severity system.

#pragma once

#include <chrono>
#include <string>

#include <libaegisub/log.h>

#include "options.h"

namespace sanae {

// Check whether profiling is enabled. Cheap (one option lookup).
inline bool profiling_enabled() {
    return OPT_GET("Sanae/Profiling/Enabled")->GetBool();
}

class ScopedTimer {
public:
    ScopedTimer(const char* section, const char* label, const char* suffix = "took")
        : section_(section), label_(label), suffix_(suffix)
        , enabled_(profiling_enabled())
        , start_(enabled_ ? std::chrono::steady_clock::now()
                          : std::chrono::steady_clock::time_point{}) {}

    ~ScopedTimer() {
        if (!enabled_) return;
        auto end = std::chrono::steady_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start_).count();
        LOG_D(section_) << label_ << " " << suffix_ << " " << ms << " ms";
    }

    ScopedTimer(ScopedTimer const&) = delete;
    ScopedTimer& operator=(ScopedTimer const&) = delete;

private:
    const char* section_;
    const char* label_;
    const char* suffix_;
    bool enabled_;
    std::chrono::steady_clock::time_point start_;
};

class PhaseTimer {
public:
    explicit PhaseTimer(const char* section)
        : section_(section)
        , enabled_(profiling_enabled())
        , start_(enabled_ ? std::chrono::steady_clock::now()
                          : std::chrono::steady_clock::time_point{})
        , last_(start_) {}

    void Mark(const char* phase) {
        if (!enabled_) return;
        auto now = std::chrono::steady_clock::now();
        auto phase_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_).count();
        LOG_D(section_) << "phase " << phase << " took " << phase_ms << " ms";
        last_ = now;
    }

    ~PhaseTimer() {
        if (!enabled_) return;
        auto end = std::chrono::steady_clock::now();
        auto total_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start_).count();
        LOG_D(section_) << "total took " << total_ms << " ms";
    }

    PhaseTimer(PhaseTimer const&) = delete;
    PhaseTimer& operator=(PhaseTimer const&) = delete;

private:
    const char* section_;
    bool enabled_;
    std::chrono::steady_clock::time_point start_;
    std::chrono::steady_clock::time_point last_;
};

// Counted variant: logs count alongside duration (for GenerateCandidates etc.)
class ScopedTimerWithCount {
public:
    ScopedTimerWithCount(const char* section, const char* label, size_t count)
        : section_(section), label_(label), count_(count)
        , enabled_(profiling_enabled())
        , start_(enabled_ ? std::chrono::steady_clock::now()
                          : std::chrono::steady_clock::time_point{}) {}

    ~ScopedTimerWithCount() {
        if (!enabled_) return;
        auto end = std::chrono::steady_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start_).count();
        LOG_D(section_) << label_ << " took " << ms << " ms (N=" << count_ << ")";
    }

    ScopedTimerWithCount(ScopedTimerWithCount const&) = delete;
    ScopedTimerWithCount& operator=(ScopedTimerWithCount const&) = delete;

private:
    const char* section_;
    const char* label_;
    size_t count_;
    bool enabled_;
    std::chrono::steady_clock::time_point start_;
};

} // namespace sanae
