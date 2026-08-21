// sanae_qc_checks.h — Extended auto-QC checks producing SanaeDiagnostic
// Phase 3.4-3.5 of SANAE_REVAMP_PLAN.md

#pragma once

#include "sanae_diagnostic.h"
#include "sanae_qc_profile.h"

#include <string>
#include <vector>

class AssDialogue;
class TranslationProject;

namespace sanae {

// Pure rule input. AssDialogue is carried only as an opaque non-owning pointer
// so resulting diagnostics can still navigate to the production line; the pure
// rule engine never dereferences it.
struct QCRuleInput {
    AssDialogue *line = nullptr;
    std::string visible_text;
    std::string source_text;
    int duration_ms = 0;
    int cps = -1;
    int visible_length = 0;
    int line_break_count = 0;
    bool has_double_spaces = false;
    bool has_unbalanced_tags = false;
    bool is_drawing = false;
    bool has_overlap = false;
};

// Pure QC rule engine.
std::vector<SanaeDiagnostic> ComputeDiagnostics(
    const QCRuleInput& input, const SanaeQCProfile& profile);

// Application adapter, defined in sanae_qc_checks_adapters.cpp.
std::vector<SanaeDiagnostic> ComputeDiagnostics(
    const AssDialogue *line,
    const AssDialogue *prev,
    const SanaeQCProfile& profile,
    const TranslationProject *tp);

bool HasBlockingDiagnostic(const std::vector<SanaeDiagnostic>& diags);

} // namespace sanae
