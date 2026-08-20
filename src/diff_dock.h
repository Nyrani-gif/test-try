// diff_dock.h — Non-modal semantic diff panel
// Phase 4.9 of SANAE_REVAMP_PLAN.md §4.4.2
//
// Replaces SemanticDiffDialog as a non-modal dock.
// Reuses existing SanaeSemanticDiff computation — no second diff engine.

#pragma once

#include <wx/panel.h>

#include <memory>
#include <string>

namespace agi { class Context; }
class wxListCtrl;
class wxChoice;

class DiffDock : public wxPanel {
public:
    DiffDock(wxWindow *parent, agi::Context *context);
    ~DiffDock();

    void Refresh();

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
