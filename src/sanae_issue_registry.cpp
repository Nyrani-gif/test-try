// sanae_issue_registry.cpp — implementation
// Phase 3 of SANAE_REVAMP_PLAN.md §3.4

#include "sanae_issue_registry.h"

#include "ass_dialogue.h"

namespace sanae {

SanaeIssueRegistry::SanaeIssueRegistry() = default;

void SanaeIssueRegistry::SetDiagnostics(std::vector<SanaeDiagnostic> diags) {
    diagnostics_ = std::move(diags);
}

void SanaeIssueRegistry::AddReviewIssue(SanaeReviewIssue issue) {
    review_issues_.push_back(std::move(issue));
}

void SanaeIssueRegistry::UpdateReviewIssue(const std::string& id, SanaeReviewIssue updated) {
    for (auto& issue : review_issues_) {
        if (issue.id == id) {
            issue = std::move(updated);
            return;
        }
    }
}

void SanaeIssueRegistry::RemoveReviewIssue(const std::string& id) {
    review_issues_.erase(
        std::remove_if(review_issues_.begin(), review_issues_.end(),
            [&](const SanaeReviewIssue& i) { return i.id == id; }),
        review_issues_.end());
}

std::vector<SanaeIssueRegistry::LineProblem>
SanaeIssueRegistry::ProblemsForLine(AssDialogue *line,
                                     LocalLineIdRegistry& line_id_reg) const {
    std::vector<LineProblem> result;

    // Diagnostics: match by line pointer.
    for (auto const& d : diagnostics_) {
        if (d.line == line) {
            result.push_back({true, &d, nullptr});
        }
    }

    // ReviewIssues: match by local_line_id.
    std::string llid = line_id_reg.LookupByDialogue(line);
    if (!llid.empty()) {
        for (auto const& issue : review_issues_) {
            if (!issue.IsDeleted() && issue.local_line_id == llid) {
                result.push_back({false, nullptr, &issue});
            }
        }
    }

    return result;
}

int SanaeIssueRegistry::CountBlockingForLine(AssDialogue *line,
                                               LocalLineIdRegistry& line_id_reg) const {
    int count = 0;
    auto problems = ProblemsForLine(line, line_id_reg);
    for (auto const& p : problems) {
        if (p.is_diagnostic) {
            if (p.diagnostic->severity == DiagnosticSeverity::Error)
                ++count;
        } else {
            if (p.issue->BlocksEpisodeDone())
                ++count;
        }
    }
    return count;
}

} // namespace sanae
