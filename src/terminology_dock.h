// terminology_dock.h — Non-modal glossary management panel
// Phase 4.7 of SANAE_REVAMP_PLAN.md §4.4.2
//
// Replaces TerminologyDialog as a non-modal dock for Advanced mode.
// Does NOT conflict with inline TerminologyHintPanel (which is for
// current-line top 3-5 suggestions in Translation mode).
// TerminologyDock is for full glossary/project management.

#pragma once

#include <wx/panel.h>

#include <memory>

namespace agi { class Context; }
class wxListCtrl;
class wxTextCtrl;
class wxStaticText;
class SanaeProjectManager;

class TerminologyDock : public wxPanel {
public:
    TerminologyDock(wxWindow *parent, agi::Context *context);
    ~TerminologyDock();

    void Refresh();

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
