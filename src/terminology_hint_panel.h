// terminology_hint_panel.h — Terminology sub-section of LineContextPanel
// Phase 2 of SANAE_REVAMP_PLAN.md §3.3
//
// Shows 3–5 relevant terms for the current EN source line.
// Click-to-apply (no hotkeys yet — Alt+1..5 conflict unresolved, see HOTKEY_AUDIT.md).
//
// Heavy/light split:
//   OnActiveLineChanged → Search (Aho-Corasick, once per line)
//   OnTextChanged       → UpdateUsage (cheap substring check, per keystroke)

#pragma once

#include "sanae_terminology_index.h"

#include <wx/panel.h>

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

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
