// sanae_issue_registry.h — Aggregates Diagnostic + ReviewIssue for unified UI
// Phase 3 of SANAE_REVAMP_PLAN.md §3.4

#pragma once

#include "sanae_diagnostic.h"
#include "sanae_review_issue.h"
#include "sanae_local_line_id.h"

#include <memory>
#include <string>
#include <vector>

class AssDialogue;
namespace agi { class Context; }

namespace sanae {

class SanaeIssueRegistry {
public:
    SanaeIssueRegistry();

    // Diagnostics (transient, recomputed)
    void SetDiagnostics(std::vector<SanaeDiagnostic> diags);
    const std::vector<SanaeDiagnostic>& Diagnostics() const { return diagnostics_; }

    // ReviewIssues (persistent, local sidecar)
    void AddReviewIssue(SanaeReviewIssue issue);
    void UpdateReviewIssue(const std::string& id, SanaeReviewIssue updated);
    void RemoveReviewIssue(const std::string& id);
    const std::vector<SanaeReviewIssue>& ReviewIssues() const { return review_issues_; }

    // Get all problems for a specific line (diagnostics + issues).
    // Uses local_line_id for issue matching.
    struct LineProblem {
        bool is_diagnostic;
        const SanaeDiagnostic* diagnostic = nullptr;
        const SanaeReviewIssue* issue = nullptr;
    };
    std::vector<LineProblem> ProblemsForLine(AssDialogue *line,
                                              LocalLineIdRegistry& line_id_reg) const;

    // Count blocking problems (for QCPassed invariant).
    int CountBlockingForLine(AssDialogue *line,
                              LocalLineIdRegistry& line_id_reg) const;

private:
    std::vector<SanaeDiagnostic> diagnostics_;
    std::vector<SanaeReviewIssue> review_issues_;
};

} // namespace sanae
