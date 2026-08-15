// Copyright (c) 2026, Aegisub Sanae contributors

#include "dialog_sanae_project.h"

#include "compat.h"
#include "dialog_sanae_batch_import.h"
#include "dialog_sanae_episode.h"
#include "format.h"
#include "include/aegisub/context.h"
#include "sanae_project.h"
#include "subtitle_format.h"
#include "utils.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

#include <wx/button.h>
#include <wx/choice.h>
#include <wx/datetime.h>
#include <wx/dialog.h>
#include <wx/listbox.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/textdlg.h>
#include <wx/utils.h>

namespace {
constexpr char const *season_codes[] = {"winter", "spring", "summer", "autumn"};

wxArrayString season_names() {
	return {_("Winter"), _("Spring"), _("Summer"), _("Autumn")};
}

int current_anime_season() {
	return static_cast<int>(wxDateTime::Now().GetMonth()) / 3;
}

std::string make_project_slug(std::string const& name) {
	std::string result;
	bool separator = false;
	for (unsigned char c : name) {
		if (c < 0x80 && std::isalnum(c)) {
			if (separator && !result.empty()) result.push_back('-');
			result.push_back(static_cast<char>(std::tolower(c)));
			separator = false;
		}
		else if (!result.empty()) separator = true;
	}
	return result;
}

bool valid_project_slug(std::string const& value) {
	if (value.empty() || value.front() == '-' || value.back() == '-') return false;
	bool separator = false;
	for (unsigned char c : value) {
		if (c == '-') {
			if (separator) return false;
			separator = true;
		}
		else {
			if (!(c >= 'a' && c <= 'z') && !std::isdigit(c)) return false;
			separator = false;
		}
	}
	return true;
}

std::string suggested_episode_code(SanaeProjectManager const& manager, std::string const& project_id) {
	long maximum = 0;
	size_t width = 2;
	bool found = false;
	for (auto const& episode : manager.Episodes()) {
		if (episode.project_id != project_id || episode.IsDeleted() || episode.episode_code.empty()
			|| !std::all_of(episode.episode_code.begin(), episode.episode_code.end(),
				[](unsigned char c) { return std::isdigit(c); })) continue;
		char *end = nullptr;
		long value = std::strtol(episode.episode_code.c_str(), &end, 10);
		if (!end || *end || value < 0) continue;
		if (!found || value > maximum) {
			maximum = value;
			width = std::max<size_t>(2, episode.episode_code.size());
			found = true;
		}
	}
	std::ostringstream output;
	output << std::setfill('0') << std::setw(static_cast<int>(width)) << (found ? maximum + 1 : 1);
	return output.str();
}

class CreateSeasonDialog final : public wxDialog {
	SanaeProjectManager& manager;
	wxSpinCtrl *year;
	wxChoice *season;
	wxTextCtrl *name;
	wxString generated_name;
	std::string created_id;

	void UpdateGeneratedName() {
		auto replacement = agi::wxformat("%s %d", season->GetStringSelection(), year->GetValue());
		if (name->GetValue().empty() || name->GetValue() == generated_name)
			name->SetValue(replacement);
		generated_name = replacement;
	}

	void Create() {
		auto display_name = from_wx(name->GetValue().Trim());
		if (display_name.empty()) {
			wxMessageBox(_("Enter a season name."), _("Create season"), wxOK | wxICON_ERROR, this);
			return;
		}
		try {
			wxBusyCursor busy;
			int selected = std::max(0, season->GetSelection());
			auto value = manager.CreateSeason(year->GetValue(), season_codes[selected],
				display_name, static_cast<double>(selected + 1));
			created_id = value.id;
			EndModal(wxID_OK);
		}
		catch (std::exception const& error) {
			wxMessageBox(agi::wxformat(_("Could not create the season.\n\nDetails: %s"),
				to_wx(error.what())), _("Create season"), wxOK | wxICON_ERROR, this);
		}
	}

public:
	CreateSeasonDialog(wxWindow *parent, SanaeProjectManager& project_manager)
	: wxDialog(parent, -1, _("Create season"), wxDefaultPosition, wxDefaultSize,
		wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
	, manager(project_manager)
	{
		auto fields = new wxFlexGridSizer(2, 6, 8);
		fields->AddGrowableCol(1, 1);
		fields->Add(new wxStaticText(this, -1, _("Year:")), 0, wxALIGN_CENTER_VERTICAL);
		fields->Add(year = new wxSpinCtrl(this, -1, wxString(), wxDefaultPosition, wxDefaultSize,
			wxSP_ARROW_KEYS, 1900, 3000, wxDateTime::Now().GetYear()), 1, wxEXPAND);
		fields->Add(new wxStaticText(this, -1, _("Season:")), 0, wxALIGN_CENTER_VERTICAL);
		fields->Add(season = new wxChoice(this, -1, wxDefaultPosition, wxDefaultSize, season_names()), 1, wxEXPAND);
		fields->Add(new wxStaticText(this, -1, _("Name:")), 0, wxALIGN_CENTER_VERTICAL);
		fields->Add(name = new wxTextCtrl(this, -1), 1, wxEXPAND);
		auto main = new wxBoxSizer(wxVERTICAL);
		main->Add(fields, 1, wxEXPAND | wxBOTTOM, 10);
		main->Add(CreateStdDialogButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND);
		SetSizerAndFit(main);
		SetSize(wxSize(480, GetSize().GetHeight()));
		CentreOnParent();
		season->SetSelection(current_anime_season());
		UpdateGeneratedName();
		year->Bind(wxEVT_SPINCTRL, [this](wxSpinEvent&) { UpdateGeneratedName(); });
		season->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { UpdateGeneratedName(); });
		Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Create(); }, wxID_OK);
	}
	std::string const& CreatedId() const { return created_id; }
};

class CreateProjectDialog final : public wxDialog {
	SanaeProjectManager& manager;
	wxChoice *season;
	wxTextCtrl *name;
	wxTextCtrl *slug;
	std::vector<std::string> season_ids;
	std::string generated_slug;
	std::string created_id;

	void NameChanged() {
		auto replacement = make_project_slug(from_wx(name->GetValue()));
		if (from_wx(slug->GetValue()).empty() || from_wx(slug->GetValue()) == generated_slug)
			slug->SetValue(to_wx(replacement));
		generated_slug = std::move(replacement);
	}

	void Create() {
		int selected = season->GetSelection();
		auto project_name = from_wx(name->GetValue().Trim());
		auto project_slug = from_wx(slug->GetValue().Trim());
		if (selected < 0 || static_cast<size_t>(selected) >= season_ids.size()) {
			wxMessageBox(_("Select a season."), _("Create project"), wxOK | wxICON_ERROR, this);
			return;
		}
		if (project_name.empty()) {
			wxMessageBox(_("Enter a project name."), _("Create project"), wxOK | wxICON_ERROR, this);
			return;
		}
		if (!valid_project_slug(project_slug)) {
			wxMessageBox(_("The short ID may contain only lowercase Latin letters, digits and single hyphens."),
				_("Create project"), wxOK | wxICON_ERROR, this);
			return;
		}
		try {
			wxBusyCursor busy;
			auto value = manager.CreateProject(season_ids[selected], project_slug, project_name);
			created_id = value.id;
			EndModal(wxID_OK);
		}
		catch (std::exception const& error) {
			wxMessageBox(agi::wxformat(_("Could not create the project.\n\nDetails: %s"),
				to_wx(error.what())), _("Create project"), wxOK | wxICON_ERROR, this);
		}
	}

public:
	CreateProjectDialog(wxWindow *parent, SanaeProjectManager& project_manager,
		std::string const& preferred_season_id)
	: wxDialog(parent, -1, _("Create project"), wxDefaultPosition, wxDefaultSize,
		wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
	, manager(project_manager)
	{
		auto fields = new wxFlexGridSizer(2, 6, 8);
		fields->AddGrowableCol(1, 1);
		fields->Add(new wxStaticText(this, -1, _("Name:")), 0, wxALIGN_CENTER_VERTICAL);
		fields->Add(name = new wxTextCtrl(this, -1), 1, wxEXPAND);
		fields->Add(new wxStaticText(this, -1, _("Season:")), 0, wxALIGN_CENTER_VERTICAL);
		fields->Add(season = new wxChoice(this, -1), 1, wxEXPAND);
		fields->Add(new wxStaticText(this, -1, _("Short ID:")), 0, wxALIGN_CENTER_VERTICAL);
		fields->Add(slug = new wxTextCtrl(this, -1), 1, wxEXPAND);

		int preferred = wxNOT_FOUND;
		for (auto const& value : manager.Seasons()) {
			season->Append(agi::wxformat("%d — %s", value.year, to_wx(value.display_name)));
			season_ids.push_back(value.id);
			if (value.id == preferred_season_id) preferred = static_cast<int>(season_ids.size() - 1);
		}
		if (preferred != wxNOT_FOUND) season->SetSelection(preferred);
		else if (!season_ids.empty()) season->SetSelection(0);

		auto hint = new wxStaticText(this, -1,
			_("The short ID is generated automatically and can be edited before creation."));
		hint->Wrap(500);
		auto main = new wxBoxSizer(wxVERTICAL);
		main->Add(fields, 1, wxEXPAND | wxBOTTOM, 7);
		main->Add(hint, 0, wxEXPAND | wxBOTTOM, 10);
		main->Add(CreateStdDialogButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND);
		SetSizerAndFit(main);
		SetSize(wxSize(560, GetSize().GetHeight()));
		CentreOnParent();
		name->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { NameChanged(); });
		Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Create(); }, wxID_OK);
	}
	std::string const& CreatedId() const { return created_id; }
};

class ProjectDialog final : public wxDialog {
	agi::Context *context;
	SanaeProjectManager& manager;
	wxChoice *season_choice;
	wxChoice *project_choice;
	wxListBox *episode_list;
	wxStaticText *status;
	wxStaticText *empty_state;
	wxButton *sync_button;
	wxButton *create_season_button;
	wxButton *create_project_button;
	wxButton *add_button;
	wxButton *batch_import_button;
	wxButton *attach_button;
	wxButton *details_button;
	wxButton *close_episode_button;
	std::vector<std::string> season_ids;
	std::vector<std::string> project_ids;
	std::vector<std::string> episode_ids;
	std::string preferred_season_id;
	std::string preferred_project_id;

	std::string SelectedSeason() const {
		int selection = season_choice->GetSelection();
		return selection >= 0 && static_cast<size_t>(selection) < season_ids.size() ? season_ids[selection] : std::string();
	}
	std::string SelectedProject() const {
		int selection = project_choice->GetSelection();
		return selection >= 0 && static_cast<size_t>(selection) < project_ids.size() ? project_ids[selection] : std::string();
	}
	std::string SelectedEpisode() const {
		int selection = episode_list->GetSelection();
		return selection >= 0 && static_cast<size_t>(selection) < episode_ids.size() ? episode_ids[selection] : std::string();
	}
	void ShowError(std::exception const& error) {
		wxMessageBox(agi::wxformat(_("The action could not be completed.\n\nDetails: %s"),
			to_wx(error.what())), _("Project"), wxOK | wxICON_ERROR, this);
	}
	void UpdateActions() {
		bool have_season = !SelectedSeason().empty();
		bool have_project = !SelectedProject().empty();
		bool have_episode = !SelectedEpisode().empty();
		sync_button->Enable(have_project);
		create_project_button->Enable(have_season);
		add_button->Enable(have_project);
		batch_import_button->Enable(have_project);
		attach_button->Enable(have_episode);
		details_button->Enable(have_episode);
		close_episode_button->Enable(manager.HasOpenEpisode());

		if (manager.Seasons().empty())
			empty_state->SetLabel(_("There are no seasons on the server yet.\nCreate the first season to begin."));
		else if (!have_project)
			empty_state->SetLabel(_("There are no projects in this season yet.\nCreate a project to continue."));
		else if (episode_ids.empty())
			empty_state->SetLabel(_("There are no episodes in this project yet.\nAdd an episode or use batch import."));
		else
			empty_state->SetLabel(wxString());
		empty_state->Show(!empty_state->GetLabel().empty());
		Layout();
	}
	void PopulateSeasons() {
		auto previous = SelectedSeason();
		season_choice->Clear();
		season_ids.clear();
		std::string active_season;
		for (auto const& project : manager.Projects())
			if (project.id == manager.ActiveProjectId()) active_season = project.season_id;
		for (auto const& season : manager.Seasons()) {
			season_choice->Append(to_wx(season.display_name));
			season_ids.push_back(season.id);
		}
		auto select_id = [&](std::string const& id) {
			auto found = std::find(season_ids.begin(), season_ids.end(), id);
			if (found == season_ids.end()) return false;
			season_choice->SetSelection(static_cast<int>(std::distance(season_ids.begin(), found)));
			return true;
		};
		bool selected = select_id(preferred_season_id) || select_id(previous) || select_id(active_season);
		if (!selected) {
			int year = wxDateTime::Now().GetYear();
			auto code = std::string(season_codes[current_anime_season()]);
			for (size_t i = 0; i < manager.Seasons().size(); ++i) {
				auto const& value = manager.Seasons()[i];
				auto lowered = value.code;
				std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char c) {
					return static_cast<char>(std::tolower(c));
				});
				if (value.year == year && lowered == code) {
					season_choice->SetSelection(static_cast<int>(i));
					selected = true;
					break;
				}
			}
		}
		if (!selected && !season_ids.empty()) season_choice->SetSelection(0);
		preferred_season_id.clear();
	}
	void PopulateProjects() {
		auto previous = SelectedProject();
		project_choice->Clear();
		project_ids.clear();
		for (auto const& project : manager.Projects()) {
			if (!SelectedSeason().empty() && project.season_id != SelectedSeason()) continue;
			project_choice->Append(to_wx(project.name));
			project_ids.push_back(project.id);
		}
		bool active_belongs_here = SelectedSeason().empty();
		for (auto const& project : manager.Projects())
			if (project.id == manager.ActiveProjectId()) active_belongs_here = project.season_id == SelectedSeason();
		if (active_belongs_here && !manager.ActiveProjectId().empty()
			&& std::find(project_ids.begin(), project_ids.end(), manager.ActiveProjectId()) == project_ids.end()) {
			project_choice->Append(to_wx(manager.ActiveProjectName().empty()
				? manager.ActiveProjectId() : manager.ActiveProjectName()));
			project_ids.push_back(manager.ActiveProjectId());
		}
		auto select_id = [&](std::string const& id) {
			auto found = std::find(project_ids.begin(), project_ids.end(), id);
			if (found == project_ids.end()) return false;
			project_choice->SetSelection(static_cast<int>(std::distance(project_ids.begin(), found)));
			return true;
		};
		if (!select_id(preferred_project_id) && !select_id(previous)
			&& !select_id(manager.ActiveProjectId()) && !project_ids.empty())
			project_choice->SetSelection(0);
		preferred_project_id.clear();
		if (SelectedProject().empty()) {
			episode_list->Clear();
			episode_ids.clear();
		}
		UpdateActions();
	}
	void PopulateEpisodes() {
		episode_list->Clear();
		episode_ids.clear();
		std::vector<SanaeEpisodeInfo const *> ordered;
		for (auto const& episode : manager.Episodes())
			if (episode.project_id == SelectedProject() && !episode.IsDeleted()) ordered.push_back(&episode);
		std::sort(ordered.begin(), ordered.end(), [](auto left, auto right) {
			return std::tie(left->sort_order, left->episode_code) < std::tie(right->sort_order, right->episode_code);
		});
		for (auto episode_ptr : ordered) {
			auto const& episode = *episode_ptr;
			wxString label = to_wx(episode.episode_code);
			if (!episode.current_finalized_revision_id.empty() || episode.status == "finalized")
				label += _(" — Finalized");
			else if (episode.status == "archived")
				label += _(" — Archived");
			else
				label += _(" — Translating");
			episode_list->Append(label);
			episode_ids.push_back(episode.id);
		}
		auto active = std::find(episode_ids.begin(), episode_ids.end(), manager.ActiveEpisodeId());
		if (active != episode_ids.end()) episode_list->SetSelection(static_cast<int>(std::distance(episode_ids.begin(), active)));
		else if (!episode_ids.empty()) episode_list->SetSelection(0);
		UpdateStatus();
		UpdateActions();
	}
	void UpdateStatus() {
		if (manager.HasOpenEpisode()) {
			auto episode = manager.ActiveEpisode();
			status->SetLabel(agi::wxformat(_("Open: %s / %s — project memory is active"),
				to_wx(manager.ActiveProjectName()), to_wx(episode ? episode->episode_code : manager.ActiveEpisodeId())));
		}
		else status->SetLabel(_("No project episode is attached. Ordinary Aegisub mode is active."));
		UpdateActions();
		Layout();
	}
	void RefreshDirectory() {
		try {
			manager.RefreshDirectory();
			PopulateSeasons();
			PopulateProjects();
			if (!SelectedProject().empty()) SyncSelected();
		}
		catch (std::exception const& error) {
			PopulateSeasons();
			PopulateProjects();
			PopulateEpisodes();
			wxMessageBox(agi::wxformat(_("Server is unavailable. The last valid local cache remains usable.\n\n%s"),
				to_wx(error.what())), _("Project"), wxOK | wxICON_WARNING, this);
		}
	}
	void CreateSeason() {
		auto created = ShowSanaeCreateSeasonDialog(context, this);
		if (created.empty()) return;
		preferred_season_id = std::move(created);
		PopulateSeasons();
		PopulateProjects();
		PopulateEpisodes();
	}
	void CreateProject() {
		auto created = ShowSanaeCreateProjectDialog(context, this, SelectedSeason());
		if (created.empty()) return;
		preferred_project_id = std::move(created);
		PopulateProjects();
		SyncSelected();
	}
	void SyncSelected() {
		auto project_id = SelectedProject();
		if (project_id.empty()) return;
		try {
			status->SetLabel(_("Synchronizing project…"));
			Update();
			manager.SyncProject(project_id);
			PopulateEpisodes();
		}
		catch (std::exception const& error) {
			// SyncProject loads the last snapshot before attempting the request,
			// so expose that snapshot when the network is unavailable.
			PopulateEpisodes();
			wxMessageBox(agi::wxformat(_("Synchronization failed. Showing the last valid local cache.\n\n%s"),
				to_wx(error.what())), _("Project"), wxOK | wxICON_WARNING, this);
		}
	}
	void AddEpisode() {
		ShowSanaeAddEpisodeDialog(context, this);
		PopulateEpisodes();
	}
	void AttachEpisode() {
		auto episode_id = SelectedEpisode();
		if (episode_id.empty()) return;
		try {
			manager.AttachEpisode(episode_id);
			UpdateStatus();
		}
		catch (std::exception const& error) { ShowError(error); }
	}
	void EpisodeDetails() {
		auto episode_id = SelectedEpisode();
		if (episode_id.empty()) return;
		auto result = ShowSanaeEpisodeDetailsDialog(context, episode_id, this);
		if (result != SanaeEpisodeDialogResult::None) {
			PopulateEpisodes();
			UpdateStatus();
		}
	}
	void ReplaceSource() {
		auto episode_id = SelectedEpisode();
		if (episode_id.empty()) return;
		if (ShowSanaeReplaceEpisodeSourceDialog(context, episode_id, this)) {
			PopulateEpisodes();
			UpdateStatus();
		}
	}
	void DeleteEpisode() {
		auto episode_id = SelectedEpisode();
		if (episode_id.empty()) return;
		if (ConfirmAndDeleteSanaeEpisode(context, episode_id, this)) {
			PopulateEpisodes();
			UpdateStatus();
		}
	}
	void ShowEpisodeContextMenu(wxContextMenuEvent& event) {
		if (event.GetPosition() != wxDefaultPosition) {
			auto hit = episode_list->HitTest(episode_list->ScreenToClient(event.GetPosition()));
			if (hit != wxNOT_FOUND) episode_list->SetSelection(hit);
		}
		if (SelectedEpisode().empty()) return;
		wxMenu menu;
		auto open_id = wxWindow::NewControlId();
		auto details_id = wxWindow::NewControlId();
		auto replace_id = wxWindow::NewControlId();
		auto delete_id = wxWindow::NewControlId();
		menu.Append(open_id, _("Open episode"));
		menu.Append(details_id, _("Episode details…"));
		menu.AppendSeparator();
		menu.Append(replace_id, _("Replace English subtitles…"));
		menu.Append(delete_id, _("Delete episode…"));
		menu.Bind(wxEVT_MENU, [this](wxCommandEvent&) { AttachEpisode(); }, open_id);
		menu.Bind(wxEVT_MENU, [this](wxCommandEvent&) { EpisodeDetails(); }, details_id);
		menu.Bind(wxEVT_MENU, [this](wxCommandEvent&) { ReplaceSource(); }, replace_id);
		menu.Bind(wxEVT_MENU, [this](wxCommandEvent&) { DeleteEpisode(); }, delete_id);
		PopupMenu(&menu);
	}
	void BatchImport() {
		ShowSanaeBatchImportForActiveProject(context, this);
		PopulateEpisodes();
	}
public:
	explicit ProjectDialog(agi::Context *c)
	: wxDialog(c->parent, -1, _("Open project"), wxDefaultPosition, wxSize(780, 540),
		wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
	, context(c)
	, manager(*c->sanaeProject)
	{
		auto main = new wxBoxSizer(wxVERTICAL);
		auto season_row = new wxBoxSizer(wxHORIZONTAL);
		season_row->Add(new wxStaticText(this, -1, _("Season:")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
		season_row->Add(season_choice = new wxChoice(this, -1), 1, wxRIGHT, 6);
		create_season_button = new wxButton(this, -1, _("Create season…"));
		season_row->Add(create_season_button);
		main->Add(season_row, 0, wxEXPAND | wxBOTTOM, 8);
		auto project_row = new wxBoxSizer(wxHORIZONTAL);
		project_row->Add(new wxStaticText(this, -1, _("Project:")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
		project_row->Add(project_choice = new wxChoice(this, -1), 1, wxRIGHT, 6);
		create_project_button = new wxButton(this, -1, _("Create project…"));
		sync_button = new wxButton(this, -1, _("Synchronize"));
		project_row->Add(create_project_button, 0, wxRIGHT, 6);
		project_row->Add(sync_button);
		main->Add(project_row, 0, wxEXPAND | wxBOTTOM, 8);

		empty_state = new wxStaticText(this, -1, "");
		empty_state->Wrap(700);
		main->Add(empty_state, 0, wxEXPAND | wxBOTTOM, 8);
		main->Add(new wxStaticText(this, -1, _("Episodes:")), 0, wxBOTTOM, 4);
		main->Add(episode_list = new wxListBox(this, -1), 1, wxEXPAND | wxBOTTOM, 8);
		main->Add(status = new wxStaticText(this, -1, ""), 0, wxEXPAND | wxBOTTOM, 8);

		auto episode_actions = new wxBoxSizer(wxHORIZONTAL);
		add_button = new wxButton(this, -1, _("Add episode…"));
		batch_import_button = new wxButton(this, -1, _("Batch import…"));
		episode_actions->Add(add_button, 0, wxRIGHT, 6);
		episode_actions->Add(batch_import_button);
		episode_actions->AddStretchSpacer();
		main->Add(episode_actions, 0, wxEXPAND | wxBOTTOM, 6);

		auto actions = new wxBoxSizer(wxHORIZONTAL);
		attach_button = new wxButton(this, -1, _("Open selected episode"));
		details_button = new wxButton(this, -1, _("Episode details…"));
		close_episode_button = new wxButton(this, -1, _("Detach current episode"));
		auto refresh = new wxButton(this, -1, _("Refresh list"));
		actions->Add(attach_button, 0, wxRIGHT, 6);
		actions->Add(details_button, 0, wxRIGHT, 6);
		actions->Add(close_episode_button, 0, wxRIGHT, 6);
		actions->Add(refresh, 0, wxRIGHT, 6);
		actions->AddStretchSpacer();
		actions->Add(new wxButton(this, wxID_CLOSE));
		main->Add(actions, 0, wxEXPAND);
		SetSizer(main);
		SetMinSize(wxSize(680, 390));
		CentreOnParent();

		refresh->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { RefreshDirectory(); });
		sync_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { SyncSelected(); });
		create_season_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { CreateSeason(); });
		create_project_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { CreateProject(); });
		season_choice->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { PopulateProjects(); SyncSelected(); });
		project_choice->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { SyncSelected(); });
		add_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { AddEpisode(); });
		batch_import_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { BatchImport(); });
		attach_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { AttachEpisode(); });
		details_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { EpisodeDetails(); });
		episode_list->Bind(wxEVT_LISTBOX, [this](wxCommandEvent&) { UpdateActions(); });
		episode_list->Bind(wxEVT_LISTBOX_DCLICK, [this](wxCommandEvent&) { AttachEpisode(); });
		episode_list->Bind(wxEVT_CONTEXT_MENU, [this](wxContextMenuEvent& event) { ShowEpisodeContextMenu(event); });
		close_episode_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
			manager.CloseEpisode();
			UpdateStatus();
		});
		Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { EndModal(wxID_CLOSE); }, wxID_CLOSE);
		RefreshDirectory();
	}
};
}

void ShowSanaeProjectDialog(agi::Context *context) {
	if (!context->sanaeProject->IsEnrolled()) {
		wxMessageBox(_("Connect this device to the Sanae server first using Project → Server connection."),
			_("Open project"), wxOK | wxICON_INFORMATION, context->parent);
		return;
	}
	ProjectDialog(context).ShowModal();
}

std::string ShowSanaeCreateSeasonDialog(agi::Context *context, wxWindow *parent) {
	if (!context->sanaeProject->IsEnrolled()) {
		wxMessageBox(_("Connect this device to the server before creating a season."),
			_("Create season"), wxOK | wxICON_INFORMATION, parent ? parent : context->parent);
		return {};
	}
	CreateSeasonDialog dialog(parent ? parent : context->parent, *context->sanaeProject);
	return dialog.ShowModal() == wxID_OK ? dialog.CreatedId() : std::string();
}

std::string ShowSanaeCreateProjectDialog(agi::Context *context, wxWindow *parent,
	std::string const& preferred_season_id)
{
	if (!context->sanaeProject->IsEnrolled()) {
		wxMessageBox(_("Connect this device to the server before creating a project."),
			_("Create project"), wxOK | wxICON_INFORMATION, parent ? parent : context->parent);
		return {};
	}
	if (context->sanaeProject->Seasons().empty()) {
		try { context->sanaeProject->RefreshDirectory(); }
		catch (std::exception const& error) {
			wxMessageBox(agi::wxformat(_("Could not load the season list.\n\nDetails: %s"), to_wx(error.what())),
				_("Create project"), wxOK | wxICON_ERROR, parent ? parent : context->parent);
			return {};
		}
	}
	if (context->sanaeProject->Seasons().empty()) {
		wxMessageBox(_("Create a season before creating a project."), _("Create project"),
			wxOK | wxICON_INFORMATION, parent ? parent : context->parent);
		return {};
	}
	CreateProjectDialog dialog(parent ? parent : context->parent, *context->sanaeProject,
		preferred_season_id);
	return dialog.ShowModal() == wxID_OK ? dialog.CreatedId() : std::string();
}

void ShowSanaeAddEpisodeDialog(agi::Context *context, wxWindow *parent) {
	auto& manager = *context->sanaeProject;
	auto project_id = manager.ActiveProjectId();
	auto owner = parent ? parent : context->parent;
	if (project_id.empty()) {
		wxMessageBox(_("Open and synchronize a project first."), _("Add episode"),
			wxOK | wxICON_INFORMATION, owner);
		return;
	}
	wxTextEntryDialog code(owner, _("Episode code (for example: 06, 12.5, OVA or SP01):"),
		_("Add episode"), to_wx(suggested_episode_code(manager, project_id)));
	if (code.ShowModal() != wxID_OK || code.GetValue().Trim().empty()) return;
	auto episode_code = from_wx(code.GetValue().Trim());
	double sort_order = 0.0;
	char *end = nullptr;
	double numeric_code = std::strtod(episode_code.c_str(), &end);
	if (end && end != episode_code.c_str() && *end == '\0') sort_order = numeric_code;
	else {
		for (auto const& episode : manager.Episodes())
			if (episode.project_id == project_id) sort_order = std::max(sort_order, episode.sort_order + 1.0);
	}
	auto path = OpenFileSelector(_("Choose English subtitles (ENSUB)"), "Path/Last/Subtitles", "", "",
		SubtitleFormat::GetWildcards(0), owner);
	if (path.empty()) return;
	try {
		manager.CreateEpisode(project_id, episode_code, sort_order, path);
		wxMessageBox(_("The episode was created and attached to the current subtitles."),
			_("Project"), wxOK | wxICON_INFORMATION, owner);
	}
	catch (std::exception const& error) {
		wxMessageBox(agi::wxformat(_("The episode could not be added.\n\nDetails: %s"), to_wx(error.what())),
			_("Add episode"), wxOK | wxICON_ERROR, owner);
	}
}

void ShowSanaeBatchImportForActiveProject(agi::Context *context, wxWindow *parent) {
	auto& manager = *context->sanaeProject;
	auto project_id = manager.ActiveProjectId();
	auto owner = parent ? parent : context->parent;
	if (project_id.empty()) {
		wxMessageBox(_("Open and synchronize a project first."), _("Batch import"),
			wxOK | wxICON_INFORMATION, owner);
		return;
	}
	std::string project_name = manager.ActiveProjectName().empty()
		? project_id : manager.ActiveProjectName();
	try {
		manager.SyncProject(project_id);
	}
	catch (std::exception const& error) {
		wxMessageBox(agi::wxformat(
			_("Synchronization failed. The batch wizard will use the last local snapshot; uploads can be retried when the server is available.\n\nDetails: %s"),
			to_wx(error.what())), _("Batch import"), wxOK | wxICON_WARNING, owner);
	}
	ShowSanaeBatchImportDialog(context, project_id, project_name);
}
