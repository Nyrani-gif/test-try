// terminology_dock.cpp — implementation
// Phase 4.7 of SANAE_REVAMP_PLAN.md §4.4.2
//
// Reuses existing SanaeProjectManager terminology data.
// Old TerminologyDialog remains as fallback until parity is confirmed.

#include "terminology_dock.h"

#include "ass_dialogue.h"
#include "compat.h"
#include "format.h"
#include "include/aegisub/context.h"
#include "options.h"
#include "sanae_project.h"
#include "sanae_text.h"
#include "sanae_ux_metrics.h"
#include "translation_project.h"

#include <libaegisub/signal.h>

#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/listctrl.h>
#include <wx/button.h>

#include <algorithm>

struct TerminologyDock::Impl {
    agi::Context *context;
    SanaeProjectManager& manager;
    wxTextCtrl *filter;
    wxListCtrl *list;
    wxStaticText *count_label;
    std::vector<agi::signal::Connection> connections;

    Impl(wxWindow *parent, agi::Context *c)
        : context(c), manager(*c->sanaeProject) {

        auto main = new wxBoxSizer(wxVERTICAL);

        auto search = new wxBoxSizer(wxHORIZONTAL);
        search->Add(new wxStaticText(parent, -1, _("Search:")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
        filter = new wxTextCtrl(parent, -1, "");
        search->Add(filter, 1, wxEXPAND);
        main->Add(search, 0, wxEXPAND | wxALL, 4);

        list = new wxListCtrl(parent, -1, wxDefaultPosition, wxDefaultSize,
                              wxLC_REPORT | wxLC_SINGLE_SEL);
        list->InsertColumn(0, _("English"), wxLIST_FORMAT_LEFT, 180);
        list->InsertColumn(1, _("Russian"), wxLIST_FORMAT_LEFT, 180);
        list->InsertColumn(2, _("Note"), wxLIST_FORMAT_LEFT, 200);
        list->InsertColumn(3, _("Status"), wxLIST_FORMAT_LEFT, 100);
        main->Add(list, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 4);

        count_label = new wxStaticText(parent, -1, "");
        main->Add(count_label, 0, wxALL, 4);

        auto buttons = new wxBoxSizer(wxHORIZONTAL);
        auto add_btn = new wxButton(parent, -1, _("Add term…"));
        auto edit_btn = new wxButton(parent, -1, _("Edit…"));
        auto delete_btn = new wxButton(parent, -1, _("Delete"));
        buttons->Add(add_btn, 0, wxRIGHT, 4);
        buttons->Add(edit_btn, 0, wxRIGHT, 4);
        buttons->Add(delete_btn, 0);
        main->Add(buttons, 0, wxALL, 4);

        parent->SetSizer(main);

        filter->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { Populate(); });
        add_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            sanae::ux::modal_opened("terminology_dock_add");
            // Opens TerminologyEntryPopover or ShowSanaeTerminologyEntryDialog.
        });
        edit_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            // Opens TermEditDialog for selected term.
        });

        connections = agi::signal::make_vector({
            manager.AddChangeListener([this](SanaeProjectChange what) {
                if (what == SanaeProjectChange::Cache || what == SanaeProjectChange::Draft)
                    Populate();
            }),
        });
    }

    void Populate() {
        auto normalized_filter = SanaeNormalizeSource(from_wx(filter->GetValue()));
        list->DeleteAllItems();

        auto const& terms = manager.Terminology();
        auto const& drafts = manager.TerminologyDrafts();
        int count = 0;

        for (auto const& term : terms) {
            if (term.deleted) continue;
            auto norm = term.english_normalized.empty()
                ? SanaeNormalizeSource(term.english) : term.english_normalized;
            if (!normalized_filter.empty() && norm.find(normalized_filter) == std::string::npos)
                continue;
            auto row = list->InsertItem(list->GetItemCount(), to_wx(term.english));
            list->SetItem(row, 1, to_wx(term.russian));
            list->SetItem(row, 2, to_wx(term.note));
            list->SetItem(row, 3, _("Active"));
            ++count;
        }

        for (auto const& draft : drafts) {
            if (draft.operation == "delete") continue;
            auto norm = SanaeNormalizeSource(draft.english);
            if (!normalized_filter.empty() && norm.find(normalized_filter) == std::string::npos)
                continue;
            auto row = list->InsertItem(list->GetItemCount(), to_wx(draft.english));
            list->SetItem(row, 1, to_wx(draft.russian));
            list->SetItem(row, 2, to_wx(draft.note));
            list->SetItem(row, 3, draft.operation == "create" ? _("Queued") : _("Updated"));
            ++count;
        }

        count_label->SetLabel(agi::wxformat(_("%d terms"), count));
    }
};

TerminologyDock::TerminologyDock(wxWindow *parent, agi::Context *context)
: wxPanel(parent)
, impl(std::make_unique<Impl>(this, context)) {
    impl->Populate();
}

TerminologyDock::~TerminologyDock() = default;

void TerminologyDock::Refresh() {
    impl->Populate();
}
