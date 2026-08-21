// sanae_modified_after_issue.h — computed ReviewIssue modification flag

#pragma once

#include <string>

class AssDialogue;

namespace sanae {

// Pure hash comparison used by unit tests and the production adapter.
bool ComputeModifiedAfterIssue(
    const std::string& current_text_hash,
    const std::string& current_timing_hash,
    const std::string& baseline_text_hash,
    const std::string& baseline_timing_hash,
    const std::string& issue_kind_name);

// AssDialogue adapter (application target only).
bool ComputeModifiedAfterIssue(
    const AssDialogue *line,
    const std::string& baseline_text_hash,
    const std::string& baseline_timing_hash,
    const std::string& issue_kind_name);

} // namespace sanae
