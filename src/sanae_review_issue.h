// sanae_review_issue.h — Persistent human-authored review issue
// Phase 3 of SANAE_REVAMP_PLAN.md §3.4, §3.5, §3.8
// Authoritative: SANAE_SERVER_REQUIREMENTS_v0.3.md §2.1, §3

#pragma once

#include "sanae_comment.h"

#include <cstdint>
#include <string>
#include <vector>

namespace sanae {

enum class ReviewIssueKind : uint8_t {
    Translation, Terminology, Timing, Style, Formatting, Other,
};

enum class ReviewIssueSeverity : uint8_t {
    Info, Warning, Error,
};

enum class ReviewIssueState : uint8_t {
    Open, ReadyForReview, Resolved, WontFix,
};

enum class TransitionResult : uint8_t {
    Ok,
    InvalidTransition,
    MissingResolutionNote,
    ResolutionNoteNotAllowed,
    ImmutableFieldChanged,
};

struct SanaeReviewIssue {
    std::string id;                 // UUID
    std::string local_line_id;      // local sidecar identity (e.g. "li_000001")
    std::string line_ref;           // future server wire identity (PENDING, empty in V0.3)

    ReviewIssueKind kind = ReviewIssueKind::Translation;
    ReviewIssueSeverity severity = ReviewIssueSeverity::Warning;
    ReviewIssueState state = ReviewIssueState::Open;

    std::string body;               // "what is wrong", nullable (empty = null)
    std::string resolution_note;    // "why wont_fix", empty unless state==WontFix

    int version = 1;                // current entity version (monotonic)

    std::string baseline_text_hash;     // immutable after creation
    std::string baseline_timing_hash;   // immutable after creation

    std::string created_by_device_id;
    std::string created_at;
    std::string updated_at;
    std::string resolved_at;        // cleared on Reopen
    std::string resolved_by_device_id;
    std::string deleted_at;         // soft-delete

    std::vector<SanaeComment> comments;

    bool IsWontFix() const noexcept { return state == ReviewIssueState::WontFix; }
    bool IsResolved() const noexcept { return state == ReviewIssueState::Resolved; }
    bool IsOpen() const noexcept { return state == ReviewIssueState::Open; }
    bool IsReadyForReview() const noexcept { return state == ReviewIssueState::ReadyForReview; }
    bool IsDeleted() const noexcept { return !deleted_at.empty(); }

    bool BlocksEpisodeDone() const noexcept {
        if (IsDeleted()) return false;
        return state == ReviewIssueState::Open
            || state == ReviewIssueState::ReadyForReview;
    }

    TransitionResult ApplyTransition(
        ReviewIssueState new_state,
        const std::string& now_iso,
        const std::string& acting_device_id,
        const std::string& new_resolution_note = "",
        const std::string& new_body = "",
        const std::string& attempted_baseline_text_hash = "",
        const std::string& attempted_baseline_timing_hash = "");

    bool AddComment(const SanaeComment& comment);
    bool SoftDelete(const std::string& now_iso);

    static const char* StateName(ReviewIssueState s);
    static ReviewIssueState StateFromString(const std::string& s);
    static std::string StateToString(ReviewIssueState s);
};

bool IsTransitionAllowed(ReviewIssueState from, ReviewIssueState to) noexcept;

} // namespace sanae
