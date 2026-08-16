// Copyright (c) 2026, Aegisub Sanae contributors

#include "command.h"

#include "../ass_dialogue.h"
#include "../ass_file.h"
#include "../compat.h"
#include "../dialog_sanae_connection.h"
#include "../dialog_sanae_final_review.h"
#include "../dialog_sanae_project.h"
#include "../dialog_sanae_terminology.h"
#include "../format.h"
#include "../include/aegisub/context.h"
#include "../sanae_project.h"
#include "../selection_controller.h"
#include "../text_selection_controller.h"
#include "../translation_project.h"

#include <libaegisub/ass/time.h>

#include <algorithm>
#include <string>
#include <vector>

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/dialog.h>
#include <wx/listctrl.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/textdlg.h>
#include <wx/utils.h>

namespace {
using cmd::Command;

class ProjectSearchDialog final : public wxDialog {
	agi::Context *context;
	SanaeProjectManager& manager;
	wxTextCtrl *query;
	wxChoice *scope;
	wxCheckBox *fuzzy;
	wxChoice *episode;
	wxListCtrl *list;
	wxTextCtrl *details;
	std::vector<SanaeRepeatMatch> results;
	std::vector<std::string> episode_codes;

	void ShowSelected() {
		long selected = list->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
		if (selected < 0 || static_cast<size_t>(selected) >= results.size()) {
			details->ChangeValue(_("Select a result to view its English and Russian subtitle context."));
			return;
		}
		auto const& value = results[selected];
		wxString hint;
		auto active = manager.ActiveEpisode();
		if (active && active->episode_code == value.episode_code)
			hint = _("\n\nDouble-click to go to this line in the current episode.");
		details->ChangeValue(agi::wxformat(
			_("%s · %s — %s\n\nEN:\n%s\n\nRU:\n%s%s"),
			to_wx(value.episode_code), to_wx(agi::Time(value.start).GetAssFormatted(true)),
			to_wx(agi::Time(value.end).GetAssFormatted(true)), to_wx(value.source),
			to_wx(value.russian.empty() ? "—" : value.russian), hint));
	}

	void NavigateSelected() {
		long selected = list->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
		if (selected < 0 || static_cast<std::size_t>(selected) >= results.size() || !manager.HasOpenEpisode()) return;
		auto const& value = results[static_cast<std::size_t>(selected)];
		auto active = manager.ActiveEpisode();
		if (!active || active->episode_code != value.episode_code) return;
		for (auto& line : context->ass->Events) {
			if (static_cast<int>(line.Start) != value.start || static_cast<int>(line.End) != value.end) continue;
			context->selectionController->SetSelectionAndActive({&line}, &line);
			return;
		}
	}
	void Search() {
		SanaeSearchOptions options;
		options.query = from_wx(query->GetValue());
		options.scope = scope->GetSelection() == 0 ? SanaeSearchScope::English
			: scope->GetSelection() == 1 ? SanaeSearchScope::Russian : SanaeSearchScope::All;
		options.fuzzy_word_forms = fuzzy->GetValue();
		if (episode->GetSelection() > 0
			&& static_cast<std::size_t>(episode->GetSelection() - 1) < episode_codes.size())
			options.episode_code = episode_codes[static_cast<std::size_t>(episode->GetSelection() - 1)];
		wxBusyCursor busy;
		results = manager.SearchMemory(options);
		list->DeleteAllItems();
		for (auto const& value : results) {
			long row = list->InsertItem(list->GetItemCount(), to_wx(value.episode_code));
			list->SetItem(row, 1, to_wx(agi::Time(value.start).GetAssFormatted(true)));
			list->SetItem(row, 2, to_wx(value.source));
			list->SetItem(row, 3, to_wx(value.russian.empty() ? "—" : value.russian));
			list->SetItem(row, 4, value.kind == SanaeRepeatKind::Similar
				? _("Similar form")
				: value.kind == SanaeRepeatKind::Span ? _("Across adjacent lines") : _("Exact text"));
		}
		if (results.empty())
			details->ChangeValue(_("No matches were found in the synchronized project memory."));
		else {
			list->SetItemState(0, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
			ShowSelected();
		}
	}

public:
	ProjectSearchDialog(agi::Context *c, wxWindow *parent, SanaeProjectManager& project_manager)
	: wxDialog(parent, -1, _("Search project"), wxDefaultPosition, wxSize(930, 590),
		wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
	, context(c)
	, manager(project_manager)
	{
		auto main = new wxBoxSizer(wxVERTICAL);
		auto search = new wxBoxSizer(wxHORIZONTAL);
		search->Add(new wxStaticText(this, -1, _("Search text:")),
			0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
		search->Add(query = new wxTextCtrl(this, -1, wxString(), wxDefaultPosition,
			wxDefaultSize, wxTE_PROCESS_ENTER), 1, wxRIGHT, 6);
		auto search_button = new wxButton(this, -1, _("Search"));
		search->Add(search_button);
		main->Add(search, 0, wxEXPAND | wxBOTTOM, 8);

		auto options = new wxBoxSizer(wxHORIZONTAL);
		options->Add(new wxStaticText(this, -1, _("Search in:")),
			0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
		scope = new wxChoice(this, -1, wxDefaultPosition, wxDefaultSize,
			wxArrayString{_("English subtitles"), _("Russian subtitles"), _("Everywhere")});
		scope->SetSelection(2);
		options->Add(scope, 0, wxRIGHT, 12);
		options->Add(fuzzy = new wxCheckBox(this, -1, _("Find similar word forms")),
			0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 12);
		options->Add(new wxStaticText(this, -1, _("Episodes:")),
			0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
		episode = new wxChoice(this, -1);
		episode->Append(_("All episodes"));
		episode->SetSelection(0);
		std::vector<std::string> codes;
		for (auto const& value : manager.Episodes())
			if (!value.IsDeleted() && value.project_id == manager.ActiveProjectId())
				codes.push_back(value.episode_code);
		std::sort(codes.begin(), codes.end());
		codes.erase(std::unique(codes.begin(), codes.end()), codes.end());
		for (auto const& code : codes) {
			episode->Append(to_wx(code));
			episode_codes.push_back(code);
		}
		options->Add(episode, 0);
		main->Add(options, 0, wxEXPAND | wxBOTTOM, 8);

		list = new wxListCtrl(this, -1, wxDefaultPosition, wxDefaultSize,
			wxLC_REPORT | wxLC_SINGLE_SEL);
		list->InsertColumn(0, _("Episode"), wxLIST_FORMAT_LEFT, 90);
		list->InsertColumn(1, _("Time"), wxLIST_FORMAT_LEFT, 100);
		list->InsertColumn(2, _("ENSUB"), wxLIST_FORMAT_LEFT, 330);
		list->InsertColumn(3, _("Russian subtitles"), wxLIST_FORMAT_LEFT, 330);
		list->InsertColumn(4, _("Match"), wxLIST_FORMAT_LEFT, 100);
		main->Add(list, 1, wxEXPAND | wxBOTTOM, 8);
		details = new wxTextCtrl(this, -1, wxString(), wxDefaultPosition, wxSize(-1, 150),
			wxTE_MULTILINE | wxTE_READONLY);
		main->Add(details, 0, wxEXPAND | wxBOTTOM, 8);
		auto close_row = new wxBoxSizer(wxHORIZONTAL);
		close_row->AddStretchSpacer();
		close_row->Add(new wxButton(this, wxID_CLOSE));
		main->Add(close_row, 0, wxEXPAND);
		SetSizer(main);
		CentreOnParent();

		search_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Search(); });
		query->Bind(wxEVT_TEXT_ENTER, [this](wxCommandEvent&) { Search(); });
		list->Bind(wxEVT_LIST_ITEM_SELECTED, [this](wxListEvent&) { ShowSelected(); });
		list->Bind(wxEVT_LIST_ITEM_ACTIVATED, [this](wxListEvent&) { NavigateSelected(); });
		Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { EndModal(wxID_CLOSE); }, wxID_CLOSE);
		query->SetFocus();
	}
};

class RepeatHistoryDialog final : public wxDialog {
	agi::Context *context;
	AssDialogue *line;
	std::vector<SanaeRepeatMatch> matches;
	wxListCtrl *list;
	wxTextCtrl *details;
	wxButton *use;

	long Selected() const {
		return list->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
	}
	void ShowSelected() {
		auto selected = Selected();
		if (selected < 0 || static_cast<std::size_t>(selected) >= matches.size()) {
			details->ChangeValue(_("Select a previous occurrence."));
			use->Enable(false);
			return;
		}
		auto const& match = matches[static_cast<std::size_t>(selected)];
		wxString kind = match.kind == SanaeRepeatKind::Exact ? _("Exact source match")
			: match.kind == SanaeRepeatKind::Fragment ? _("Previous line fragment")
			: match.kind == SanaeRepeatKind::Span ? _("Split/merged previous lines")
			: agi::wxformat(_("Similar source line (%.1f%%)"), match.similarity * 100.0);
		details->ChangeValue(agi::wxformat(
			_("%s\n\nEpisode %s · %s — %s\n\nEnglish:\n%s\n\nPrevious translation:\n%s"),
			kind,
			to_wx(match.episode_code), to_wx(agi::Time(match.start).GetAssFormatted(true)),
			to_wx(agi::Time(match.end).GetAssFormatted(true)), to_wx(match.source),
			to_wx(match.russian.empty() ? "—" : match.russian)));
		use->Enable(match.kind == SanaeRepeatKind::Exact && !match.russian.empty());
	}
	void UseSelected() {
		auto selected = Selected();
		if (selected < 0 || static_cast<std::size_t>(selected) >= matches.size()) return;
		auto const& match = matches[static_cast<std::size_t>(selected)];
		if (match.kind != SanaeRepeatKind::Exact || match.russian.empty()) return;
		auto current = line->Text.get();
		if (!current.empty()) {
			wxString warning;
			if (current.find('{') != std::string::npos)
				warning = _("\n\nThe current line contains ASS override tags. They will be replaced as part of the text.");
			if (wxMessageBox(agi::wxformat(
				_("Current text:\n%s\n\nPrevious translation:\n%s%s\n\nUse the previous translation?"),
				to_wx(current), to_wx(match.russian), warning), _("Use previous translation"),
				wxYES_NO | wxNO_DEFAULT | wxICON_QUESTION, this) != wxYES) return;
		}
		line->Text = match.russian;
		context->ass->Commit(_("use previous project translation"),
			AssFile::COMMIT_DIAG_TEXT, -1, line);
		EndModal(wxID_OK);
	}
public:
	RepeatHistoryDialog(agi::Context *c, AssDialogue *active_line)
	: wxDialog(c->parent, -1, _("Previous source occurrences"), wxDefaultPosition,
		wxSize(850, 560), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
	, context(c), line(active_line), matches(c->sanaeProject->RepeatsFor(active_line))
	{
		auto main = new wxBoxSizer(wxVERTICAL);
		list = new wxListCtrl(this, -1, wxDefaultPosition, wxDefaultSize,
			wxLC_REPORT | wxLC_SINGLE_SEL | wxLC_HRULES | wxLC_VRULES);
		list->InsertColumn(0, _("Episode"), wxLIST_FORMAT_LEFT, 90);
		list->InsertColumn(1, _("Time"), wxLIST_FORMAT_LEFT, 105);
		list->InsertColumn(2, _("English"), wxLIST_FORMAT_LEFT, 300);
		list->InsertColumn(3, _("Previous translation"), wxLIST_FORMAT_LEFT, 300);
		for (auto const& match : matches) {
			auto row = list->InsertItem(list->GetItemCount(), to_wx(match.episode_code));
			list->SetItem(row, 1, to_wx(agi::Time(match.start).GetAssFormatted(true)));
			list->SetItem(row, 2, to_wx(match.source));
			list->SetItem(row, 3, to_wx(match.russian.empty() ? "—" : match.russian));
		}
		main->Add(list, 1, wxEXPAND | wxBOTTOM, 8);
		details = new wxTextCtrl(this, -1, wxString(), wxDefaultPosition, wxSize(-1, 175),
			wxTE_MULTILINE | wxTE_READONLY);
		main->Add(details, 0, wxEXPAND | wxBOTTOM, 8);
		auto buttons = new wxBoxSizer(wxHORIZONTAL);
		buttons->Add(use = new wxButton(this, -1, _("Use previous translation")));
		buttons->AddStretchSpacer();
		buttons->Add(new wxButton(this, wxID_CLOSE));
		main->Add(buttons, 0, wxEXPAND);
		SetSizer(main);
		CentreOnParent();
		list->Bind(wxEVT_LIST_ITEM_SELECTED, [this](wxListEvent&) { ShowSelected(); });
		use->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { UseSelected(); });
		Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { EndModal(wxID_CLOSE); }, wxID_CLOSE);
		if (!matches.empty()) {
			list->SetItemState(0, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
			ShowSelected();
		}
		else ShowSelected();
	}
};

struct validate_enrolled : public Command {
	CMD_TYPE(COMMAND_VALIDATE)
	bool Validate(agi::Context const *c) override { return c->sanaeProject->IsEnrolled(); }
};

struct validate_active_project : public validate_enrolled {
	bool Validate(agi::Context const *c) override {
		return validate_enrolled::Validate(c) && !c->sanaeProject->ActiveProjectId().empty();
	}
};

struct sanae_server_connection final : public Command {
	CMD_NAME("sanae/server/connection")
	STR_MENU("&Server connection…")
	STR_DISP("Server connection")
	STR_HELP("Connect this device to the Sanae server or check the saved connection")
	void operator()(agi::Context *c) override { ShowSanaeConnectionDialog(c); }
};

struct sanae_project_open final : public validate_enrolled {
	CMD_NAME("sanae/project/open")
	STR_MENU("&Open project…")
	STR_DISP("Open project")
	STR_HELP("Choose a season, project and episode")
	void operator()(agi::Context *c) override { ShowSanaeProjectDialog(c); }
};

struct validate_open_episode : public Command {
	CMD_TYPE(COMMAND_VALIDATE)
	bool Validate(agi::Context const *c) override { return c->sanaeProject->HasOpenEpisode(); }
};

struct sanae_project_sync final : public validate_active_project {
	CMD_NAME("sanae/project/sync")
	STR_MENU("S&ynchronize project")
	STR_DISP("Synchronize project")
	STR_HELP("Synchronize the open project and rebuild its local memory")
	void operator()(agi::Context *c) override {
		try {
			wxBusyCursor busy;
			c->sanaeProject->SyncProject(c->sanaeProject->ActiveProjectId());
		}
		catch (std::exception const& error) {
			wxMessageBox(agi::wxformat(_("The project could not be synchronized.\n\nDetails: %s"),
				to_wx(error.what())), _("Project synchronization"), wxOK | wxICON_ERROR, c->parent);
		}
	}
};

struct sanae_recovery_now final : public validate_open_episode {
	CMD_NAME("sanae/recovery/create_now")
	STR_MENU("Create &recovery copy now")
	STR_DISP("Create recovery copy now")
	STR_HELP("Save the complete working ASS as a server recovery copy")
	bool Validate(agi::Context const *c) override {
		return validate_open_episode::Validate(c) && c->sanaeProject->IsEnrolled();
	}
	void operator()(agi::Context *c) override { c->sanaeProject->RequestRecoveryNow(); }
};

struct sanae_season_create final : public validate_enrolled {
	CMD_NAME("sanae/season/create")
	STR_MENU("Create &season…")
	STR_DISP("Create season")
	STR_HELP("Create a new anime season on the Sanae server")
	void operator()(agi::Context *c) override { ShowSanaeCreateSeasonDialog(c, c->parent); }
};

struct sanae_project_create final : public validate_enrolled {
	CMD_NAME("sanae/project/create")
	STR_MENU("Create &project…")
	STR_DISP("Create project")
	STR_HELP("Create a project in an existing season")
	void operator()(agi::Context *c) override { ShowSanaeCreateProjectDialog(c, c->parent); }
};

struct sanae_episode_add final : public validate_active_project {
	CMD_NAME("sanae/episode/add")
	STR_MENU("&Add episode…")
	STR_DISP("Add episode")
	STR_HELP("Create an episode and upload its English subtitles")
	void operator()(agi::Context *c) override { ShowSanaeAddEpisodeDialog(c, c->parent); }
};

struct sanae_batch_import final : public validate_active_project {
	CMD_NAME("sanae/project/batch_import")
	STR_MENU("&Batch import…")
	STR_DISP("Batch import")
	STR_HELP("Match and import folders of English and Russian subtitles")
	void operator()(agi::Context *c) override { ShowSanaeBatchImportForActiveProject(c, c->parent); }
};

struct sanae_terminology final : public validate_active_project {
	CMD_NAME("sanae/project/terminology")
	STR_MENU("&Terminology…")
	STR_DISP("Project terminology")
	STR_HELP("View, search and queue changes to project terminology")
	void operator()(agi::Context *c) override { ShowSanaeTerminologyDialog(c, c->parent); }
};

struct sanae_project_close final : public validate_open_episode {
	CMD_NAME("sanae/project/close")
	STR_MENU("&Detach current episode")
	STR_DISP("Detach current episode")
	STR_HELP("Return the current ASS to ordinary Aegisub mode without changing subtitles")
	void operator()(agi::Context *c) override { c->sanaeProject->CloseEpisode(); }
};

struct sanae_final_review final : public validate_open_episode {
	CMD_NAME("sanae/project/final_review")
	STR_MENU("&Final review…")
	STR_DISP("Final review")
	STR_HELP("Review terminology, consistency and source repeats, then finalize the episode")
	void operator()(agi::Context *c) override { ShowSanaeFinalReview(c, c->parent); }
};

struct sanae_repeat_view final : public validate_open_episode {
	CMD_NAME("sanae/repeat/view")
	STR_MENU("View source repeat")
	STR_DISP("View source repeat")
	STR_HELP("Show the cached ENSUB match and its previous RUSUB translation")
	bool Validate(agi::Context const *c) override {
		return validate_open_episode::Validate(c)
			&& c->sanaeProject->RepeatFor(c->selectionController->GetActiveLine());
	}
	void operator()(agi::Context *c) override {
		auto line = c->selectionController->GetActiveLine();
		if (line) RepeatHistoryDialog(c, line).ShowModal();
	}
};

struct sanae_memory_search final : public validate_active_project {
	CMD_NAME("sanae/memory/search")
	STR_MENU("Search &project…")
	STR_DISP("Search project")
	STR_HELP("Search locally synchronized ENSUB and previous RUSUB text")
	void operator()(agi::Context *c) override {
		ProjectSearchDialog(c, c->parent, *c->sanaeProject).ShowModal();
	}
};

struct sanae_terminology_queue final : public validate_open_episode {
	CMD_NAME("sanae/terminology/queue")
	STR_MENU("Add to project terminology…")
	STR_DISP("Add to project terminology")
	STR_HELP("Queue an English/Russian terminology pair for Final Review")
	void operator()(agi::Context *c) override {
		auto line = c->selectionController->GetActiveLine();
		if (!line) return;
		std::string english = c->translationProject->SourceDisplayText(line);
		std::string russian;
		int start = c->textSelectionController->GetSelectionStart();
		int end = c->textSelectionController->GetSelectionEnd();
		auto const& raw = line->Text.get();
		start = std::clamp(start, 0, static_cast<int>(raw.size()));
		end = std::clamp(end, start, static_cast<int>(raw.size()));
		if (end > start) russian = raw.substr(static_cast<size_t>(start), static_cast<size_t>(end - start));
		else russian = line->GetStrippedText();
		ShowSanaeTerminologyEntryDialog(c, c->parent, std::move(english), std::move(russian));
	}
};
}

namespace cmd {
void init_sanae() {
	reg(std::make_unique<sanae_server_connection>());
	reg(std::make_unique<sanae_project_open>());
	reg(std::make_unique<sanae_project_sync>());
	reg(std::make_unique<sanae_recovery_now>());
	reg(std::make_unique<sanae_season_create>());
	reg(std::make_unique<sanae_project_create>());
	reg(std::make_unique<sanae_episode_add>());
	reg(std::make_unique<sanae_batch_import>());
	reg(std::make_unique<sanae_terminology>());
	reg(std::make_unique<sanae_project_close>());
	reg(std::make_unique<sanae_final_review>());
	reg(std::make_unique<sanae_repeat_view>());
	reg(std::make_unique<sanae_memory_search>());
	reg(std::make_unique<sanae_terminology_queue>());
}
}
