// sanae_ux_metrics.h — UX baseline measurement hooks
// Phase 0.11 of SANAE_REVAMP_PLAN.md
//
// These hooks measure UX interactions that the revamp aims to reduce:
//   - modal dialog opens (Sanae-related)
//   - manual terminology searches
//   - FinalReview/QC issue interactions
//   - translation session start/end
//   - saved-line counter (reaching 20 translated lines = one UX-baseline unit)
//
// Hooks emit LOG_I ("sanae/ux/...") messages with structured fields.
// Real user study (2-5 translators) is EXTERNAL, but the hooks exist in code
// so that subsequent UX changes do not destroy the baseline measurement.
//
// GATING: controlled by "Sanae/Profiling/Enabled" (same option as timing).
// When disabled, hooks are no-op (one option lookup, no string formatting).

#pragma once

#include <chrono>
#include <string>

#include <libaegisub/log.h>

#include "options.h"

namespace sanae {
namespace ux {

inline bool ux_metrics_enabled() {
    return OPT_GET("Sanae/Profiling/Enabled")->GetBool();
}

// Modal dialog opened. `dialog_name` is a short identifier like
// "terminology", "final_review", "terminology_entry", "project",
// "batch_import", "connection", "episode", "repeat_history", "project_search".
inline void modal_opened(const char* dialog_name) {
    if (!ux_metrics_enabled()) return;
    LOG_I("sanae/ux/modal") << "open " << dialog_name;
}

// Manual terminology search invoked (via menu or command, not inline).
inline void terminology_manual_search() {
    if (!ux_metrics_enabled()) return;
    LOG_I("sanae/ux/terminology") << "manual_search";
}

// Terminology term applied via inline hint (Phase 2 success metric).
inline void terminology_inline_applied() {
    if (!ux_metrics_enabled()) return;
    LOG_I("sanae/ux/terminology") << "inline_applied";
}

// Terminology suggestion impressions (Phase 2 KPI).
// Called when the LineContextPanel shows term suggestions to the user.
// `count` = number of terms shown (typically 3-5).
// `is_top3` = true if this is the first 3 suggestions shown for this line
//             (used for Apply rate computation).
// `line_ref` = a short identifier for the current line (for correlating
//              impressions with later user feedback in external studies).
//              Empty if no line is active.
//
// NOTE: These logs support:
//   - impression count
//   - top-3 impressions
//   - Apply rate (apply events / impression events)
//
// They do NOT by themselves compute:
//   - Relevance@3 (requires explicit user "was this relevant?" feedback)
//   - Irrelevant suggestion rate (requires user annotation)
//
// Real Relevance@3 and Irrelevant rate require an external user study where
// translators annotate each impression as relevant/irrelevant. The line_ref
// in the log enables correlation between impressions and user annotations.
inline void terminology_suggestion_impression(int count, bool is_top3 = false,
                                               const std::string& line_ref = "") {
    if (!ux_metrics_enabled()) return;
    LOG_I("sanae/ux/terminology") << "suggestion_impression N=" << count
                                   << (is_top3 ? " top3" : "")
                                   << " line=" << line_ref;
}

// Line context panel shown/hidden (measures how often panel has content).
inline void line_context_shown() {
    if (!ux_metrics_enabled()) return;
    LOG_I("sanae/ux/line_context") << "shown";
}

inline void line_context_hidden() {
    if (!ux_metrics_enabled()) return;
    LOG_I("sanae/ux/line_context") << "hidden";
}

// FinalReview / QC issue interaction.
// `action` is one of: "accept", "return", "comment", "wont_fix", "goto_line",
// "correct_selected", "use_previous_translation".
inline void qc_issue_interaction(const char* action) {
    if (!ux_metrics_enabled()) return;
    LOG_I("sanae/ux/qc") << "action " << action;
}

// Translation session lifecycle.
// `event` is "start" or "end". `episode_id` may be empty.
inline void translation_session(const char* event, const std::string& episode_id = "") {
    if (!ux_metrics_enabled()) return;
    LOG_I("sanae/ux/session") << event << " episode=" << episode_id;
}

// Saved-line counter. Emitted every N lines (default 20) to measure
// the "time to translate 20 lines" UX baseline metric.
// `total_saved` is the cumulative count of saved lines in this session.
inline void saved_line_milestone(int total_saved) {
    if (!ux_metrics_enabled()) return;
    LOG_I("sanae/ux/saved") << "milestone N=" << total_saved;
}

// RAII helper for measuring modal dialog duration.
class ModalDuration {
public:
    explicit ModalDuration(const char* dialog_name)
        : name_(dialog_name)
        , enabled_(ux_metrics_enabled())
        , start_(enabled_ ? std::chrono::steady_clock::now()
                          : std::chrono::steady_clock::time_point{}) {
        if (enabled_) LOG_I("sanae/ux/modal") << "open " << name_;
    }

    ~ModalDuration() {
        if (!enabled_) return;
        auto end = std::chrono::steady_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start_).count();
        LOG_I("sanae/ux/modal") << "close " << name_ << " duration_ms=" << ms;
    }

    ModalDuration(ModalDuration const&) = delete;
    ModalDuration& operator=(ModalDuration const&) = delete;

private:
    const char* name_;
    bool enabled_;
    std::chrono::steady_clock::time_point start_;
};

} // namespace ux
} // namespace sanae
