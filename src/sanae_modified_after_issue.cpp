// sanae_modified_after_issue.cpp — pure hash comparison

#include "sanae_modified_after_issue.h"

namespace sanae {

bool ComputeModifiedAfterIssue(
    const std::string& current_text_hash,
    const std::string& current_timing_hash,
    const std::string& baseline_text_hash,
    const std::string& baseline_timing_hash,
    const std::string& issue_kind_name) {

    bool text_matters = true;
    bool timing_matters = true;

    if (issue_kind_name == "translation" || issue_kind_name == "terminology") {
        timing_matters = false;
    } else if (issue_kind_name == "timing") {
        text_matters = false;
    }

    if (text_matters && !baseline_text_hash.empty()
        && current_text_hash != baseline_text_hash)
        return true;

    if (timing_matters && !baseline_timing_hash.empty()
        && current_timing_hash != baseline_timing_hash)
        return true;

    return false;
}

} // namespace sanae
