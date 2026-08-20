// terminology_entry_popover.cpp — implementation
// Phase 2.8 of SANAE_REVAMP_PLAN.md §4.1.4

#include "terminology_entry_popover.h"

#include "ass_dialogue.h"
#include "compat.h"
#include "format.h"
#include "include/aegisub/context.h"
#include "options.h"
#include "sanae_project.h"
#include "sanae_text.h"
#include "subs_edit_ctrl.h"
#include "translation_project.h"

#include <libaegisub/util.h>

#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/button.h>
#include <wx/panel.h>

#include <algorithm>

struct TerminologyEntryPopover::Impl {
    agi::Context *context;
    SanaeProjectManager& manager;
    wxTextCtrl *english;
    wxTextCtrl *russian;
    wxTextCtrl *note;

    Impl(wxWindow *parent, agi::Context *c)
        : context(c), manager(*c->sanaeProject) {

        auto panel = new wxPanel(parent);
        auto main = new wxBoxSizer(wxVERTICAL);

        // Header
        main->Add(new wxStaticText(panel, -1, _("Add to terminology")), 0, wxBOTTOM, 6);

        auto fields = new wxFlexGridSizer(2, 7, 9);
        fields->AddGrowableCol(1, 1);

        fields->Add(new wxStaticText(panel, -1, _("English:")), 0, wxALIGN_CENTER_VERTICAL);
        fields->Add(english = new wxTextCtrl(panel, -1, ""), 1, wxEXPAND);
        fields->Add(new wxStaticText(panel, -1, _("Russian:")), 0, wxALIGN_CENTER_VERTICAL);
        fields->Add(russian = new wxTextCtrl(panel, -1, ""), 1, wxEXPAND);
        fields->Add(new wxStaticText(panel, -1, _("Note (optional):")), 0, wxALIGN_CENTER_VERTICAL);
        fields->Add(note = new wxTextCtrl(panel, -1, ""), 1, wxEXPAND);

        main->Add(fields, 0, wxEXPAND | wxBOTTOM, 8);

        auto buttons = new wxBoxSizer(wxHORIZONTAL);
        auto ok = new wxButton(panel, wxID_OK, _("Add"));
        auto cancel = new wxButton(panel, wxID_CANCEL, _("Cancel"));
        buttons->AddStretchSpacer();
        buttons->Add(cancel, 0, wxRIGHT, 4);
        buttons->Add(ok, 0);
        main->Add(buttons, 0, wxEXPAND);

        panel->SetSizer(main);
        main->Fit(panel);
        parent->SetSize(panel->GetSize());

        ok->Bind(wxEVT_BUTTON, [this, parent](wxCommandEvent&) {
            DoAdd();
            parent->Dismiss();
        });
        cancel->Bind(wxEVT_BUTTON, [parent](wxCommandEvent&) {
            parent->Dismiss();
        });
    }

    void Prefill(std::string const& en, std::string const& ru) {
        english->SetValue(to_wx(en));
        russian->SetValue(to_wx(ru));
        russian->SetFocus();
    }

    void DoAdd() {
        auto en = from_wx(english->GetValue().Trim());
        auto ru = from_wx(russian->GetValue().Trim());
        auto n = from_wx(note->GetValue().Trim());
        if (en.empty() || ru.empty()) return;
        try {
            manager.QueueTerminology({en, ru, n, "create", "", 0});
        }
        catch (std::exception const& e) {
            // Silently fail — the popover is transient. Errors are logged by the manager.
        }
    }
};

TerminologyEntryPopover::TerminologyEntryPopover(wxWindow *parent, agi::Context *context,
                                                   AssDialogue *active_line,
                                                   SubsTextEditCtrl *edit_ctrl)
: wxPopupTransientWindow(parent, wxBORDER_SIMPLE | wxPU_CONTAINS_CONTROLS)
, impl(std::make_unique<Impl>(this, context)) {

    // Prefill: EN from source text, RU from selection or full line.
    std::string en, ru;
    if (active_line && context->translationProject)
        en = context->translationProject->SourceDisplayText(active_line);
    if (edit_ctrl && active_line) {
        int start = edit_ctrl->GetSelectionStart();
        int end = edit_ctrl->GetSelectionEnd();
        auto const& raw = active_line->Text.get();
        start = std::clamp(start, 0, static_cast<int>(raw.size()));
        end = std::clamp(end, start, static_cast<int>(raw.size()));
        if (end > start)
            ru = raw.substr(static_cast<size_t>(start), static_cast<size_t>(end - start));
        else
            ru = active_line->GetStrippedText();
    }
    impl->Prefill(en, ru);
    PositionNearEditCtrl(parent, edit_ctrl);
}

TerminologyEntryPopover::TerminologyEntryPopover(wxWindow *parent, agi::Context *context,
                                                   std::string english_prefill,
                                                   std::string russian_prefill)
: wxPopupTransientWindow(parent, wxBORDER_SIMPLE | wxPU_CONTAINS_CONTROLS)
, impl(std::make_unique<Impl>(this, context)) {
    impl->Prefill(english_prefill, russian_prefill);
}

TerminologyEntryPopover::~TerminologyEntryPopover() = default;

void TerminologyEntryPopover::PositionNearEditCtrl(wxWindow *parent, SubsTextEditCtrl *edit_ctrl) {
    if (!edit_ctrl) return;
    auto pos = edit_ctrl->GetScreenPosition();
    auto size = edit_ctrl->GetSize();
    Position(pos + wxPoint(size.GetWidth() / 2, size.GetHeight()));
}
