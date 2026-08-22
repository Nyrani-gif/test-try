// terminology_hint_panel.h — Terminology sub-section of LineContextPanel
// Phase 2 of SANAE_REVAMP_PLAN.md §3.3
//
// Shows 3–5 relevant terms for the current EN source line.
// Can be driven by mouse or by the Sanae Terminology hotkey context.
//
// Heavy/light split:
//   OnActiveLineChanged → Search (Aho-Corasick, once per line)
//   OnTextChanged       → UpdateUsage (cheap substring check, per keystroke)

#pragma once

#include "sanae_terminology_index.h"

#include <wx/panel.h>

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

class AssDialogue;
class SubsTextEditCtrl;
namespace agi { class Context; }
class SanaeProjectManager;

class TerminologyHintPanel : public wxPanel {
public:
    TerminologyHintPanel(wxWindow *parent, agi::Context *context,
                         SubsTextEditCtrl *edit_ctrl);
    ~TerminologyHintPanel();

    // Heavy: search EN source for terms. Called on ActiveLineChanged.
    void OnActiveLineChanged(AssDialogue *line);

    // Light: update usage state. Called on text change.
    void OnTextChanged();

    // Apply a cached suggestion (0-based). Returns false when the requested
    // suggestion is not currently available.
    bool ApplySuggestion(size_t index);

    // Ignore a cached suggestion for the current episode (0-based). This is
    // intentionally conservative: Ctrl+I should suppress the noisy match in
    // the current episode without silently hiding a project-wide glossary term.
    bool IgnoreSuggestion(size_t index);

    size_t SuggestionCount() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
