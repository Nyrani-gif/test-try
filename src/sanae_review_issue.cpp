// sanae_review_issue.cpp — state machine implementation
// Phase 3 of SANAE_REVAMP_PLAN.md §3.5, §3.8

#include "sanae_review_issue.h"

namespace sanae {

bool IsTransitionAllowed(ReviewIssueState from, ReviewIssueState to) noexcept {
    if (from == to) return false;
    switch (from) {
        case ReviewIssueState::Open:
            return to == ReviewIssueState::ReadyForReview
                || to == ReviewIssueState::Resolved
                || to == ReviewIssueState::WontFix;
        case ReviewIssueState::ReadyForReview:
            return to == ReviewIssueState::Open
                || to == ReviewIssueState::Resolved
                || to == ReviewIssueState::WontFix;
        case ReviewIssueState::Resolved:
            return to == ReviewIssueState::Open;
        case ReviewIssueState::WontFix:
            return to == ReviewIssueState::Open;
    }
    return false;
}

TransitionResult SanaeReviewIssue::ApplyTransition(
    ReviewIssueState new_state,
    const std::string& now_iso,
    const std::string& acting_device_id,
    const std::string& new_resolution_note,
    const std::string& new_body,
    const std::string& attempted_baseline_text_hash,
    const std::string& attempted_baseline_timing_hash) {

    if (!attempted_baseline_text_hash.empty()
        && attempted_baseline_text_hash != baseline_text_hash)
        return TransitionResult::ImmutableFieldChanged;
    if (!attempted_baseline_timing_hash.empty()
        && attempted_baseline_timing_hash != baseline_timing_hash)
        return TransitionResult::ImmutableFieldChanged;

    if (!IsTransitionAllowed(state, new_state))
        return TransitionResult::InvalidTransition;

    if (new_state == ReviewIssueState::WontFix) {
        if (new_resolution_note.empty())
            return TransitionResult::MissingResolutionNote;
    }

    state = new_state;

    if (new_state == ReviewIssueState::WontFix) {
        resolution_note = new_resolution_note;
    } else {
        resolution_note.clear();
    }

    if (new_state == ReviewIssueState::Resolved) {
        resolved_at = now_iso;
        resolved_by_device_id = acting_device_id;
    } else {
        resolved_at.clear();
        resolved_by_device_id.clear();
    }

    if (!new_body.empty())
        body = new_body;

    ++version;
    updated_at = now_iso;
    return TransitionResult::Ok;
}

bool SanaeReviewIssue::AddComment(const SanaeComment& comment) {
    if (IsDeleted()) return false;
    if (comment.body.empty()) return false;
    comments.push_back(comment);
    return true;
}

bool SanaeReviewIssue::SoftDelete(const std::string& now_iso) {
    if (IsDeleted()) return false;
    deleted_at = now_iso;
    ++version;
    updated_at = now_iso;
    return true;
}

const char* SanaeReviewIssue::StateName(ReviewIssueState s) {
    switch (s) {
        case ReviewIssueState::Open:           return "open";
        case ReviewIssueState::ReadyForReview: return "ready_for_review";
        case ReviewIssueState::Resolved:       return "resolved";
        case ReviewIssueState::WontFix:        return "wont_fix";
    }
    return "unknown";
}

ReviewIssueState SanaeReviewIssue::StateFromString(const std::string& s) {
    if (s == "open") return ReviewIssueState::Open;
    if (s == "ready_for_review") return ReviewIssueState::ReadyForReview;
    if (s == "resolved") return ReviewIssueState::Resolved;
    if (s == "wont_fix") return ReviewIssueState::WontFix;
    return ReviewIssueState::Open;
}

std::string SanaeReviewIssue::StateToString(ReviewIssueState s) {
    return StateName(s);
}

} // namespace sanae
