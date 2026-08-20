// line_context_panel.h — Single priority-ordered context panel under edit box
// Phase 2 of SANAE_REVAMP_PLAN.md §3.2
//
// One panel, not a stack. Priority order:
//   1. Critical open ReviewIssue (Phase 3)
//   2. Other open ReviewIssues (Phase 3)
//   3. Relevant terminology (Phase 2)
//   4. Repeat memory (Phase 2)
//   5. Warning/Error Diagnostics (Phase 3)
//   6. Info Diagnostics (collapsed, Phase 3)
//
// When empty: collapses to 0 height (hidden).
// Feature flag: Sanae/InlineTerminology (Phase 2 uses this flag for the
// entire panel; Phase 3 adds Sanae/UnifiedProblemsList for issues).

#pragma once

#include <wx/panel.h>

class AssDialogue;
class SubsTextEditCtrl;
namespace agi { class Context; }

class LineContextPanel : public wxPanel {
public:
    LineContextPanel(wxWindow *parent, agi::Context *context, SubsTextEditCtrl *edit_ctrl);
    ~LineContextPanel();

    // Called by SubsEditBox when the active line changes.
    // Triggers heavy terminology search (Aho-Corasick) on the EN source.
    void OnActiveLineChanged(AssDialogue *new_line);

    // Called by SubsEditBox when the edit box text changes (RU keystroke).
    // Triggers light usage check on cached term matches.
    void OnTextChanged();

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
