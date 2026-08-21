// terminology_hint_panel.cpp — implementation
// Phase 2 of SANAE_REVAMP_PLAN.md §3.3

#include "terminology_hint_panel.h"

#include "ass_dialogue.h"
#include "format.h"
#include "include/aegisub/context.h"
#include "options.h"
#include "sanae_project.h"
#include "sanae_text.h"
#include "sanae_ux_metrics.h"
#include "subs_edit_box.h"
#include "subs_edit_ctrl.h"
#include "compat.h"
#include "translation_project.h"

#include <libaegisub/signal.h>
#include <libaegisub/util.h>

#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/button.h>
#include <wx/hyperlink.h>

#include <algorithm>

struct TerminologyHintPanel::Impl {
    TerminologyHintPanel *owner;  // the actual wxPanel
    agi::Context *context;
    SubsTextEditCtrl *edit_ctrl;
    SanaeProjectManager& manager;
    sanae::SanaeTerminologyIndex index;

    // Cached matches for the current line (heavy search result).
    std::vector<sanae::TerminologyMatch> cached_matches;

    // Current line (for applying terms).
    AssDialogue *active_line = nullptr;

    // UI elements
    wxBoxSizer *main_sizer;
    wxStaticText *header;
    wxFlexGridSizer *term_grid;

    // Signal connections
    std::vector<agi::signal::Connection> connections;

    Impl(TerminologyHintPanel *panel, agi::Context *c, SubsTextEditCtrl *e)
        : owner(panel), context(c), edit_ctrl(e), manager(*c->sanaeProject) {

        main_sizer = new wxBoxSizer(wxVERTICAL);
        header = new wxStaticText(owner, -1, "");
        main_sizer->Add(header, 0, wxBOTTOM, 4);

        term_grid = new wxFlexGridSizer(2, 4, 4);
        term_grid->AddGrowableCol(1, 1);
        main_sizer->Add(term_grid, 0, wxEXPAND);

        // Rebuild index when terminology changes.
        // SanaeProjectChange::Cache — fired on snapshot load, project sync,
        //   episode create/attach/detach, source replace, finalize.
        //   Terminology vector is fully replaced.
        // SanaeProjectChange::Draft — fired on every terminology queue
        //   operation (add/edit/delete/restore) and ignore queue operation.
        //   Drafts vector changes; terminology vector may change on finalize.
        // Both require index rebuild because the index merges terms + drafts.
        connections = agi::signal::make_vector({
            manager.AddChangeListener([this](SanaeProjectChange what) {
                if (what == SanaeProjectChange::Cache
                    || what == SanaeProjectChange::Draft)
                    RebuildIndex();
            }),
        });
        RebuildIndex();
    }

    void RebuildIndex() {
        std::vector<sanae::TerminologyIndexEntry> entries;
        auto const& terms = manager.Terminology();
        auto const& drafts = manager.TerminologyDrafts();

        entries.reserve(terms.size() + drafts.size());
        for (auto const& term : terms) {
            if (term.deleted) continue;
            sanae::TerminologyIndexEntry e;
            e.term_id = term.id;
            e.english = term.english;
            e.english_normalized = term.english_normalized.empty()
                ? SanaeNormalizeSource(term.english)
                : term.english_normalized;
            e.russian = term.russian;
            e.note = term.note;
            e.is_phrase = e.english_normalized.find(' ') != std::string::npos;
            entries.push_back(std::move(e));
        }
        for (auto const& draft : drafts) {
            if (draft.operation == "delete") continue;
            sanae::TerminologyIndexEntry e;
            e.term_id = draft.term_id;  // empty for "create"
            e.english = draft.english;
            e.english_normalized = SanaeNormalizeSource(draft.english);
            e.russian = draft.russian;
            e.note = draft.note;
            e.is_phrase = e.english_normalized.find(' ') != std::string::npos;
            entries.push_back(std::move(e));
        }
        index.Rebuild(entries);
    }

    void DoSearch(AssDialogue *line) {
        active_line = line;
        cached_matches.clear();

        if (!line || !context->sanaeProject->HasOpenEpisode()) {
            UpdateUI();
            return;
        }

        // Get EN source text for this line.
        std::string en_source;
        if (context->translationProject)
            en_source = context->translationProject->SourceDisplayTextCached(line);
        if (en_source.empty()) {
            UpdateUI();
            return;
        }

        // Normalize and search.
        std::string normalized = SanaeNormalizeSource(en_source);
        cached_matches = index.Search(normalized);

        // Truncate to top 5.
        if (cached_matches.size() > 5)
            cached_matches.resize(5);

        // Initial usage check (in case there's already RU text).
        DoUsageCheck();

        UpdateUI();
    }

    void DoUsageCheck() {
        if (cached_matches.empty() || !edit_ctrl) return;
        std::string ru_text = edit_ctrl->GetTextRaw().data();
        sanae::SanaeTerminologyIndex::UpdateUsage(cached_matches, ru_text);
        UpdateUI();
    }

    void ApplyTerm(size_t index) {
        if (index >= cached_matches.size()) return;
        auto const& match = cached_matches[index];
        sanae::ux::terminology_inline_applied();

        // Insert the Russian translation at cursor position in edit_ctrl.
        // If there is a selection, replace it. Otherwise insert at cursor.
        if (!edit_ctrl || !active_line) return;

        int sel_start = edit_ctrl->GetSelectionStart();
        int sel_end = edit_ctrl->GetSelectionEnd();

        if (sel_start != sel_end) {
            // Replace selection with the Russian term.
            edit_ctrl->SetTargetStart(sel_start);
            edit_ctrl->SetTargetEnd(sel_end);
            edit_ctrl->ReplaceTarget(to_wx(match.russian));
        } else {
            // Insert at cursor position.
            int cursor = edit_ctrl->GetInsertionPoint();
            edit_ctrl->InsertText(cursor, to_wx(match.russian));
        }

        // The InsertText/ReplaceTarget triggers wxEVT_STC_MODIFIED, which
        // flows through SubsEditBox::OnChange → CommitText → AssFile::Commit.
        // This is the normal existing path — no manual commit needed.
        //
        // The OnChange handler also calls line_context_panel->OnTextChanged(),
        // which triggers the LIGHT usage check only (UpdateUsage). The HEAVY
        // search (Aho-Corasick) is NOT rerun — it only fires on
        // OnActiveLineChanged, not on text change.
    }

    void UpdateUI() {
        // Clear existing grid items.
        while (term_grid->GetItemCount() > 0) {
            auto item = term_grid->GetItem(static_cast<size_t>(0));
            term_grid->Detach(item->GetWindow());
            item->GetWindow()->Destroy();
        }

        if (cached_matches.empty()) {
            header->SetLabel("");
            owner->Hide();
            sanae::ux::line_context_hidden();
            return;
        }

        // Log impression (Phase 2 KPI: suggestion_impression + top3 flag).
        // line_ref passed for correlation with external user feedback studies.
        sanae::ux::terminology_suggestion_impression(
            static_cast<int>(cached_matches.size()),
            cached_matches.size() <= 3,  // top3 if 3 or fewer shown
            active_line ? std::to_string(active_line->Id) : "");

        // Header
        header->SetLabel(agi::wxformat(_("Terms (%d):"), static_cast<int>(cached_matches.size())));

        // Term rows
        for (size_t i = 0; i < cached_matches.size(); ++i) {
            auto const& m = cached_matches[i];

            // Left: term text with usage indicator
            wxString label = to_wx(m.english + " → " + m.russian);
            if (m.usage == sanae::TerminologyUsage::CorrectlyUsed)
                label = "✓ " + label;

            auto *term_label = new wxStaticText(owner, -1, label);
            term_grid->Add(term_label, 1, wxALIGN_CENTER_VERTICAL);

            // Right: apply button (click-to-apply, no hotkey)
            auto *apply_btn = new wxButton(owner, -1, _("Apply"));
            apply_btn->Bind(wxEVT_BUTTON, [this, i](wxCommandEvent&) {
                ApplyTerm(i);
            });
            term_grid->Add(apply_btn, 0, wxALIGN_CENTER_VERTICAL);
        }

        owner->Show();
        sanae::ux::line_context_shown();
        owner->Layout();
    }
};

TerminologyHintPanel::TerminologyHintPanel(wxWindow *parent, agi::Context *context,
                                             SubsTextEditCtrl *edit_ctrl)
: wxPanel(parent)
, impl(std::make_unique<Impl>(this, context, edit_ctrl)) {
    SetSizer(impl->main_sizer);
    impl->main_sizer->Fit(this);
    Hide();  // hidden until there are matches
}

TerminologyHintPanel::~TerminologyHintPanel() = default;

void TerminologyHintPanel::OnActiveLineChanged(AssDialogue *line) {
    if (!OPT_GET("Sanae/InlineTerminology")->GetBool()) {
        Hide();
        return;
    }
    impl->DoSearch(line);
}

void TerminologyHintPanel::OnTextChanged() {
    if (!OPT_GET("Sanae/InlineTerminology")->GetBool()) return;
    if (!IsShown()) return;
    impl->DoUsageCheck();
}
