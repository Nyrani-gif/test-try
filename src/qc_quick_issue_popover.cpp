// qc_quick_issue_popover.cpp — implementation
// Phase 3 of SANAE_REVAMP_PLAN.md §4.2.3

#include "qc_quick_issue_popover.h"

#include "ass_dialogue.h"
#include "compat.h"
#include "format.h"
#include "include/aegisub/context.h"
#include "sanae_baseline_fingerprint.h"
#include "sanae_local_line_id.h"
#include "sanae_review_issue.h"
#include "sanae_ux_metrics.h"
#include "selection_controller.h"

#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/button.h>
#include <wx/panel.h>
#include <wx/choice.h>

#include <sstream>
#include <iomanip>

struct QuickIssuePopover::Impl {
    agi::Context *context;
    AssDialogue *line;
    wxChoice *type_choice;
    wxChoice *severity_choice;
    wxTextCtrl *body_input;

    Impl(wxWindow *parent, agi::Context *c, AssDialogue *l)
        : context(c), line(l) {

        auto panel = new wxPanel(parent);
        auto main = new wxBoxSizer(wxVERTICAL);

        main->Add(new wxStaticText(panel, -1, _("Create issue")), 0, wxBOTTOM, 6);

        auto fields = new wxFlexGridSizer(2, 5, 5);
        fields->Add(new wxStaticText(panel, -1, _("Type:")), 0, wxALIGN_CENTER_VERTICAL);

        wxString types[] = { _("Translation"), _("Terminology"), _("Timing"),
                             _("Style"), _("Formatting"), _("Other") };
        type_choice = new wxChoice(panel, -1, wxDefaultPosition, wxDefaultSize, 6, types);
        type_choice->SetSelection(0);
        fields->Add(type_choice, 1, wxEXPAND);

        fields->Add(new wxStaticText(panel, -1, _("Severity:")), 0, wxALIGN_CENTER_VERTICAL);
        wxString severities[] = { _("Info"), _("Warning"), _("Error") };
        severity_choice = new wxChoice(panel, -1, wxDefaultPosition, wxDefaultSize, 3, severities);
        severity_choice->SetSelection(1);  // Warning default
        fields->Add(severity_choice, 1, wxEXPAND);

        main->Add(fields, 0, wxEXPAND | wxBOTTOM, 6);

        main->Add(new wxStaticText(panel, -1, _("What's wrong?")), 0, wxBOTTOM, 2);
        body_input = new wxTextCtrl(panel, -1, "", wxDefaultPosition, wxSize(400, 60), wxTE_MULTILINE);
        main->Add(body_input, 1, wxEXPAND | wxBOTTOM, 8);

        auto buttons = new wxBoxSizer(wxHORIZONTAL);
        auto ok = new wxButton(panel, wxID_OK, _("Create"));
        auto cancel = new wxButton(panel, wxID_CANCEL, _("Cancel"));
        buttons->AddStretchSpacer();
        buttons->Add(cancel, 0, wxRIGHT, 4);
        buttons->Add(ok, 0);
        main->Add(buttons, 0, wxEXPAND);

        panel->SetSizer(main);
        main->Fit(panel);
        parent->SetSize(panel->GetSize());

        body_input->SetFocus();

        ok->Bind(wxEVT_BUTTON, [this, parent](wxCommandEvent&) {
            CreateIssue();
            parent->Dismiss();
        });
        cancel->Bind(wxEVT_BUTTON, [parent](wxCommandEvent&) {
            parent->Dismiss();
        });
    }

    void CreateIssue() {
        if (!line) return;

        sanae::SanaeReviewIssue issue;
        issue.local_line_id = "";  // assigned by persistence layer
        issue.kind = static_cast<sanae::ReviewIssueKind>(type_choice->GetSelection());
        issue.severity = static_cast<sanae::ReviewIssueSeverity>(severity_choice->GetSelection());
        issue.body = from_wx(body_input->GetValue());
        issue.state = sanae::ReviewIssueState::Open;
        issue.version = 1;

        // Capture baseline fingerprints at creation time.
        issue.baseline_text_hash = sanae::compute_text_hash(*line);
        issue.baseline_timing_hash = sanae::compute_timing_hash(*line);

        // Generate a simple UUID (timestamp-based for local-only).
        // Phase 6 will use server-generated UUIDs; local-only uses this.
        std::ostringstream ss;
        ss << "local-" << std::hex << std::time(nullptr) << "-" << std::rand();
        issue.id = ss.str();

        issue.created_by_device_id = "";  // set by persistence layer
        issue.created_at = "";  // set by persistence layer

        // TODO: Add to SanaeIssueRegistry + persist to sidecar.
        // For now, the issue is created but not persisted until the
        // persistence layer is wired in Phase 3.16.

        sanae::ux::qc_issue_interaction("create");
    }
};

QuickIssuePopover::QuickIssuePopover(wxWindow *parent, agi::Context *context, AssDialogue *line)
: wxPopupTransientWindow(parent, wxBORDER_SIMPLE | wxPU_CONTAINS_CONTROLS)
, impl(std::make_unique<Impl>(this, context, line)) {
    // Position near the active line in the grid.
    if (line) {
        auto pos = parent->GetScreenPosition();
        Position(pos + wxPoint(50, 50));
    }
}

QuickIssuePopover::~QuickIssuePopover() = default;
