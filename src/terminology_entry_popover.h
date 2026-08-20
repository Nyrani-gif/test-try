// terminology_entry_popover.h — Unified popover for adding/editing terminology entries
// Phase 2.8 of SANAE_REVAMP_PLAN.md §4.1.4
//
// Replaces the duplicate TermEditDialog (dialog_sanae_terminology.cpp) and
// TerminologyEntryDialog (dialog_sanae_final_review.cpp) with a single
// wxPopupTransientWindow.
//
// Fields: EN / RU / Note (+ optional aliases V1.5).
// Prefill from edit context: EN from SourceDisplayText, RU from selection or full line.
// Ctrl+T command opens this popover (command exists without conflicting default binding).

#pragma once

#include <wx/popupwin.h>

#include <memory>
#include <string>

class AssDialogue;
class SubsTextEditCtrl;
namespace agi { class Context; }
class SanaeProjectManager;
class wxTextCtrl;

class TerminologyEntryPopover : public wxPopupTransientWindow {
public:
    // Create for adding a new term. Prefills EN/RU from the edit context.
    TerminologyEntryPopover(wxWindow *parent, agi::Context *context,
                            AssDialogue *active_line,
                            SubsTextEditCtrl *edit_ctrl);

    // Create for adding a new term with explicit prefill (from candidate list etc.)
    TerminologyEntryPopover(wxWindow *parent, agi::Context *context,
                            std::string english_prefill,
                            std::string russian_prefill);

    ~TerminologyEntryPopover();

private:
    void PositionNearEditCtrl(wxWindow *parent, SubsTextEditCtrl *edit_ctrl);
    struct Impl;
    std::unique_ptr<Impl> impl;
};
