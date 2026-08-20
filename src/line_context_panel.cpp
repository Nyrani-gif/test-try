// line_context_panel.cpp — implementation
// Phase 2 of SANAE_REVAMP_PLAN.md §3.2

#include "line_context_panel.h"

#include "ass_dialogue.h"
#include "include/aegisub/context.h"
#include "options.h"
#include "subs_edit_box.h"
#include "terminology_hint_panel.h"

#include <wx/sizer.h>
#include <wx/stattext.h>

#include <memory>

struct LineContextPanel::Impl {
    agi::Context *context;
    SubsTextEditCtrl *edit_ctrl;
    TerminologyHintPanel *term_panel;
    wxBoxSizer *main_sizer;

    Impl(wxWindow *parent, agi::Context *c, SubsTextEditCtrl *e)
        : context(c), edit_ctrl(e) {

        main_sizer = new wxBoxSizer(wxVERTICAL);

        // Phase 2: only terminology hints. Phase 3 adds ReviewIssue/Diagnostic sections.
        term_panel = new TerminologyHintPanel(parent, c, e);
        main_sizer->Add(term_panel, 0, wxEXPAND | wxALL, 2);
    }
};

LineContextPanel::LineContextPanel(wxWindow *parent, agi::Context *context,
                                     SubsTextEditCtrl *edit_ctrl)
: wxPanel(parent)
, impl(std::make_unique<Impl>(this, context, edit_ctrl)) {
    SetSizer(impl->main_sizer);
    impl->main_sizer->Fit(this);
    Hide();  // collapsed by default; shown when content arrives
}

LineContextPanel::~LineContextPanel() = default;

void LineContextPanel::OnActiveLineChanged(AssDialogue *new_line) {
    if (!OPT_GET("Sanae/InlineTerminology")->GetBool()) {
        Hide();
        return;
    }
    impl->term_panel->OnActiveLineChanged(new_line);

    // Show/hide based on whether term panel has content.
    if (impl->term_panel->IsShown())
        Show();
    else
        Hide();

    GetParent()->Layout();
}

void LineContextPanel::OnTextChanged() {
    if (!OPT_GET("Sanae/InlineTerminology")->GetBool()) return;
    impl->term_panel->OnTextChanged();
}
