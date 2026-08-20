// sanae_qc_checks.h — Extended auto-QC checks producing SanaeDiagnostic
// Phase 3.4-3.5 of SANAE_REVAMP_PLAN.md
//
// Wraps the existing TranslationProject::CheckLine and adds new checks
// (empty, whitespace, dash, quotes, ellipsis, CPS low, line breaks,
// untranslated, punctuation, malformed tags) controlled by QCProfile.
//
// Terminology NotUsed is NOT a Diagnostic.

#pragma once

#include "sanae_diagnostic.h"
#include "sanae_qc_profile.h"

#include <vector>

class AssDialogue;
class AssFile;
class TranslationProject;

namespace sanae {

// Compute all diagnostics for a single line.
// `prev` is the preceding dialogue line (for overlap check), may be nullptr.
// `profile` controls which checks are active and their severity.
// `tp` provides existing CheckLine results and CPS computation.
std::vector<SanaeDiagnostic> ComputeDiagnostics(
    const AssDialogue *line,
    const AssDialogue *prev,
    const SanaeQCProfile& profile,
    const TranslationProject *tp);

// Helper: check if a line has blocking Error-severity diagnostics.
bool HasBlockingDiagnostic(const std::vector<SanaeDiagnostic>& diags);

} // namespace sanae
