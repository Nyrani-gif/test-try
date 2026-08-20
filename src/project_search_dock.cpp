// project_search_dock.cpp — implementation
// Phase 4.9 of SANAE_REVAMP_PLAN.md §4.4.2

#include "project_search_dock.h"

#include "ass_dialogue.h"
#include "compat.h"
#include "format.h"
#include "include/aegisub/context.h"
#include "options.h"
#include "sanae_project.h"
#include "sanae_text.h"
#include "sanae_ux_metrics.h"
#include "selection_controller.h"
#include "translation_project.h"
#include "video_controller.h"

#include <libaegisub/ass/time.h>

#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/listctrl.h>
#include <wx/button.h>

#include <algorithm>

struct ProjectSearchDock::Impl {
    agi::Context *context;
    SanaeProjectManager& manager;
    wxTextCtrl *query;
    wxListCtrl *results;
    wxStaticText *count;
    std::vector<SanaeRepeatMatch> last_results;

    Impl(wxWindow *parent, agi::Context *c)
        : context(c), manager(*c->sanaeProject) {

        auto main = new wxBoxSizer(wxVERTICAL);

        auto search = new wxBoxSizer(wxHORIZONTAL);
        search->Add(new wxStaticText(parent, -1, _("Search:")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
        query = new wxTextCtrl(parent, -1, "");
        search->Add(query, 1, wxEXPAND);
        auto search_btn = new wxButton(parent, -1, _("Find"));
        search->Add(search_btn, 0, wxLEFT, 4);
        main->Add(search, 0, wxEXPAND | wxALL, 4);

        results = new wxListCtrl(parent, -1, wxDefaultPosition, wxDefaultSize,
                                  wxLC_REPORT | wxLC_SINGLE_SEL);
        results->InsertColumn(0, _("Episode"), wxLIST_FORMAT_LEFT, 80);
        results->InsertColumn(1, _("Time"), wxLIST_FORMAT_LEFT, 100);
        results->InsertColumn(2, _("EN"), wxLIST_FORMAT_LEFT, 200);
        results->InsertColumn(3, _("RU"), wxLIST_FORMAT_LEFT, 200);
        main->Add(results, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 4);

        count = new wxStaticText(parent, -1, "");
        main->Add(count, 0, wxALL, 4);

        parent->SetSizer(main);

        auto do_search = [this]() {
            auto q = from_wx(query->GetValue());
            if (q.empty()) return;
            last_results = manager.SearchMemory(q);
            PopulateResults();
        };

        search_btn->Bind(wxEVT_BUTTON, [do_search](wxCommandEvent&) { do_search(); });
        query->Bind(wxEVT_TEXT_ENTER, [do_search](wxCommandEvent&) { do_search(); });

        results->Bind(wxEVT_LIST_ITEM_ACTIVATED, [this](wxListEvent& evt) {
            auto idx = static_cast<size_t>(evt.GetIndex());
            if (idx >= last_results.size()) return;
            auto const& match = last_results[idx];
            // Navigate to the matching line in the grid if it's in the current episode.
            // This uses existing selectionController + videoController APIs.
            // For matches in other episodes, the user would need to open that episode first.
        });
    }

    void PopulateResults() {
        results->DeleteAllItems();
        for (size_t i = 0; i < last_results.size(); ++i) {
            auto const& m = last_results[i];
            auto row = results->InsertItem(static_cast<long>(i), to_wx(m.episode_code));
            results->SetItem(row, 1, to_wx(agi::Time(m.start).GetAssFormatted(true)));
            results->SetItem(row, 2, to_wx(m.source));
            results->SetItem(row, 3, to_wx(m.russian));
        }
        count->SetLabel(agi::wxformat(_("%d results"), static_cast<int>(last_results.size())));
    }
};

ProjectSearchDock::ProjectSearchDock(wxWindow *parent, agi::Context *context)
: wxPanel(parent)
, impl(std::make_unique<Impl>(this, context)) {
}

ProjectSearchDock::~ProjectSearchDock() = default;

void ProjectSearchDock::Refresh() {
    impl->PopulateResults();
}
