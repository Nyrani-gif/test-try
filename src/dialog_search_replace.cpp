// Copyright (c) 2013, Thomas Goyne <plorkyeran@aegisub.org>
//
// Permission to use, copy, modify, and distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
//
// Aegisub Project http://www.aegisub.org/

/// @file dialog_search_replace.cpp
/// @brief Find and Search/replace dialogue box and logic
/// @ingroup secondary_ui
///

#include "dialog_search_replace.h"

#include "compat.h"
#include "dialog_manager.h"
#include "format.h"
#include "include/aegisub/context.h"
#include "options.h"
#include "search_replace_engine.h"
#include "sanae_project.h"
#include "selection_controller.h"
#include "translation_project.h"
#include "utils.h"
#include "validators.h"


#include <functional>
#include <algorithm>
#include <string>
#include <vector>

#include <libaegisub/ass/time.h>

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/combobox.h>
#include <wx/choice.h>
#include <wx/radiobox.h>
#include <wx/listctrl.h>
#include <wx/msgdlg.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/valgen.h>
#include <wx/utils.h>

struct SanaeFindProjectState {
	wxChoice *where = nullptr;
	wxPanel *panel = nullptr;
	wxChoice *scope = nullptr;
	wxCheckBox *fuzzy = nullptr;
	wxChoice *episode = nullptr;
	wxListCtrl *results = nullptr;
	wxTextCtrl *details = nullptr;
	std::vector<SanaeRepeatMatch> matches;
	std::vector<std::string> episode_codes;
};

template<bool has_replace>
DialogSearchReplace<has_replace>::~DialogSearchReplace() = default;

template<bool has_replace>
DialogSearchReplace<has_replace>::DialogSearchReplace(agi::Context* c)
: wxDialog(c->parent, -1, has_replace ? _("Replace") : _("Find"))
, c(c)
, settings(std::make_unique<SearchReplaceSettings>())
{
	auto recent_find(lagi_MRU_wxAS("Find"));
	auto recent_replace(lagi_MRU_wxAS("Replace"));

	settings->field = static_cast<SearchReplaceSettings::Field>(OPT_GET("Tool/Search Replace/Field")->GetInt());
	settings->limit_to = static_cast<SearchReplaceSettings::Limit>(OPT_GET("Tool/Search Replace/Affect")->GetInt());
	settings->find = recent_find.empty() ? std::string() : from_wx(recent_find.front());
	settings->replace_with = recent_replace.empty() ? std::string() : from_wx(recent_replace.front());
	settings->match_case = OPT_GET("Tool/Search Replace/Match Case")->GetBool();
	settings->use_regex = OPT_GET("Tool/Search Replace/RegExp")->GetBool();
	settings->ignore_comments = OPT_GET("Tool/Search Replace/Skip Comments")->GetBool();
	settings->skip_tags = OPT_GET("Tool/Search Replace/Skip Tags")->GetBool();
	settings->exact_match = false;

	auto find_sizer = new wxFlexGridSizer(2, 2, 5, 15);
	find_edit = new wxComboBox(this, -1, "", wxDefaultPosition, wxSize(300, -1), recent_find, wxCB_DROPDOWN | wxTE_PROCESS_ENTER, StringBinder(&settings->find));
	find_edit->SetMaxLength(0);
	find_sizer->Add(new wxStaticText(this, -1, _("Find what:")), wxSizerFlags().Center().Left());
	find_sizer->Add(find_edit);

	if (has_replace) {
		replace_edit = new wxComboBox(this, -1, "", wxDefaultPosition, wxSize(300, -1), lagi_MRU_wxAS("Replace"), wxCB_DROPDOWN | wxTE_PROCESS_ENTER, StringBinder(&settings->replace_with));
		replace_edit->SetMaxLength(0);
		find_sizer->Add(new wxStaticText(this, -1, _("Replace with:")), wxSizerFlags().Center().Left());
		find_sizer->Add(replace_edit);
	}

	if constexpr (!has_replace) {
		if (c->sanaeProject && !c->sanaeProject->ActiveProjectId().empty()) {
			sanae_project_search = std::make_unique<SanaeFindProjectState>();
			auto where_row = new wxBoxSizer(wxHORIZONTAL);
			where_row->Add(new wxStaticText(this, -1, _("Where to search:")),
				0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
			sanae_project_search->where = new wxChoice(this, -1, wxDefaultPosition, wxDefaultSize,
				wxArrayString{_("Current file"), _("Whole project")});
			sanae_project_search->where->SetSelection(0);
			where_row->Add(sanae_project_search->where, 1);
			find_sizer->Add(new wxStaticText(this, -1, wxString()));
			find_sizer->Add(where_row, 1, wxEXPAND);
		}
	}

	auto options_sizer = new wxBoxSizer(wxVERTICAL);
	options_sizer->Add(new wxCheckBox(this, -1, _("&Match case"), wxDefaultPosition, wxDefaultSize, 0, wxGenericValidator(&settings->match_case)), wxSizerFlags().Border(wxBOTTOM));
	options_sizer->Add(new wxCheckBox(this, -1, _("&Use regular expressions"), wxDefaultPosition, wxDefaultSize, 0, wxGenericValidator(&settings->use_regex)), wxSizerFlags().Border(wxBOTTOM));
	options_sizer->Add(new wxCheckBox(this, -1, _("&Skip Comments"), wxDefaultPosition, wxDefaultSize, 0, wxGenericValidator(&settings->ignore_comments)), wxSizerFlags().Border(wxBOTTOM));
	options_sizer->Add(new wxCheckBox(this, -1, _("S&kip Override Tags"), wxDefaultPosition, wxDefaultSize, 0, wxGenericValidator(&settings->skip_tags)));

	auto left_sizer = new wxBoxSizer(wxVERTICAL);
	left_sizer->Add(find_sizer, wxSizerFlags().DoubleBorder(wxBOTTOM));
	left_sizer->Add(options_sizer);

	wxString field[] = { _("&Text"), _("St&yle"), _("A&ctor"), _("&Effect") };
	wxString affect[] = { _("A&ll rows"), _("Selected &rows") };
	auto limit_sizer = new wxBoxSizer(wxHORIZONTAL);
	limit_sizer->Add(new wxRadioBox(this, -1, _("In Field"), wxDefaultPosition, wxDefaultSize, std::size(field), field, 0, wxRA_SPECIFY_COLS, MakeEnumBinder(&settings->field)), wxSizerFlags().Border(wxRIGHT));
	limit_sizer->Add(new wxRadioBox(this, -1, _("Limit to"), wxDefaultPosition, wxDefaultSize, std::size(affect), affect, 0, wxRA_SPECIFY_COLS, MakeEnumBinder(&settings->limit_to)));

	auto find_next = new wxButton(this, -1, _("&Find next"));
	auto replace_next = new wxButton(this, -1, _("Replace &next"));
	auto replace_all = new wxButton(this, -1, _("Replace &all"));
	find_next->SetDefault();

	auto button_sizer = new wxBoxSizer(wxVERTICAL);
	button_sizer->Add(find_next, wxSizerFlags().Border(wxBOTTOM));
	button_sizer->Add(replace_next, wxSizerFlags().Border(wxBOTTOM));
	button_sizer->Add(replace_all, wxSizerFlags().Border(wxBOTTOM));
	button_sizer->Add(new wxButton(this, wxID_CANCEL));

	if (!has_replace) {
		button_sizer->Hide(replace_next);
		button_sizer->Hide(replace_all);
	}

	auto top_sizer = new wxBoxSizer(wxHORIZONTAL);
	top_sizer->Add(left_sizer, wxSizerFlags().Border());
	top_sizer->Add(button_sizer, wxSizerFlags().Border());

	auto main_sizer = new wxBoxSizer(wxVERTICAL);
	main_sizer->Add(top_sizer);
	main_sizer->Add(limit_sizer, wxSizerFlags().Border());
	if constexpr (!has_replace) {
		if (sanae_project_search) {
			auto panel = sanae_project_search->panel = new wxPanel(this);
			auto ps = new wxBoxSizer(wxVERTICAL);
			auto options = new wxBoxSizer(wxHORIZONTAL);
			options->Add(new wxStaticText(panel, -1, _("Search in:")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
			sanae_project_search->scope = new wxChoice(panel, -1, wxDefaultPosition, wxDefaultSize,
				wxArrayString{_("Russian subtitles"), _("English subtitles"), _("Both")});
			sanae_project_search->scope->SetSelection(0);
			options->Add(sanae_project_search->scope, 0, wxRIGHT, 12);
			sanae_project_search->fuzzy = new wxCheckBox(panel, -1, _("Find similar word forms"));
			options->Add(sanae_project_search->fuzzy, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 12);
			options->Add(new wxStaticText(panel, -1, _("Episodes:")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
			sanae_project_search->episode = new wxChoice(panel, -1);
			sanae_project_search->episode->Append(_("All episodes"));
			sanae_project_search->episode->SetSelection(0);
			std::vector<std::string> codes;
			for (auto const& value : c->sanaeProject->Episodes())
				if (!value.IsDeleted() && value.project_id == c->sanaeProject->ActiveProjectId())
					codes.push_back(value.episode_code);
			std::sort(codes.begin(), codes.end());
			codes.erase(std::unique(codes.begin(), codes.end()), codes.end());
			for (auto const& code : codes) {
				sanae_project_search->episode->Append(to_wx(code));
				sanae_project_search->episode_codes.push_back(code);
			}
			options->Add(sanae_project_search->episode, 0);
			ps->Add(options, 0, wxEXPAND | wxBOTTOM, 8);
			sanae_project_search->results = new wxListCtrl(panel, -1, wxDefaultPosition, wxSize(860, 260),
				wxLC_REPORT | wxLC_SINGLE_SEL);
			sanae_project_search->results->AppendColumn(_("Episode"), wxLIST_FORMAT_LEFT, 85);
			sanae_project_search->results->AppendColumn(_("Time"), wxLIST_FORMAT_LEFT, 100);
			sanae_project_search->results->AppendColumn(_("English subtitles"), wxLIST_FORMAT_LEFT, 300);
			sanae_project_search->results->AppendColumn(_("Russian subtitles"), wxLIST_FORMAT_LEFT, 300);
			ps->Add(sanae_project_search->results, 1, wxEXPAND | wxBOTTOM, 8);
			sanae_project_search->details = new wxTextCtrl(panel, -1, wxString(), wxDefaultPosition, wxSize(-1, 105),
				wxTE_MULTILINE | wxTE_READONLY);
			ps->Add(sanae_project_search->details, 0, wxEXPAND);
			panel->SetSizer(ps);
			main_sizer->Add(panel, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);
			panel->Hide();
		}
	}
	SetSizerAndFit(main_sizer);
	CenterOnParent();

	TransferDataToWindow();
	find_edit->SetFocus();
	find_edit->SelectAll();

	find_edit->Bind(wxEVT_TEXT_ENTER, [this](wxCommandEvent&) {
		if constexpr (!has_replace) {
			if (sanae_project_search && sanae_project_search->where->GetSelection() == 1) { RunSanaeProjectSearch(); return; }
		}
		FindReplace(&SearchReplaceEngine::FindNext);
	});
	if (has_replace)
	  replace_edit->Bind(wxEVT_TEXT_ENTER, std::bind(&DialogSearchReplace::FindReplace, this, &SearchReplaceEngine::ReplaceNext));
	find_next->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
		if constexpr (!has_replace) {
			if (sanae_project_search && sanae_project_search->where->GetSelection() == 1) { RunSanaeProjectSearch(); return; }
		}
		FindReplace(&SearchReplaceEngine::FindNext);
	});
	replace_next->Bind(wxEVT_BUTTON, std::bind(&DialogSearchReplace::FindReplace, this, &SearchReplaceEngine::ReplaceNext));
	replace_all->Bind(wxEVT_BUTTON, std::bind(&DialogSearchReplace::FindReplace, this, &SearchReplaceEngine::ReplaceAll));
	if constexpr (!has_replace) {
		if (sanae_project_search) {
			sanae_project_search->where->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { UpdateSanaeSearchMode(); });
			sanae_project_search->results->Bind(wxEVT_LIST_ITEM_SELECTED, [this](wxListEvent&) { ShowSanaeSearchSelected(); });
			sanae_project_search->results->Bind(wxEVT_LIST_ITEM_ACTIVATED, [this](wxListEvent&) { NavigateSanaeSearchSelected(); });
		}
	}
}

template<bool has_replace>
void DialogSearchReplace<has_replace>::UpdateSanaeSearchMode() {
	if (!sanae_project_search) return;
	bool project = sanae_project_search->where->GetSelection() == 1;
	sanae_project_search->panel->Show(project);
	if (project) {
		SetMinSize(wxSize(900, 560));
		if (GetSize().GetWidth() < 900 || GetSize().GetHeight() < 560) SetSize(wxSize(930, 640));
	}
	else {
		SetMinSize(wxDefaultSize);
		Fit();
	}
	Layout();
	CentreOnParent();
}

template<bool has_replace>
void DialogSearchReplace<has_replace>::RunSanaeProjectSearch() {
	if (!sanae_project_search || !c->sanaeProject) return;
	TransferDataFromWindow();
	if (settings->find.empty()) return;
	SanaeSearchOptions options;
	options.query = settings->find;
	int selected_scope = sanae_project_search->scope->GetSelection();
	options.scope = selected_scope == 0 ? SanaeSearchScope::Russian
		: selected_scope == 1 ? SanaeSearchScope::English : SanaeSearchScope::All;
	options.fuzzy_word_forms = sanae_project_search->fuzzy->GetValue();
	int selected_episode = sanae_project_search->episode->GetSelection();
	if (selected_episode > 0 && static_cast<std::size_t>(selected_episode - 1) < sanae_project_search->episode_codes.size())
		options.episode_code = sanae_project_search->episode_codes[static_cast<std::size_t>(selected_episode - 1)];
	wxBusyCursor busy;
	sanae_project_search->matches = c->sanaeProject->SearchMemory(options);
	auto list = sanae_project_search->results;
	list->DeleteAllItems();
	for (auto const& value : sanae_project_search->matches) {
		long row = list->InsertItem(list->GetItemCount(), to_wx(value.episode_code));
		list->SetItem(row, 1, to_wx(agi::Time(value.start).GetAssFormatted(true)));
		list->SetItem(row, 2, to_wx(value.source));
		list->SetItem(row, 3, to_wx(value.russian.empty() ? "—" : value.russian));
	}
	config::mru->Add("Find", settings->find);
	UpdateDropDowns();
	if (sanae_project_search->matches.empty())
		sanae_project_search->details->ChangeValue(_("No matches were found in the project."));
	else {
		list->SetItemState(0, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
		ShowSanaeSearchSelected();
	}
}

template<bool has_replace>
void DialogSearchReplace<has_replace>::ShowSanaeSearchSelected() {
	if (!sanae_project_search) return;
	long selected = sanae_project_search->results->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
	if (selected < 0 || static_cast<std::size_t>(selected) >= sanae_project_search->matches.size()) {
		sanae_project_search->details->ChangeValue(_("Select a result to see its English and Russian context."));
		return;
	}
	auto const& value = sanae_project_search->matches[static_cast<std::size_t>(selected)];
	wxString hint;
	auto active = c->sanaeProject->ActiveEpisode();
	if (active && active->episode_code == value.episode_code)
		hint = _("\n\nDouble-click to go to this line in the current episode.");
	else
		hint = _("\n\nThis result is from another episode; the current working ASS will not be switched automatically.");
	sanae_project_search->details->ChangeValue(agi::wxformat(
		_("Episode %s · %s\n\nEN:\n%s\n\nRU:\n%s%s"),
		to_wx(value.episode_code), to_wx(agi::Time(value.start).GetAssFormatted(true)),
		to_wx(value.source), to_wx(value.russian.empty() ? "—" : value.russian), hint));
}

template<bool has_replace>
void DialogSearchReplace<has_replace>::NavigateSanaeSearchSelected() {
	if (!sanae_project_search || !c->sanaeProject || !c->sanaeProject->HasOpenEpisode()) return;
	long selected = sanae_project_search->results->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
	if (selected < 0 || static_cast<std::size_t>(selected) >= sanae_project_search->matches.size()) return;
	auto const& value = sanae_project_search->matches[static_cast<std::size_t>(selected)];
	auto active = c->sanaeProject->ActiveEpisode();
	if (!active || active->episode_code != value.episode_code) return;
	for (auto& line : c->ass->Events) {
		if (static_cast<int>(line.Start) != value.start || static_cast<int>(line.End) != value.end) continue;
		c->selectionController->SetSelectionAndActive({&line}, &line);
		return;
	}
}

template<bool has_replace>
void DialogSearchReplace<has_replace>::FindReplace(bool (SearchReplaceEngine::*func)()) {
	TransferDataFromWindow();

	if (settings->find.empty())
		return;

	c->search->Configure(*settings);
	try {
		((*c->search).*func)();
	}
	catch (std::exception const& e) {
		wxMessageBox(to_wx(e.what()), _("Error"), wxOK | wxICON_ERROR | wxCENTER, this);
		return;
	}

	config::mru->Add("Find", settings->find);
	if (has_replace)
		config::mru->Add("Replace", settings->replace_with);

	OPT_SET("Tool/Search Replace/Match Case")->SetBool(settings->match_case);
	OPT_SET("Tool/Search Replace/RegExp")->SetBool(settings->use_regex);
	OPT_SET("Tool/Search Replace/Skip Comments")->SetBool(settings->ignore_comments);
	OPT_SET("Tool/Search Replace/Skip Tags")->SetBool(settings->skip_tags);
	OPT_SET("Tool/Search Replace/Field")->SetInt(static_cast<int>(settings->field));
	OPT_SET("Tool/Search Replace/Affect")->SetInt(static_cast<int>(settings->limit_to));

	UpdateDropDowns();
}

static void update_mru(wxComboBox *cb, const char *mru_name) {
	cb->Freeze();
	cb->Clear();
	cb->Append(lagi_MRU_wxAS(mru_name));
	if (!cb->IsListEmpty())
		cb->SetSelection(0);
	cb->Thaw();
}

template<bool has_replace>
void DialogSearchReplace<has_replace>::UpdateDropDowns() {
	update_mru(find_edit, "Find");

	if (has_replace)
		update_mru(replace_edit, "Replace");
}

template<bool replace>
void ShowSearchReplaceDialog(agi::Context *context) {
	auto other = context->dialog->Get<DialogSearchReplace<!replace>>();
	if (other != nullptr) {
		other->Close();
	}

	context->dialog->Show<DialogSearchReplace<replace>>(context);
	auto dialog = context->dialog->Get<DialogSearchReplace<replace>>();

	dialog->find_edit->SetFocus();
	dialog->find_edit->SelectAll();
	dialog->Raise();
}

void ShowSearchReplaceDialog(agi::Context *context, bool replace) {
	if (replace) {
		ShowSearchReplaceDialog<true>(context);
	} else {
		ShowSearchReplaceDialog<false>(context);
	}
}
