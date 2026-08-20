// project_search_dock.h — Non-modal project memory search panel
// Phase 4.9 of SANAE_REVAMP_PLAN.md §4.4.2
//
// Replaces ProjectSearchDialog as a non-modal dock.
// Search results navigate through existing grid/selection APIs.

#pragma once

#include <wx/panel.h>

#include <memory>
#include <string>
#include <vector>

namespace agi { class Context; }
class wxTextCtrl;
class wxListCtrl;
struct SanaeRepeatMatch;

class ProjectSearchDock : public wxPanel {
public:
    ProjectSearchDock(wxWindow *parent, agi::Context *context);
    ~ProjectSearchDock();

    void Refresh();

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
