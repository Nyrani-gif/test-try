// project_navigator_dock.h — Non-modal project/episode navigation panel
// Phase 4.8 of SANAE_REVAMP_PLAN.md §4.4.2
//
// Replaces ProjectDialog as a non-modal dock.
// Shows seasons/projects/episodes tree.
// Navigation converges on existing episode/project state.

#pragma once

#include <wx/panel.h>

#include <memory>

namespace agi { class Context; }
class wxTreeCtrl;
class SanaeProjectManager;

class ProjectNavigatorDock : public wxPanel {
public:
    ProjectNavigatorDock(wxWindow *parent, agi::Context *context);
    ~ProjectNavigatorDock();

    void Refresh();

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
