// qc_issue_dock.cpp — implementation
// Phase 3 of SANAE_REVAMP_PLAN.md §4.2.1

#include "qc_issue_dock.h"

#include "ass_dialogue.h"
#include "format.h"
#include "include/aegisub/context.h"
#include "options.h"
#include "sanae_diagnostic.h"
#include "sanae_review_issue.h"
#include "sanae_ux_metrics.h"
#include "selection_controller.h"
#include "video_controller.h"

#include <libaegisub/log.h>

#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/checkbox.h>
#include <wx/listctrl.h>
#include <wx/button.h>

#include <algorithm>

struct QCIssueDock::Impl {
    agi::Context *context;
    sanae::WorkspaceMode mode = sanae::WorkspaceMode::Translation;
    sanae::SanaeUserRole role = sanae::SanaeUserRole::Translator;

    wxStaticText *summary;
    wxCheckBox *filter_critical;
    wxCheckBox *filter_open;
    wxListCtrl *list;
    wxStaticText *nav_hint;

    struct Entry {
        bool is_diagnostic;
        const sanae::SanaeDiagnostic* diagnostic;
        const sanae::SanaeReviewIssue* issue;
        AssDialogue *line;
    };
    std::vector<Entry> entries;

    Impl(wxWindow *parent, agi::Context *c) : context(c) {
        auto main = new wxBoxSizer(wxVERTICAL);

        summary = new wxStaticText(parent, -1, "");
        main->Add(summary, 0, wxEXPAND | wxALL, 4);

        auto filters = new wxBoxSizer(wxHORIZONTAL);
        filter_critical = new wxCheckBox(parent, -1, _("Critical"));
        filter_open = new wxCheckBox(parent, -1, _("Open"));
        filters->Add(filter_critical, 0, wxRIGHT, 8);
        filters->Add(filter_open, 0, wxRIGHT, 8);
        main->Add(filters, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 4);

        list = new wxListCtrl(parent, -1, wxDefaultPosition, wxDefaultSize,
                              wxLC_REPORT | wxLC_SINGLE_SEL | wxLC_VIRTUAL);
        list->InsertColumn(0, _("Type"), wxLIST_FORMAT_LEFT, 80);
        list->InsertColumn(1, _("Line"), wxLIST_FORMAT_RIGHT, 50);
        list->InsertColumn(2, _("Detail"), wxLIST_FORMAT_LEFT, 300);
        list->Bind(wxEVT_LIST_ITEM_ACTIVATED, [this](wxListEvent& evt) {
            NavigateToEntry(static_cast<size_t>(evt.GetIndex()));
        });
        main->Add(list, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 4);

        nav_hint = new wxStaticText(parent, -1, _("[F4] Next problem"));
        main->Add(nav_hint, 0, wxALL, 4);

        parent->SetSizer(main);
    }

    void NavigateToEntry(size_t index) {
        if (index >= entries.size()) return;
        auto *line = entries[index].line;
        if (!line) return;
        context->selectionController->SetSelectionAndActive({line}, line);
        context->videoController->JumpToTime(line->Start);
    }

    void NavigateNext(bool reverse) {
        if (entries.empty()) return;

        auto *active = context->selectionController->GetActiveLine();
        int current_idx = -1;
        for (size_t i = 0; i < entries.size(); ++i) {
            if (entries[i].line == active) {
                current_idx = static_cast<int>(i);
                break;
            }
        }

        // F4 navigation semantics:
        // Translator/Translation: next Open ReviewIssue or blocking Error Diagnostic
        // Reviewer/QC: first ReadyForReview; if none, Open
        int start = reverse ? current_idx - 1 : current_idx + 1;
        if (start < 0) start = static_cast<int>(entries.size()) - 1;
        if (start >= static_cast<int>(entries.size())) start = 0;

        for (int offset = 0; offset < static_cast<int>(entries.size()); ++offset) {
            int idx = (start + (reverse ? -offset : offset)) % static_cast<int>(entries.size());
            if (idx < 0) idx += static_cast<int>(entries.size());
            auto const& e = entries[static_cast<size_t>(idx)];
            if (e.line == active) continue;  // skip current

            if (role == sanae::SanaeUserRole::Reviewer) {
                // Reviewer: prefer ReadyForReview, then Open
                if (e.issue && e.issue->IsReadyForReview()) {
                    NavigateToEntry(static_cast<size_t>(idx));
                    return;
                }
            }
            // Translator or Reviewer fallback: Open issue or Error diagnostic
            if (e.issue && e.issue->IsOpen()) {
                NavigateToEntry(static_cast<size_t>(idx));
                return;
            }
            if (e.is_diagnostic && e.diagnostic->severity == sanae::DiagnosticSeverity::Error) {
                NavigateToEntry(static_cast<size_t>(idx));
                return;
            }
        }
    }
};

QCIssueDock::QCIssueDock(wxWindow *parent, agi::Context *context)
: wxPanel(parent)
, impl(std::make_unique<Impl>(this, context)) {
}

QCIssueDock::~QCIssueDock() = default;

void QCIssueDock::Refresh() {
    // TODO: populate entries from SanaeIssueRegistry when integrated.
    // For now, just update the summary.
    int open_count = 0;
    int ready_count = 0;
    int critical_count = 0;
    for (auto const& e : impl->entries) {
        if (e.is_diagnostic) {
            if (e.diagnostic->severity == sanae::DiagnosticSeverity::Error) ++critical_count;
        } else if (e.issue) {
            if (e.issue->IsOpen()) ++open_count;
            if (e.issue->IsReadyForReview()) ++ready_count;
        }
    }

    impl->summary->SetLabel(agi::wxformat(
        _("%d problems · %d open · %d waiting · %d critical"),
        static_cast<int>(impl->entries.size()), open_count, ready_count, critical_count));

    impl->list->SetItemCount(static_cast<long>(impl->entries.size()));
}

void QCIssueDock::OnWorkspaceModeChanged(sanae::WorkspaceMode mode) {
    impl->mode = mode;
    Refresh();
}

void QCIssueDock::OnUserRoleChanged(sanae::SanaeUserRole role) {
    impl->role = role;
    Refresh();
}

void QCIssueDock::NavigateNext(bool reverse) {
    impl->NavigateNext(reverse);
}
