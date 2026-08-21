// AssDialogue adapter for modified_after_issue.

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
    return ComputeModifiedAfterIssue(
        compute_text_hash(*line),
        compute_timing_hash(*line),
        baseline_text_hash,
        baseline_timing_hash,
        issue_kind_name);
}

} // namespace sanae
