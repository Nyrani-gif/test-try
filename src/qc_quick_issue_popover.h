// qc_quick_issue_popover.h — Quick issue creation popover
// Phase 3 of SANAE_REVAMP_PLAN.md §4.2.3

#pragma once

#include <wx/popupwin.h>

#include <memory>
#include <string>

class AssDialogue;
namespace agi { class Context; }
class wxTextCtrl;
class wxChoice;

class QuickIssuePopover : public wxPopupTransientWindow {
public:
    QuickIssuePopover(wxWindow *parent, agi::Context *context, AssDialogue *line);
    ~QuickIssuePopover();

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
