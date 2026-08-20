// sanae_modified_after_issue.h — Computed flag for ReviewIssue
// Phase 3.10 of SANAE_REVAMP_PLAN.md
//
// modified_after_issue is a CLIENT-COMPUTED flag, NOT persisted state.
// It is computed from baseline_text_hash / baseline_timing_hash vs current
// line text/timing. It does NOT change ReviewIssue state.

#pragma once

#include <string>

class AssDialogue;

namespace sanae {

// Compute whether the line has been modified after the issue was created.
// `baseline_text_hash` and `baseline_timing_hash` come from the ReviewIssue.
// Uses compute_text_hash / compute_timing_hash from sanae_baseline_fingerprint.
//
// `issue_kind_name` controls field-specific behavior:
//   "translation" or "terminology" → text changes matter
//   "timing" → timing changes matter
//   "style", "formatting", "other" → both matter
//   "" (empty) → both matter (conservative)
//
// Returns true if the relevant field's current hash differs from baseline.
bool ComputeModifiedAfterIssue(
    const AssDialogue *line,
    const std::string& baseline_text_hash,
    const std::string& baseline_timing_hash,
    const std::string& issue_kind_name);

} // namespace sanae
