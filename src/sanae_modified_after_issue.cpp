// sanae_modified_after_issue.cpp — implementation
// Phase 3.10 of SANAE_REVAMP_PLAN.md

#include "sanae_modified_after_issue.h"

#include "ass_dialogue.h"
#include "sanae_baseline_fingerprint.h"

namespace sanae {

bool ComputeModifiedAfterIssue(
    const AssDialogue *line,
    const std::string& baseline_text_hash,
    const std::string& baseline_timing_hash,
    const std::string& issue_kind_name) {

    if (!line) return false;

    bool text_matters = true;
    bool timing_matters = true;

    if (issue_kind_name == "translation" || issue_kind_name == "terminology") {
        timing_matters = false;
    } else if (issue_kind_name == "timing") {
        text_matters = false;
    }
    // "style", "formatting", "other", "" → both matter

    if (text_matters && !baseline_text_hash.empty()) {
        auto current_text_hash = compute_text_hash(*line);
        if (current_text_hash != baseline_text_hash)
            return true;
    }

    if (timing_matters && !baseline_timing_hash.empty()) {
        auto current_timing_hash = compute_timing_hash(*line);
        if (current_timing_hash != baseline_timing_hash)
            return true;
    }

    return false;
}

} // namespace sanae
