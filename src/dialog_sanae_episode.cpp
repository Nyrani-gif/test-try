// Copyright (c) 2026, Aegisub Sanae contributors
#include "dialog_sanae_episode.h"

#include "ass_file.h"
#include "compat.h"
#include "format.h"
#include "include/aegisub/context.h"
#include "sanae_api.h"
#include "sanae_project.h"
#include "subs_controller.h"
#include "subtitle_format.h"
#include "utils.h"

#include <libaegisub/ass/time.h>
#include <libaegisub/io.h>

#include <algorithm>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <wx/button.h>
#include <wx/choice.h>
#include <wx/dialog.h>
#include <wx/filedlg.h>
#include <wx/listctrl.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/utils.h>

namespace {
wxString status_text(SanaeEpisodeInfo const& episode) {
	if (episode.IsDeleted()) return _("Deleted");
	if (episode.status == "finalized" || !episode.current_finalized_revision_id.empty())
		return _("Finalized");
	if (episode.status == "archived") return _("Archived");
	return _("Translating");
}

wxString date_text(std::string value) {
	if (value.empty()) return _("Unknown");
	std::replace(value.begin(), value.end(), 'T', ' ');
	if (!value.empty() && value.back() == 'Z') value.pop_back();
	return to_wx(value);
}

wxString kind_text(std::string const& value) {
	return value == "source_ensub" ? _("English subtitles (ENSUB)")
		: value == "compact_rusub" ? _("Compact Russian subtitles") : to_wx(value);
}

wxString change_text(SanaeSemanticDiffKind kind) {
	switch (kind) {
		case SanaeSemanticDiffKind::Changed: return _("Changed");
		case SanaeSemanticDiffKind::Added: return _("Added");
		case SanaeSemanticDiffKind::Removed: return _("Removed");
	}
	return wxString();
}

wxString recovery_device_text(SanaeRecoverySnapshotInfo const& snapshot) {
	wxString display = to_wx(snapshot.device_display_name);
	wxString device = to_wx(snapshot.device_name);
	if (display.empty()) display = _("Unknown user");
	return device.empty() ? display : display + " · " + device;
}

wxString recovery_size_text(std::size_t bytes) {
	if (bytes < 1024 * 1024)
		return agi::wxformat(_("%.1f KiB"), static_cast<double>(bytes) / 1024.0);
	return agi::wxformat(_("%.1f MiB"), static_cast<double>(bytes) / (1024.0 * 1024.0));
}

wxString recovery_filename(SanaeEpisodeInfo const& episode,
	SanaeRecoverySnapshotInfo const& snapshot)
{
	std::string stamp;
	for (char value : snapshot.created_at) {
		if (value >= '0' && value <= '9') stamp.push_back(value);
		if (stamp.size() == 12) break;
	}
	if (stamp.size() < 12) stamp = "snapshot";
	return agi::wxformat("EP%s_recovery_%s.ass", to_wx(episode.episode_code), to_wx(stamp));
}

enum {
	ID_RECOVERY_COMPARE = wxID_HIGHEST + 330,
	ID_RECOVERY_OPEN,
	ID_RECOVERY_SAVE
};

int choose_recovery_action(wxWindow *parent, SanaeRecoverySnapshotInfo const& snapshot) {
	wxDialog dialog(parent, -1, _("Restore recovery copy"), wxDefaultPosition,
		wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
	auto main = new wxBoxSizer(wxVERTICAL);
	main->Add(new wxStaticText(&dialog, -1, agi::wxformat(
		_("Saved: %s\nDevice: %s\nSize: %s\n\n"
		  "Opening the recovered copy will first ask where to save it and will not overwrite the current file silently."),
		date_text(snapshot.created_at), recovery_device_text(snapshot),
		recovery_size_text(snapshot.size_bytes))), 0, wxEXPAND | wxBOTTOM, 12);
	auto actions = new wxBoxSizer(wxHORIZONTAL);
	actions->Add(new wxButton(&dialog, ID_RECOVERY_COMPARE, _("Compare with current version")), 0, wxRIGHT, 6);
	actions->Add(new wxButton(&dialog, ID_RECOVERY_OPEN, _("Open recovered copy")), 0, wxRIGHT, 6);
	actions->Add(new wxButton(&dialog, ID_RECOVERY_SAVE, _("Save as…")));
	main->Add(actions, 0, wxEXPAND | wxBOTTOM, 10);
	main->Add(dialog.CreateStdDialogButtonSizer(wxCANCEL), 0, wxEXPAND);
	dialog.SetSizerAndFit(main);
	dialog.SetMinSize(wxSize(680, -1));
	dialog.CentreOnParent();
	dialog.Bind(wxEVT_BUTTON, [&](wxCommandEvent& event) { dialog.EndModal(event.GetId()); },
		ID_RECOVERY_COMPARE, ID_RECOVERY_SAVE);
	return dialog.ShowModal();
}

class SemanticDiffDialog final : public wxDialog {
public:
	SemanticDiffDialog(wxWindow *parent, wxString const& title,
		SanaeSemanticDiff const& diff, bool confirmation)
	: wxDialog(parent, -1, title, wxDefaultPosition, wxSize(900, 590),
		wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
	{
		auto main = new wxBoxSizer(wxVERTICAL);
		main->Add(new wxStaticText(this, -1, agi::wxformat(
			_("Unchanged lines: %d\nChanged: %d\nAdded: %d\nRemoved: %d"),
			static_cast<int>(diff.unchanged), static_cast<int>(diff.changed),
			static_cast<int>(diff.added), static_cast<int>(diff.removed))),
			0, wxEXPAND | wxBOTTOM, 8);
		auto list = new wxListCtrl(this, -1, wxDefaultPosition, wxDefaultSize,
			wxLC_REPORT | wxLC_SINGLE_SEL | wxLC_HRULES | wxLC_VRULES);
		list->InsertColumn(0, _("Time"), wxLIST_FORMAT_LEFT, 105);
		list->InsertColumn(1, _("Change"), wxLIST_FORMAT_LEFT, 90);
		list->InsertColumn(2, _("Before"), wxLIST_FORMAT_LEFT, 320);
		list->InsertColumn(3, _("After"), wxLIST_FORMAT_LEFT, 320);
		for (auto const& value : diff.entries) {
			auto row = list->InsertItem(list->GetItemCount(),
				to_wx(agi::Time(value.start).GetAssFormatted(true)));
			list->SetItem(row, 1, change_text(value.kind));
			list->SetItem(row, 2, to_wx(value.before.empty() ? "—" : value.before));
			list->SetItem(row, 3, to_wx(value.after.empty() ? "—" : value.after));
		}
		main->Add(list, 1, wxEXPAND | wxBOTTOM, 8);
		main->Add(CreateStdDialogButtonSizer(confirmation ? wxOK | wxCANCEL : wxOK),
			0, wxEXPAND);
		SetSizer(main);
		SetMinSize(wxSize(720, 420));
		CentreOnParent();
	}
};

void show_file_preview(wxWindow *parent, wxString const& title, std::string const& data) {
	wxDialog dialog(parent, -1, title, wxDefaultPosition, wxSize(900, 650),
		wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
	auto main = new wxBoxSizer(wxVERTICAL);
	main->Add(new wxTextCtrl(&dialog, -1, to_wx(data), wxDefaultPosition, wxDefaultSize,
		wxTE_MULTILINE | wxTE_READONLY | wxTE_DONTWRAP), 1, wxEXPAND | wxBOTTOM, 8);
	main->Add(dialog.CreateStdDialogButtonSizer(wxOK), 0, wxEXPAND);
	dialog.SetSizer(main);
	dialog.CentreOnParent();
	dialog.ShowModal();
}

bool confirm_source_replace(wxWindow *parent, SanaeEpisodeInfo const& episode,
	SanaeSemanticDiff const& diff)
{
	wxString warning;
	if (!episode.current_finalized_revision_id.empty())
		warning = _("\n\nThis episode is finalized. Replacing ENSUB will return it to ‘Translating’; old finalized revisions remain in history.");
	wxMessageDialog summary(parent, agi::wxformat(
		_("The new ENSUB was compared with the current source.\n\n"
		  "Unchanged lines: %d\nChanged: %d\nAdded: %d\nRemoved: %d%s\n\nReplace the English subtitles?"),
		static_cast<int>(diff.unchanged), static_cast<int>(diff.changed),
		static_cast<int>(diff.added), static_cast<int>(diff.removed), warning),
		_("Replace English subtitles"), wxYES_NO | wxCANCEL | wxICON_QUESTION);
	summary.SetYesNoLabels(_("View changes"), _("Replace source"));
	for (;;) {
		auto answer = summary.ShowModal();
		if (answer == wxID_NO) return true;
		if (answer != wxID_YES) return false;
		SemanticDiffDialog(parent, _("ENSUB changes"), diff, false).ShowModal();
	}
}

class EpisodeDetailsDialog final : public wxDialog {
	agi::Context *context;
	SanaeProjectManager& manager;
	std::string episode_id;
	SanaeEpisodeDetails details;
	wxStaticText *heading;
	wxStaticText *state;
	wxStaticText *source;
	wxStaticText *file_state;
	wxListCtrl *file_list;
	wxStaticText *revision_state;
	wxListCtrl *revision_list;
	wxButton *preview_file;
	wxButton *save_file;
	wxButton *preview_revision;
	wxButton *compare_revisions;
	std::vector<SanaeRecoverySnapshotInfo> recovery_snapshots;
	wxStaticText *recovery_state;
	wxListCtrl *recovery_list;
	wxButton *preview_recovery;
	wxButton *save_recovery;
	wxButton *restore_recovery;
	wxButton *delete_recovery;
	SanaeEpisodeDialogResult result = SanaeEpisodeDialogResult::None;

	long SelectedFile() const {
		return file_list->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
	}
	long SelectedRevision() const {
		return revision_list->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
	}
	long SelectedRecovery() const {
		return recovery_list->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
	}
	void UpdateButtons() {
		preview_file->Enable(SelectedFile() >= 0);
		save_file->Enable(SelectedFile() >= 0);
		preview_revision->Enable(SelectedRevision() >= 0);
		compare_revisions->Enable(details.finalized_revisions.size() >= 2);
		bool recovery_selected = SelectedRecovery() >= 0;
		preview_recovery->Enable(recovery_selected);
		save_recovery->Enable(recovery_selected);
		restore_recovery->Enable(recovery_selected);
		delete_recovery->Enable(recovery_selected);
	}
	void Load() {
		details = manager.GetEpisodeDetails(episode_id);
		heading->SetLabel(agi::wxformat(_("Episode %s"), to_wx(details.episode.episode_code)));
		state->SetLabel(agi::wxformat(_("Status: %s\nCreated: %s"),
			status_text(details.episode), date_text(details.episode.created_at)));
		auto current = std::find_if(details.files.begin(), details.files.end(), [&](auto const& file) {
			return file.id == details.episode.current_source_file_id;
		});
		if (current == details.files.end()) source->SetLabel(_("Current ENSUB: metadata unavailable"));
		else source->SetLabel(agi::wxformat(
			_("Current ENSUB: version %d · %s · %d bytes"),
			current->revision_number, date_text(current->created_at),
			static_cast<int>(current->size_bytes)));

		file_list->DeleteAllItems();
		std::sort(details.files.begin(), details.files.end(), [](auto const& left, auto const& right) {
			return std::tie(left.kind, left.revision_number) < std::tie(right.kind, right.revision_number);
		});
		file_state->SetLabel(details.files.empty()
			? _("No episode files are available.") : wxString());
		file_state->Show(details.files.empty());
		file_list->Show(!details.files.empty());
		for (std::size_t i = 0; i < details.files.size(); ++i) {
			auto const& file = details.files[i];
			auto row = file_list->InsertItem(file_list->GetItemCount(), kind_text(file.kind));
			file_list->SetItem(row, 1, to_wx(std::to_string(file.revision_number)));
			file_list->SetItem(row, 2, date_text(file.created_at));
			file_list->SetItem(row, 3, agi::wxformat("%d", static_cast<int>(file.size_bytes)));
			file_list->SetItemData(row, static_cast<long>(i));
			if (file.id == details.episode.current_source_file_id)
				file_list->SetItem(row, 0, kind_text(file.kind) + _(" (current)"));
		}

		revision_list->DeleteAllItems();
		std::sort(details.finalized_revisions.begin(), details.finalized_revisions.end(),
			[](auto const& left, auto const& right) { return left.revision_number < right.revision_number; });
		revision_state->SetLabel(details.finalized_revisions.empty()
			? _("This episode has not been finalized yet.") : wxString());
		revision_state->Show(details.finalized_revisions.empty());
		revision_list->Show(!details.finalized_revisions.empty());
		for (std::size_t i = 0; i < details.finalized_revisions.size(); ++i) {
			auto const& revision = details.finalized_revisions[i];
			auto row = revision_list->InsertItem(revision_list->GetItemCount(),
				agi::wxformat(_("Version %d"), revision.revision_number));
			revision_list->SetItem(row, 1, date_text(revision.created_at));
			auto source_file = manager.FindFile(revision.source_file_id);
			revision_list->SetItem(row, 2, source_file
				? agi::wxformat(_("ENSUB version %d"), source_file->revision_number) : _("Unknown"));
			revision_list->SetItemData(row, static_cast<long>(i));
		}

		recovery_list->DeleteAllItems();
		try {
			recovery_snapshots = manager.ListRecoverySnapshots(episode_id);
			recovery_state->SetLabel(recovery_snapshots.empty()
				? _("No server recovery copies have been saved for this episode.")
				: _("The server keeps up to three copies per device."));
		}
		catch (std::exception const& error) {
			recovery_snapshots = manager.CachedRecoverySnapshots(episode_id);
			recovery_state->SetLabel(agi::wxformat(
				_("The server list is unavailable; showing cached metadata. Details: %s"),
				to_wx(error.what())));
		}
		std::sort(recovery_snapshots.begin(), recovery_snapshots.end(),
			[](auto const& left, auto const& right) { return left.created_at > right.created_at; });
		for (std::size_t i = 0; i < recovery_snapshots.size(); ++i) {
			auto const& snapshot = recovery_snapshots[i];
			auto row = recovery_list->InsertItem(recovery_list->GetItemCount(),
				date_text(snapshot.created_at));
			recovery_list->SetItem(row, 1, recovery_device_text(snapshot));
			recovery_list->SetItem(row, 2, recovery_size_text(snapshot.size_bytes));
			recovery_list->SetItem(row, 3,
				snapshot.source_file_id == details.episode.current_source_file_id
					? _("Current ENSUB") : _("Previous source version"));
			recovery_list->SetItemData(row, static_cast<long>(i));
		}
		if (details.local_cache_only)
			state->SetLabel(state->GetLabel() + _("\nOffline: showing the last local snapshot."));
		UpdateButtons();
		Layout();
	}
	SanaeEpisodeFileInfo const *SelectedFileInfo() const {
		auto row = SelectedFile();
		if (row < 0) return nullptr;
		auto index = static_cast<std::size_t>(file_list->GetItemData(row));
		return index < details.files.size() ? &details.files[index] : nullptr;
	}
	SanaeFinalizedRevisionInfo const *SelectedRevisionInfo() const {
		auto row = SelectedRevision();
		if (row < 0) return nullptr;
		auto index = static_cast<std::size_t>(revision_list->GetItemData(row));
		return index < details.finalized_revisions.size() ? &details.finalized_revisions[index] : nullptr;
	}
	SanaeRecoverySnapshotInfo const *SelectedRecoveryInfo() const {
		auto row = SelectedRecovery();
		if (row < 0) return nullptr;
		auto index = static_cast<std::size_t>(recovery_list->GetItemData(row));
		return index < recovery_snapshots.size() ? &recovery_snapshots[index] : nullptr;
	}
	void PreviewFile(std::string const& file_id, wxString const& title) {
		try { show_file_preview(this, title, manager.ReadEpisodeFile(file_id)); }
		catch (std::exception const& error) {
			wxMessageBox(agi::wxformat(_("The file could not be opened.\n\nDetails: %s"),
				to_wx(error.what())), _("Episode details"), wxOK | wxICON_ERROR, this);
		}
	}
	void PreviewSelectedFile() {
		auto file = SelectedFileInfo();
		if (file) PreviewFile(file->id, kind_text(file->kind));
	}
	void PreviewSelectedRevision() {
		auto revision = SelectedRevisionInfo();
		if (revision) PreviewFile(revision->compact_rusub_file_id,
			agi::wxformat(_("Final version %d"), revision->revision_number));
	}
	void SaveSelectedFile() {
		auto file = SelectedFileInfo();
		if (!file) return;
		wxFileDialog dialog(this, _("Save subtitle copy"), wxEmptyString,
			agi::wxformat("episode-%s-%s-v%d.ass", to_wx(details.episode.episode_code),
				file->kind == "source_ensub" ? "ensub" : "rusub", file->revision_number),
			_("Advanced SubStation Alpha files (*.ass)|*.ass|All files (*.*)|*.*"),
			wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
		if (dialog.ShowModal() != wxID_OK) return;
		try {
			auto data = manager.ReadEpisodeFile(file->id);
			agi::io::Save output(agi::fs::path(from_wx(dialog.GetPath())), true);
			output.Get().write(data.data(), static_cast<std::streamsize>(data.size()));
		}
		catch (std::exception const& error) {
			wxMessageBox(agi::wxformat(_("The copy could not be saved.\n\nDetails: %s"),
				to_wx(error.what())), _("Episode details"), wxOK | wxICON_ERROR, this);
		}
	}
	bool SaveRecoveryCopy(SanaeRecoverySnapshotInfo const& snapshot,
		agi::fs::path *saved_path = nullptr)
	{
		wxFileDialog dialog(this, _("Save recovery copy"), wxEmptyString,
			recovery_filename(details.episode, snapshot),
			_("Advanced SubStation Alpha files (*.ass)|*.ass|All files (*.*)|*.*"),
			wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
		if (dialog.ShowModal() != wxID_OK) return false;
		try {
			wxBusyCursor busy;
			auto data = manager.ReadRecoverySnapshot(snapshot);
			agi::fs::path path(from_wx(dialog.GetPath()));
			agi::io::Save output(path, true);
			output.Get().write(data.data(), static_cast<std::streamsize>(data.size()));
			if (saved_path) *saved_path = path;
			return true;
		}
		catch (std::exception const& error) {
			wxMessageBox(agi::wxformat(
				_("The recovery copy could not be saved.\n\nDetails: %s"),
				to_wx(error.what())), _("Recovery copies"), wxOK | wxICON_ERROR, this);
			return false;
		}
	}
	void PreviewSelectedRecovery() {
		auto snapshot = SelectedRecoveryInfo();
		if (!snapshot) return;
		try {
			wxBusyCursor busy;
			show_file_preview(this, _("Recovery copy (read-only)"),
				manager.ReadRecoverySnapshot(*snapshot));
		}
		catch (std::exception const& error) {
			wxMessageBox(agi::wxformat(
				_("The recovery copy could not be opened.\n\nDetails: %s"),
				to_wx(error.what())), _("Recovery copies"), wxOK | wxICON_ERROR, this);
		}
	}
	void RestoreSelectedRecovery() {
		auto snapshot = SelectedRecoveryInfo();
		if (!snapshot) return;
		for (;;) {
			auto action = choose_recovery_action(this, *snapshot);
			if (action == ID_RECOVERY_COMPARE) {
				try {
					wxBusyCursor busy;
					auto diff = manager.CompareRecoverySnapshot(*snapshot);
					SemanticDiffDialog(this, _("Recovery copy compared with current ASS"),
						diff, false).ShowModal();
				}
				catch (std::exception const& error) {
					wxMessageBox(agi::wxformat(
						_("The recovery copy could not be compared.\n\nDetails: %s"),
						to_wx(error.what())), _("Recovery copies"),
						wxOK | wxICON_ERROR, this);
				}
				continue;
			}
			if (action == ID_RECOVERY_SAVE) {
				SaveRecoveryCopy(*snapshot);
				return;
			}
			if (action != ID_RECOVERY_OPEN) return;
			agi::fs::path path;
			if (!SaveRecoveryCopy(*snapshot, &path)) return;
			if (context->subsController->TryToClose(true) == wxCANCEL) return;
			try {
				context->subsController->Load(path, "UTF-8");
				if (snapshot->source_file_id == details.episode.current_source_file_id)
					manager.AttachEpisode(snapshot->episode_id);
				else wxMessageBox(
					_("This copy belongs to a previous ENSUB version. It was opened as a normal ASS and was not attached to the current server episode."),
					_("Recovery copy opened"), wxOK | wxICON_INFORMATION, this);
				result = SanaeEpisodeDialogResult::Changed;
				EndModal(wxID_OK);
			}
			catch (std::exception const& error) {
				wxMessageBox(agi::wxformat(
					_("The saved recovery copy could not be opened.\n\nDetails: %s"),
					to_wx(error.what())), _("Recovery copies"), wxOK | wxICON_ERROR, this);
			}
			return;
		}
	}
	void DeleteSelectedRecovery() {
		auto snapshot = SelectedRecoveryInfo();
		if (!snapshot) return;
		if (wxMessageBox(agi::wxformat(
			_("Delete this recovery copy?\n\n%s\n%s"),
			date_text(snapshot->created_at), recovery_device_text(*snapshot)),
			_("Delete recovery copy"), wxYES_NO | wxNO_DEFAULT | wxICON_WARNING,
			this) != wxYES) return;
		try {
			wxBusyCursor busy;
			manager.DeleteRecoverySnapshot(snapshot->id);
			Load();
		}
		catch (std::exception const& error) {
			wxMessageBox(agi::wxformat(
				_("The recovery copy could not be deleted.\n\nDetails: %s"),
				to_wx(error.what())), _("Delete recovery copy"),
				wxOK | wxICON_ERROR, this);
		}
	}
	void Replace() {
		if (!ShowSanaeReplaceEpisodeSourceDialog(context, episode_id, this)) return;
		result = SanaeEpisodeDialogResult::Changed;
		Load();
	}
	void Delete() {
		if (!ConfirmAndDeleteSanaeEpisode(context, episode_id, this)) return;
		result = SanaeEpisodeDialogResult::Deleted;
		EndModal(wxID_OK);
	}
	void Compare() {
		if (details.finalized_revisions.size() < 2) return;
		wxArrayString choices;
		for (auto const& revision : details.finalized_revisions)
			choices.Add(agi::wxformat(_("Version %d · %s"), revision.revision_number,
				date_text(revision.created_at)));
		wxDialog dialog(this, -1, _("Compare finalized versions"));
		auto main = new wxBoxSizer(wxVERTICAL);
		auto fields = new wxFlexGridSizer(2, 6, 8);
		fields->Add(new wxStaticText(&dialog, -1, _("Before:")), 0, wxALIGN_CENTER_VERTICAL);
		auto before = new wxChoice(&dialog, -1, wxDefaultPosition, wxDefaultSize, choices);
		fields->Add(before, 1, wxEXPAND);
		fields->Add(new wxStaticText(&dialog, -1, _("After:")), 0, wxALIGN_CENTER_VERTICAL);
		auto after = new wxChoice(&dialog, -1, wxDefaultPosition, wxDefaultSize, choices);
		fields->Add(after, 1, wxEXPAND);
		fields->AddGrowableCol(1, 1);
		before->SetSelection(static_cast<int>(details.finalized_revisions.size() - 2));
		after->SetSelection(static_cast<int>(details.finalized_revisions.size() - 1));
		main->Add(fields, 1, wxEXPAND | wxBOTTOM, 10);
		main->Add(dialog.CreateStdDialogButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND);
		dialog.SetSizerAndFit(main);
		dialog.SetSize(wxSize(540, dialog.GetSize().GetHeight()));
		dialog.CentreOnParent();
		if (dialog.ShowModal() != wxID_OK || before->GetSelection() == after->GetSelection()) return;
		try {
			auto diff = manager.CompareFinalizedRevisions(
				details.finalized_revisions[before->GetSelection()].id,
				details.finalized_revisions[after->GetSelection()].id);
			SemanticDiffDialog(this, _("Changes between finalized versions"), diff, false).ShowModal();
		}
		catch (std::exception const& error) {
			wxMessageBox(agi::wxformat(_("The versions could not be compared.\n\nDetails: %s"),
				to_wx(error.what())), _("Compare finalized versions"), wxOK | wxICON_ERROR, this);
		}
	}
public:
	EpisodeDetailsDialog(agi::Context *c, std::string id, wxWindow *parent)
	: wxDialog(parent ? parent : c->parent, -1, _("Episode details"), wxDefaultPosition,
		wxSize(920, 820), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
	, context(c), manager(*c->sanaeProject), episode_id(std::move(id))
	{
		auto main = new wxBoxSizer(wxVERTICAL);
		heading = new wxStaticText(this, -1, wxString());
		auto font = heading->GetFont(); font.MakeBold(); heading->SetFont(font);
		main->Add(heading, 0, wxEXPAND | wxBOTTOM, 5);
		main->Add(state = new wxStaticText(this, -1, wxString()), 0, wxEXPAND | wxBOTTOM, 8);
		main->Add(source = new wxStaticText(this, -1, wxString()), 0, wxEXPAND | wxBOTTOM, 10);
		main->Add(new wxStaticText(this, -1, _("Episode files:")), 0, wxBOTTOM, 4);
		main->Add(file_state = new wxStaticText(this, -1, wxString()),
			0, wxEXPAND | wxBOTTOM, 4);
		file_state->Hide();
		file_list = new wxListCtrl(this, -1, wxDefaultPosition, wxSize(-1, 170),
			wxLC_REPORT | wxLC_SINGLE_SEL | wxLC_HRULES | wxLC_VRULES);
		file_list->InsertColumn(0, _("Type"), wxLIST_FORMAT_LEFT, 250);
		file_list->InsertColumn(1, _("Version"), wxLIST_FORMAT_LEFT, 80);
		file_list->InsertColumn(2, _("Uploaded"), wxLIST_FORMAT_LEFT, 190);
		file_list->InsertColumn(3, _("Bytes"), wxLIST_FORMAT_RIGHT, 90);
		main->Add(file_list, 0, wxEXPAND | wxBOTTOM, 6);
		auto file_buttons = new wxBoxSizer(wxHORIZONTAL);
		file_buttons->Add(preview_file = new wxButton(this, -1, _("View")), 0, wxRIGHT, 6);
		file_buttons->Add(save_file = new wxButton(this, -1, _("Save a copy…")));
		file_buttons->AddStretchSpacer();
		file_buttons->Add(new wxButton(this, wxID_HIGHEST + 100, _("Replace ENSUB…")));
		main->Add(file_buttons, 0, wxEXPAND | wxBOTTOM, 10);

		main->Add(new wxStaticText(this, -1, _("Finalized versions:")), 0, wxBOTTOM, 4);
		main->Add(revision_state = new wxStaticText(this, -1, wxString()),
			0, wxEXPAND | wxBOTTOM, 4);
		revision_state->Hide();
		revision_list = new wxListCtrl(this, -1, wxDefaultPosition, wxSize(-1, 150),
			wxLC_REPORT | wxLC_SINGLE_SEL | wxLC_HRULES | wxLC_VRULES);
		revision_list->InsertColumn(0, _("Version"), wxLIST_FORMAT_LEFT, 120);
		revision_list->InsertColumn(1, _("Created"), wxLIST_FORMAT_LEFT, 210);
		revision_list->InsertColumn(2, _("Source"), wxLIST_FORMAT_LEFT, 180);
		main->Add(revision_list, 1, wxEXPAND | wxBOTTOM, 6);
		auto revision_buttons = new wxBoxSizer(wxHORIZONTAL);
		revision_buttons->Add(preview_revision = new wxButton(this, -1, _("View selected version")), 0, wxRIGHT, 6);
		revision_buttons->Add(compare_revisions = new wxButton(this, -1, _("Compare finalized versions…")));
		main->Add(revision_buttons, 0, wxEXPAND | wxBOTTOM, 10);

		main->Add(new wxStaticText(this, -1, _("Recovery copies:")), 0, wxBOTTOM, 4);
		main->Add(recovery_state = new wxStaticText(this, -1, wxString()),
			0, wxEXPAND | wxBOTTOM, 4);
		recovery_list = new wxListCtrl(this, -1, wxDefaultPosition, wxSize(-1, 145),
			wxLC_REPORT | wxLC_SINGLE_SEL | wxLC_HRULES | wxLC_VRULES);
		recovery_list->InsertColumn(0, _("Saved"), wxLIST_FORMAT_LEFT, 190);
		recovery_list->InsertColumn(1, _("Device"), wxLIST_FORMAT_LEFT, 245);
		recovery_list->InsertColumn(2, _("Size"), wxLIST_FORMAT_RIGHT, 90);
		recovery_list->InsertColumn(3, _("Source"), wxLIST_FORMAT_LEFT, 190);
		main->Add(recovery_list, 0, wxEXPAND | wxBOTTOM, 6);
		auto recovery_buttons = new wxBoxSizer(wxHORIZONTAL);
		recovery_buttons->Add(preview_recovery = new wxButton(this, -1, _("View…")),
			0, wxRIGHT, 6);
		recovery_buttons->Add(save_recovery = new wxButton(this, -1, _("Save a copy…")),
			0, wxRIGHT, 6);
		recovery_buttons->Add(restore_recovery = new wxButton(this, -1, _("Restore…")),
			0, wxRIGHT, 6);
		recovery_buttons->AddStretchSpacer();
		recovery_buttons->Add(delete_recovery = new wxButton(this, -1, _("Delete…")));
		main->Add(recovery_buttons, 0, wxEXPAND | wxBOTTOM, 10);

		auto buttons = new wxBoxSizer(wxHORIZONTAL);
		buttons->Add(new wxButton(this, wxID_DELETE, _("Delete episode…")));
		buttons->AddStretchSpacer();
		buttons->Add(new wxButton(this, wxID_CLOSE));
		main->Add(buttons, 0, wxEXPAND);
		SetSizer(main);
		SetMinSize(wxSize(760, 650));
		CentreOnParent();

		preview_file->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { PreviewSelectedFile(); });
		save_file->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { SaveSelectedFile(); });
		preview_revision->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { PreviewSelectedRevision(); });
		compare_revisions->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Compare(); });
		preview_recovery->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { PreviewSelectedRecovery(); });
		save_recovery->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
			if (auto snapshot = SelectedRecoveryInfo()) SaveRecoveryCopy(*snapshot);
		});
		restore_recovery->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { RestoreSelectedRecovery(); });
		delete_recovery->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { DeleteSelectedRecovery(); });
		Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Replace(); }, wxID_HIGHEST + 100);
		Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Delete(); }, wxID_DELETE);
		Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { EndModal(wxID_CLOSE); }, wxID_CLOSE);
		file_list->Bind(wxEVT_LIST_ITEM_SELECTED, [this](wxListEvent&) { UpdateButtons(); });
		revision_list->Bind(wxEVT_LIST_ITEM_SELECTED, [this](wxListEvent&) { UpdateButtons(); });
		recovery_list->Bind(wxEVT_LIST_ITEM_SELECTED, [this](wxListEvent&) { UpdateButtons(); });
		file_list->Bind(wxEVT_LIST_ITEM_ACTIVATED, [this](wxListEvent&) { PreviewSelectedFile(); });
		revision_list->Bind(wxEVT_LIST_ITEM_ACTIVATED, [this](wxListEvent&) { PreviewSelectedRevision(); });
		recovery_list->Bind(wxEVT_LIST_ITEM_ACTIVATED, [this](wxListEvent&) { PreviewSelectedRecovery(); });
		Load();
	}
	SanaeEpisodeDialogResult Result() const { return result; }
};
}

bool ShowSanaeReplaceEpisodeSourceDialog(agi::Context *context,
	std::string const& episode_id, wxWindow *parent, agi::fs::path const& selected_path)
{
	auto owner = parent ? parent : context->parent;
	auto path = selected_path;
	if (path.empty()) {
		path = OpenFileSelector(_("Choose replacement English subtitles (ENSUB)"),
			"Path/Last/Subtitles", "", "", SubtitleFormat::GetWildcards(0), owner);
		if (path.empty()) return false;
	}
	for (;;) {
		try {
			auto diff = context->sanaeProject->CompareEpisodeSource(episode_id, path);
			auto episode = context->sanaeProject->FindEpisode(episode_id);
			if (!episode) throw std::runtime_error("Episode is not present in the local project snapshot");
			if (!confirm_source_replace(owner, *episode, diff)) return false;
			context->sanaeProject->ReplaceEpisodeSource(episode_id, path);
			wxMessageBox(_("English subtitles were replaced. The current Russian ASS was not changed."),
				_("Replace English subtitles"), wxOK | wxICON_INFORMATION, owner);
			return true;
		}
		catch (SanaeApiError const& error) {
			if (error.Code() != "source_changed") {
				wxMessageBox(agi::wxformat(_("The source could not be replaced.\n\nDetails: %s"),
					to_wx(error.what())), _("Replace English subtitles"), wxOK | wxICON_ERROR, owner);
				return false;
			}
			wxMessageDialog conflict(owner,
				_("The English subtitles of this episode were already changed on the server after your last synchronization.\n\nSynchronize the project and compare again?"),
				_("Source changed"), wxYES_NO | wxICON_WARNING);
			conflict.SetYesNoLabels(_("Synchronize"), _("Cancel"));
			if (conflict.ShowModal() != wxID_YES) return false;
			try { context->sanaeProject->SyncProject(context->sanaeProject->ActiveProjectId()); }
			catch (std::exception const& sync_error) {
				wxMessageBox(agi::wxformat(_("The project could not be synchronized.\n\nDetails: %s"),
					to_wx(sync_error.what())), _("Source changed"), wxOK | wxICON_ERROR, owner);
				return false;
			}
		}
		catch (std::exception const& error) {
			wxMessageBox(agi::wxformat(_("The source could not be replaced.\n\nDetails: %s"),
				to_wx(error.what())), _("Replace English subtitles"), wxOK | wxICON_ERROR, owner);
			return false;
		}
	}
}

bool ConfirmAndDeleteSanaeEpisode(agi::Context *context,
	std::string const& episode_id, wxWindow *parent)
{
	auto owner = parent ? parent : context->parent;
	auto episode = context->sanaeProject->FindEpisode(episode_id);
	if (!episode || episode->IsDeleted()) return false;
	wxMessageDialog confirm(owner, agi::wxformat(
		_("Delete episode %s from the project?\n\n"
		  "The episode will stop appearing as an active project episode. "
		  "The server keeps its internal history."), to_wx(episode->episode_code)),
		_("Delete episode"), wxYES_NO | wxNO_DEFAULT | wxICON_WARNING);
	confirm.SetYesNoLabels(_("Delete"), _("Cancel"));
	if (confirm.ShowModal() != wxID_YES) return false;
	try {
		context->sanaeProject->DeleteEpisode(episode_id);
		return true;
	}
	catch (std::exception const& error) {
		wxMessageBox(agi::wxformat(_("The episode could not be deleted.\n\nDetails: %s"),
			to_wx(error.what())), _("Delete episode"), wxOK | wxICON_ERROR, owner);
		return false;
	}
}

SanaeEpisodeDialogResult ShowSanaeEpisodeDetailsDialog(agi::Context *context,
	std::string const& episode_id, wxWindow *parent)
{
	try {
		EpisodeDetailsDialog dialog(context, episode_id, parent);
		dialog.ShowModal();
		return dialog.Result();
	}
	catch (std::exception const& error) {
		wxMessageBox(agi::wxformat(_("Episode details could not be loaded.\n\nDetails: %s"),
			to_wx(error.what())), _("Episode details"), wxOK | wxICON_ERROR,
			parent ? parent : context->parent);
		return SanaeEpisodeDialogResult::None;
	}
}
