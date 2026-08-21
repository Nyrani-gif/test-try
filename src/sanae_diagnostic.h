// sanae_diagnostic.h — Transient computed diagnostic (NOT persisted)
// Phase 3 of SANAE_REVAMP_PLAN.md §3.4

#pragma once

#include <cstdint>
#include <string>

class AssDialogue;

namespace sanae {

enum class DiagnosticKind : uint8_t {
    CpsHigh, CpsLow, Duration, Length, Overlap, Spaces,
    Tags, Style, Empty, Whitespace, Dash, Quotes,
    Ellipsis, LineBreaks, Untranslated, Punctuation,
    TagMalformed, TerminologyDrift, SourceRepeat
};

enum class DiagnosticSeverity : uint8_t {
    Info, Warning, Error
};

struct SanaeDiagnostic {
    DiagnosticKind kind;
    DiagnosticSeverity severity;
    std::string code;
    std::string message;
    AssDialogue *line = nullptr;     // non-owning, transient
    std::string replacement_from;    // for one-click fix, optional
    std::string replacement_to;

    // NO id, NO state, NO comments, NO created_by, NO sync.
    // Computed on-the-fly. Disappears when condition resolves.
};

} // namespace sanae
