// project_navigator_dock.cpp — implementation
// Phase 4.8 of SANAE_REVAMP_PLAN.md §4.4.2

#include "project_navigator_dock.h"

#include "compat.h"
#include "format.h"
#include "include/aegisub/context.h"
#include "options.h"
#include "sanae_project.h"
#include "sanae_ux_metrics.h"
#include "translation_project.h"

#include <libaegisub/signal.h>

#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/treectrl.h>
#include <wx/button.h>

#include <algorithm>

struct ProjectNavigatorDock::Impl {
    agi::Context *context;
    SanaeProjectManager& manager;
    wxTreeCtrl *tree;
    std::vector<agi::signal::Connection> connections;

    Impl(wxWindow *parent, agi::Context *c)
        : context(c), manager(*c->sanaeProject) {

        auto main = new wxBoxSizer(wxVERTICAL);

        tree = new wxTreeCtrl(parent, -1, wxDefaultPosition, wxDefaultSize,
                              wxTR_DEFAULT_STYLE | wxTR_HIDE_ROOT);
        main->Add(tree, 1, wxEXPAND | wxALL, 4);

        auto buttons = new wxBoxSizer(wxHORIZONTAL);
        auto sync_btn = new wxButton(parent, -1, _("Sync"));
        auto close_btn = new wxButton(parent, -1, _("Detach"));
        buttons->Add(sync_btn, 0, wxRIGHT, 4);
        buttons->AddStretchSpacer();
        buttons->Add(close_btn, 0);
        main->Add(buttons, 0, wxEXPAND | wxALL, 4);

        parent->SetSizer(main);

        tree->Bind(wxEVT_TREE_ITEM_ACTIVATED, [this](wxTreeEvent& evt) {
            // Navigate to selected episode/project.
            // Uses existing manager.AttachEpisode / project open.
        });

        connections = agi::signal::make_vector({
            manager.AddChangeListener([this](SanaeProjectChange what) {
                if (what == SanaeProjectChange::Cache || what == SanaeProjectChange::Binding)
                    Populate();
            }),
        });
    }

    void Populate() {
        tree->DeleteAllItems();
        auto root = tree->AddRoot("Projects");

        for (auto const& season : manager.Seasons()) {
            auto season_item = tree->AppendItem(root, to_wx(season.display_name));
            for (auto const& project : manager.Projects()) {
                if (project.season_id != season.id) continue;
                auto project_item = tree->AppendItem(season_item, to_wx(project.name));
                for (auto const& episode : manager.Episodes()) {
                    if (episode.project_id != project.id || episode.IsDeleted()) continue;
                    auto label = to_wx(episode.episode_code);
                    if (episode.id == manager.ActiveEpisodeId())
                        label += " ✓";
                    tree->AppendItem(project_item, label);
                }
            }
        }

        tree->Expand(root);
    }
};

ProjectNavigatorDock::ProjectNavigatorDock(wxWindow *parent, agi::Context *context)
: wxPanel(parent)
, impl(std::make_unique<Impl>(this, context)) {
    impl->Populate();
}

ProjectNavigatorDock::~ProjectNavigatorDock() = default;

void ProjectNavigatorDock::Refresh() {
    impl->Populate();
}
