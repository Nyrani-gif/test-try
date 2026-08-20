// qc_issue_dock.h — Non-modal QC panel (replaces FinalReviewDialog)
// Phase 3 of SANAE_REVAMP_PLAN.md §4.2.1
//
// Non-modal: clicking an item activates the subtitle line and jumps video.
// Virtual list for scalability. No "my issues" filter (only device identity).

#pragma once

#include "sanae_issue_registry.h"
#include "sanae_qc_profile.h"
#include "sanae_user_role.h"
#include "workspace_mode.h"

#include <wx/panel.h>

#include <memory>
#include <vector>

class AssDialogue;
namespace agi { class Context; }
class wxListCtrl;
class wxStaticText;
class wxCheckBox;

class QCIssueDock : public wxPanel {
public:
    QCIssueDock(wxWindow *parent, agi::Context *context);
    ~QCIssueDock();

    // Called when diagnostics or issues change.
    void Refresh();

    // Called when workspace mode changes.
    void OnWorkspaceModeChanged(sanae::WorkspaceMode mode);

    // Called when user role changes.
    void OnUserRoleChanged(sanae::SanaeUserRole role);

    // F4 navigation: next problem based on role/mode.
    void NavigateNext(bool reverse = false);

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
