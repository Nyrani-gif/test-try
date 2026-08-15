// Copyright (c) 2026, Aegisub Sanae contributors

#include "dialog_sanae_batch_import.h"

#include "compat.h"
#include "format.h"
#include "include/aegisub/context.h"
#include "sanae_batch_import.h"
#include "sanae_api.h"
#include "sanae_project.h"
#include "subtitle_format.h"
#include "utils.h"

#include <libaegisub/fs.h>
#include <libaegisub/io.h>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/choicdlg.h>
#include <wx/colour.h>
#include <wx/dialog.h>
#include <wx/dirdlg.h>
#include <wx/filedlg.h>
#include <wx/listctrl.h>
#include <wx/msgdlg.h>
#include <wx/progdlg.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/textdlg.h>
#include <wx/utils.h>

namespace {
enum class BatchAction {
	Blocked,
	Skip,
	Create,
	CreateAndFinalize,
	FinalizeExisting,
	ReplaceSource,
	ReplaceAndFinalize
};

class SanaeBatchImportDialog final : public wxDialog {
	SanaeProjectManager& manager;
	std::string project_id;
	std::string project_name;
	SanaeBatchImportJob job;
	agi::fs::path state_path;

	wxTextCtrl *ensub_directory;
	wxTextCtrl *rusub_directory;
	wxListView *list;
	wxStaticText *summary;
	wxStaticText *details;
	wxCheckBox *skip_finalized;
	wxCheckBox *continue_after_error;
	wxCheckBox *sync_after_import;
	wxButton *import_button;
	wxButton *report_button;

	std::vector<BatchAction> actions;
	std::vector<std::string> server_states;
	std::vector<SanaeCompactStats> compact_stats;

	std::size_t SelectedRow() const {
		auto selected = list->GetFirstSelected();
		return selected < 0 ? job.rows.size() : static_cast<std::size_t>(selected);
	}

	SanaeEpisodeInfo const *FindEpisodeByCode(std::string const& code, bool *ambiguous = nullptr) const {
		auto key = SanaeBatchCanonicalEpisodeCode(code);
		SanaeEpisodeInfo const *result = nullptr;
		for (auto const& episode : manager.Episodes()) {
			if (episode.project_id == project_id && !episode.IsDeleted()
				&& SanaeBatchCanonicalEpisodeCode(episode.episode_code) == key) {
				if (result) {
					if (ambiguous) *ambiguous = true;
					return nullptr;
				}
				result = &episode;
			}
		}
		if (ambiguous) *ambiguous = false;
		return result;
	}

	static std::string LowerAscii(std::string value) {
		std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
			return static_cast<char>(std::tolower(c));
		});
		return value;
	}

	double NextSortOrder(std::size_t except_row) const {
		double next = 0.0;
		for (auto const& episode : manager.Episodes())
			if (episode.project_id == project_id && !episode.IsDeleted())
				next = std::max(next, episode.sort_order + 1.0);
		for (std::size_t index = 0; index < job.rows.size(); ++index)
			if (index != except_row && !job.rows[index].episode_code.empty())
				next = std::max(next, job.rows[index].sort_order + 1.0);
		return next;
	}

	static wxString ActionText(BatchAction action) {
		switch (action) {
			case BatchAction::Blocked: return _("Needs attention");
			case BatchAction::Skip: return _("Skip");
			case BatchAction::Create: return _("Create episode");
			case BatchAction::CreateAndFinalize: return _("Create + Finalize");
			case BatchAction::FinalizeExisting: return _("Finalize RUSUB");
			case BatchAction::ReplaceSource: return _("Replace ENSUB");
			case BatchAction::ReplaceAndFinalize: return _("Replace ENSUB + Finalize");
		}
		return wxString();
	}

	static wxString StateText(SanaeBatchRowState state) {
		switch (state) {
			case SanaeBatchRowState::Pending: return _("Pending");
			case SanaeBatchRowState::Running: return _("Running");
			case SanaeBatchRowState::Succeeded: return _("Done");
			case SanaeBatchRowState::Failed: return _("Failed");
			case SanaeBatchRowState::Skipped: return _("Skipped");
		}
		return wxString();
	}

	void SaveJob() {
		job.skip_finalized = skip_finalized->GetValue();
		job.continue_after_error = continue_after_error->GetValue();
		job.sync_after_import = sync_after_import->GetValue();
		SanaeBatchSaveJob(state_path, job);
	}

	void SaveOptions() {
		if (job.rows.empty()) return;
		try { SaveJob(); }
		catch (std::exception const& error) {
			wxMessageBox(to_wx(error.what()), _("Batch Import"), wxOK | wxICON_WARNING, this);
		}
	}

	void PlanRows() {
		actions.assign(job.rows.size(), BatchAction::Blocked);
		server_states.assign(job.rows.size(), std::string());
		compact_stats.assign(job.rows.size(), SanaeCompactStats{});

		std::unordered_map<std::string, int> code_counts;
		for (auto const& row : job.rows)
			if (row.included && !row.episode_code.empty())
				++code_counts[SanaeBatchCanonicalEpisodeCode(row.episode_code)];

		for (std::size_t i = 0; i < job.rows.size(); ++i) {
			auto& row = job.rows[i];
			auto previous_failure = row.state == SanaeBatchRowState::Failed ? row.status : std::string();
			auto ready_status = [&](wxString const& value) {
				row.status = previous_failure.empty() ? from_wx(value) : previous_failure;
			};
			if (row.state == SanaeBatchRowState::Succeeded) {
				actions[i] = BatchAction::Skip;
				server_states[i] = "imported";
				row.status = from_wx(_("Imported"));
				continue;
			}
			if (!row.included) {
				actions[i] = BatchAction::Skip;
				server_states[i] = "excluded";
				row.state = SanaeBatchRowState::Skipped;
				row.status = from_wx(_("Excluded"));
				continue;
			}
			if (row.state == SanaeBatchRowState::Skipped) row.state = SanaeBatchRowState::Pending;
			if (row.episode_code.empty()) {
				row.status = from_wx(_("Episode code could not be detected"));
				continue;
			}
			auto code_key = SanaeBatchCanonicalEpisodeCode(row.episode_code);
			if (code_key.empty()) {
				row.status = from_wx(_("Episode code is invalid"));
				continue;
			}
			if (to_wx(row.episode_code).length() > 64) {
				row.status = from_wx(_("Episode code exceeds the server limit of 64 characters"));
				continue;
			}
			if (code_counts[code_key] > 1) {
				row.status = from_wx(_("Several rows use the same episode code"));
				continue;
			}
			if (!row.ensub_path.empty() && row.ensub_path == row.rusub_path) {
				row.status = from_wx(_("ENSUB and RUSUB refer to the same file"));
				continue;
			}
			if (row.duplicate_ensub || row.duplicate_rusub) {
				row.status = from_wx(row.duplicate_ensub
					? _("Several ENSUB files matched this episode")
					: _("Several RUSUB files matched this episode"));
				continue;
			}

			bool ambiguous_server_episode = false;
			auto existing = FindEpisodeByCode(row.episode_code, &ambiguous_server_episode);
			if (ambiguous_server_episode) {
				row.status = from_wx(_("Several server episodes match this episode code"));
				continue;
			}
			if (!existing) {
				server_states[i] = "new";
				row.existing_episode_id.clear();
				if (row.ensub_path.empty()) {
					row.status = from_wx(_("A new episode requires an ENSUB"));
					continue;
				}
				actions[i] = row.rusub_path.empty() ? BatchAction::Create : BatchAction::CreateAndFinalize;
				ready_status(row.rusub_path.empty() ? _("Ready to create") : _("Ready to create and finalize"));
				continue;
			}

			if (!row.existing_episode_id.empty() && row.existing_episode_id != existing->id) {
				row.existing_source_action = "ask";
				row.replace_idempotency_key.clear();
				row.finalize_idempotency_key.clear();
			}
			row.existing_episode_id = existing->id;
			server_states[i] = existing->current_finalized_revision_id.empty() ? "existing" : "finalized";
			if (existing->id == manager.ActiveEpisodeId()) {
				row.status = from_wx(_("This episode is attached to the current ASS"));
				continue;
			}
			if (existing->current_source_file_id.empty()) {
				row.status = from_wx(_("The server episode has no current ENSUB"));
				continue;
			}
			bool source_differs = false;
			if (!row.ensub_path.empty()) {
				auto source_file = manager.FindFile(existing->current_source_file_id);
				if (!source_file || source_file->sha256.empty()) {
					row.status = from_wx(_("The existing ENSUB identity cannot be verified from the local snapshot"));
					continue;
				}
				try {
					source_differs = agi::fs::Size(row.ensub_path) != source_file->size_bytes;
					auto digest = SanaeBatchSha256File(row.ensub_path);
					if (digest.empty()) {
						row.status = from_wx(_("ENSUB verification is unavailable on this platform"));
						continue;
					}
					source_differs = source_differs
						|| LowerAscii(digest) != LowerAscii(source_file->sha256);
				}
				catch (std::exception const& error) { row.status = error.what(); continue; }
			}
			if (source_differs) {
				if (row.existing_source_action == "skip") {
					actions[i] = BatchAction::Skip;
					row.state = SanaeBatchRowState::Skipped;
					row.status = from_wx(_("Skipped by user"));
					continue;
				}
				if (row.existing_source_action == "ask") {
					row.status = from_wx(_("Existing episode has a different ENSUB — choose how to continue"));
					continue;
				}
				if (row.existing_source_action == "replace") {
					actions[i] = row.rusub_path.empty()
						? BatchAction::ReplaceSource : BatchAction::ReplaceAndFinalize;
					ready_status(row.rusub_path.empty()
						? _("Ready to replace ENSUB") : _("Ready to replace ENSUB and finalize"));
					continue;
				}
				// "use" explicitly ignores the imported ENSUB and continues with
				// the server source below.
			}
			else if (!row.ensub_path.empty()) {
				// A previous replace may have succeeded while its response was lost.
				// Identical bytes mean there is nothing destructive left to retry.
				row.existing_source_action = "use";
				row.replace_idempotency_key.clear();
			}
			if (row.rusub_path.empty()) {
				actions[i] = BatchAction::Skip;
				row.state = SanaeBatchRowState::Skipped;
				row.status = from_wx(existing->current_finalized_revision_id.empty()
					? _("ENSUB already exists") : _("Episode is already finalized"));
				continue;
			}
			if (!existing->current_finalized_revision_id.empty() && skip_finalized->GetValue()) {
				actions[i] = BatchAction::Skip;
				row.state = SanaeBatchRowState::Skipped;
				row.status = from_wx(_("Already finalized; protected by import option"));
				continue;
			}
			actions[i] = BatchAction::FinalizeExisting;
			ready_status(existing->current_finalized_revision_id.empty()
				? _("Ready to finalize") : _("Ready to create a new finalized revision"));
		}
	}

	void PreflightRows() {
		for (std::size_t i = 0; i < job.rows.size(); ++i) {
			auto& row = job.rows[i];
			if (row.state == SanaeBatchRowState::Succeeded) continue;
			try {
				if (actions[i] == BatchAction::Create || actions[i] == BatchAction::CreateAndFinalize
					|| actions[i] == BatchAction::ReplaceSource || actions[i] == BatchAction::ReplaceAndFinalize)
					manager.ValidateBatchEnsub(row.ensub_path);
				if (actions[i] == BatchAction::CreateAndFinalize || actions[i] == BatchAction::FinalizeExisting
					|| actions[i] == BatchAction::ReplaceAndFinalize)
					compact_stats[i] = manager.PreviewBatchRusub(row.rusub_path);
			}
			catch (std::exception const& error) {
				actions[i] = BatchAction::Blocked;
				row.status = error.what();
			}
		}
	}

	void UpdateDetails() {
		auto index = SelectedRow();
		if (index >= job.rows.size()) {
			details->SetLabel(_("Select a row to inspect or correct its mapping."));
			return;
		}
		auto const& row = job.rows[index];
		std::ostringstream text;
		text << "ENSUB: " << (row.ensub_path.empty() ? "-" : row.ensub_path.string())
			<< "\nRUSUB: " << (row.rusub_path.empty() ? "-" : row.rusub_path.string());
		if (!row.rusub_path.empty() && actions[index] != BatchAction::Blocked) {
			auto const& stats = compact_stats[index];
			text << "\nCompact: " << stats.input_events << " -> " << stats.output_events
				<< ", drawings " << stats.drawings_removed
				<< ", comments " << stats.comments_removed
				<< ", collapsed " << stats.technical_duplicates_collapsed;
		}
		text << "\n" << row.status;
		details->SetLabel(to_wx(text.str()));
		details->Wrap(GetClientSize().GetWidth() - 30);
		Layout();
	}

	void UpdateTable() {
		list->DeleteAllItems();
		int create_count = 0, replace_count = 0, finalize_count = 0, blocked_count = 0, skipped_count = 0;
		for (std::size_t i = 0; i < job.rows.size(); ++i) {
			auto const& row = job.rows[i];
			auto item = list->InsertItem(static_cast<long>(i),
				row.episode_code.empty() ? wxString("?") : to_wx(row.episode_code));
			list->SetItem(item, 1,
				row.ensub_path.empty() ? wxString("-") : to_wx(row.ensub_path.filename().string()));
			list->SetItem(item, 2,
				row.rusub_path.empty() ? wxString("-") : to_wx(row.rusub_path.filename().string()));
			wxString server = server_states[i] == "new" ? _("New episode")
				: server_states[i] == "existing" ? _("Existing")
				: server_states[i] == "finalized" ? _("Finalized")
				: server_states[i] == "imported" ? _("Imported")
				: server_states[i] == "excluded" ? _("Excluded") : wxString("-");
			list->SetItem(item, 3, server);
			list->SetItem(item, 4, ActionText(actions[i]));
			list->SetItem(item, 5, StateText(row.state));
			if (row.state == SanaeBatchRowState::Failed) {
				list->SetItemTextColour(item, wxColour(190, 60, 60));
			}
			if (actions[i] == BatchAction::Blocked) {
				++blocked_count;
				if (row.state != SanaeBatchRowState::Failed)
					list->SetItemTextColour(item, wxColour(190, 60, 60));
			}
			else if (actions[i] == BatchAction::Skip) {
				++skipped_count;
				list->SetItemTextColour(item, wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
			}
			else {
				if (actions[i] == BatchAction::Create || actions[i] == BatchAction::CreateAndFinalize) ++create_count;
				if (actions[i] == BatchAction::ReplaceSource || actions[i] == BatchAction::ReplaceAndFinalize) ++replace_count;
				if (actions[i] == BatchAction::CreateAndFinalize || actions[i] == BatchAction::FinalizeExisting
					|| actions[i] == BatchAction::ReplaceAndFinalize) ++finalize_count;
			}
		}
		summary->SetLabel(agi::wxformat(
			_("Episodes to create: %d    ENSUB to replace: %d    RUSUB to finalize: %d    Skipped: %d    Needs attention: %d"),
			create_count, replace_count, finalize_count, skipped_count, blocked_count));
		import_button->Enable(create_count + replace_count + finalize_count > 0);
		UpdateDetails();
	}

	void Replan(bool save = true) {
		PlanRows();
		PreflightRows();
		UpdateTable();
		if (save) {
			try { SaveJob(); }
			catch (std::exception const& error) {
				wxMessageBox(to_wx(error.what()), _("Batch Import"), wxOK | wxICON_WARNING, this);
			}
		}
	}

	void BrowseDirectory(wxTextCtrl *target, wxString const& title) {
		wxDirDialog dialog(this, title, target->GetValue(), wxDD_DEFAULT_STYLE | wxDD_DIR_MUST_EXIST);
		if (dialog.ShowModal() == wxID_OK) target->SetValue(dialog.GetPath());
	}

	void Scan() {
		try {
			auto ensub = agi::fs::path(from_wx(ensub_directory->GetValue().Trim()));
			auto rusub = agi::fs::path(from_wx(rusub_directory->GetValue().Trim()));
			if (!job.rows.empty() && wxMessageBox(
				_("Replace the current batch-import queue with a new folder scan?"),
				_("Batch Import"), wxYES_NO | wxICON_QUESTION, this) != wxYES) return;
			SanaeBatchImportJob scanned;
			scanned.project_id = project_id;
			scanned.ensub_directory = ensub;
			scanned.rusub_directory = rusub;
			scanned.skip_finalized = skip_finalized->GetValue();
			scanned.continue_after_error = continue_after_error->GetValue();
			scanned.sync_after_import = sync_after_import->GetValue();
			scanned.rows = SanaeBatchScanFolders(ensub, rusub);
			job = std::move(scanned);
			Replan();
			if (job.rows.empty())
				wxMessageBox(_("No ASS or SSA subtitle files were found in the selected folders."),
					_("Batch Import"), wxOK | wxICON_INFORMATION, this);
		}
		catch (std::exception const& error) {
			wxMessageBox(to_wx(error.what()), _("Batch Import"), wxOK | wxICON_ERROR, this);
		}
	}

	void EditEpisodeCode() {
		auto index = SelectedRow();
		if (index >= job.rows.size() || job.rows[index].state == SanaeBatchRowState::Succeeded) return;
		wxTextEntryDialog dialog(this, _("Episode code:"), _("Correct Batch Mapping"),
			to_wx(job.rows[index].episode_code));
		if (dialog.ShowModal() != wxID_OK || dialog.GetValue().Trim().empty()) return;
		auto& row = job.rows[index];
		row.episode_code = from_wx(dialog.GetValue().Trim());
		row.sort_order = SanaeBatchEpisodeSortOrder(row.episode_code, NextSortOrder(index));
		row.existing_episode_id.clear();
		row.existing_source_action = "ask";
		row.create_idempotency_key.clear();
		row.replace_idempotency_key.clear();
		row.finalize_idempotency_key.clear();
		row.state = SanaeBatchRowState::Pending;
		Replan();
	}

	void ChooseFile(bool ensub) {
		auto index = SelectedRow();
		if (index >= job.rows.size() || job.rows[index].state == SanaeBatchRowState::Succeeded) return;
		auto path = OpenFileSelector(ensub ? _("Choose source ENSUB") : _("Choose translated RUSUB"),
			"Path/Last/Subtitles", "", "", SubtitleFormat::GetWildcards(0), this);
		if (path.empty()) return;
		auto& row = job.rows[index];
		if (ensub) {
			row.ensub_path = path;
			row.duplicate_ensub = false;
			row.existing_source_action = "ask";
			row.replace_idempotency_key.clear();
		}
		else { row.rusub_path = path; row.duplicate_rusub = false; }
		row.create_idempotency_key.clear();
		row.finalize_idempotency_key.clear();
		row.state = SanaeBatchRowState::Pending;
		Replan();
	}

	void ResolveExistingEpisode() {
		auto index = SelectedRow();
		if (index >= job.rows.size() || job.rows[index].state == SanaeBatchRowState::Succeeded) return;
		auto& row = job.rows[index];
		bool ambiguous = false;
		auto episode = FindEpisodeByCode(row.episode_code, &ambiguous);
		if (!episode || ambiguous) {
			wxMessageBox(_("This row does not map to one active server episode."),
				_("Existing episode"), wxOK | wxICON_INFORMATION, this);
			return;
		}
		if (row.ensub_path.empty()) {
			wxMessageBox(_("Choose an imported ENSUB before resolving the existing episode."),
				_("Existing episode"), wxOK | wxICON_INFORMATION, this);
			return;
		}
		try {
			auto source = manager.FindFile(episode->current_source_file_id);
			if (!source) {
				auto existing_id = episode->id;
				(void)manager.GetEpisodeDetails(existing_id);
				episode = manager.FindEpisode(existing_id);
				source = episode ? manager.FindFile(episode->current_source_file_id) : nullptr;
			}
			if (!episode || !source) throw std::runtime_error("Current ENSUB metadata is unavailable");
			auto diff = manager.CompareEpisodeSource(episode->id, row.ensub_path);
			wxString message = agi::wxformat(
				_("Episode %s already exists on the server.\n\n"
				  "Current ENSUB: version %d, %d bytes\nSHA-256: %s\n\n"
				  "Imported ENSUB: %s, %d bytes\n\n"
				  "Unchanged lines: %d\nChanged: %d\nAdded: %d\nRemoved: %d"),
				to_wx(episode->episode_code), source->revision_number,
				static_cast<int>(source->size_bytes), to_wx(source->sha256),
				to_wx(row.ensub_path.filename().string()), static_cast<int>(agi::fs::Size(row.ensub_path)),
				static_cast<int>(diff.unchanged), static_cast<int>(diff.changed),
				static_cast<int>(diff.added), static_cast<int>(diff.removed));
			if (!episode->current_finalized_revision_id.empty())
				message += _("\n\nThe episode is finalized. Replacing ENSUB returns it to ‘Translating’; the old final version remains in history.");
			message += _("\n\nChoose what Batch Import should do:");
			wxArrayString choices{
				_("Skip this episode"),
				_("Use the existing server ENSUB"),
				_("Replace the server ENSUB with the imported file")};
			wxSingleChoiceDialog dialog(this, message, _("Existing episode"), choices);
			dialog.SetSelection(row.existing_source_action == "use" ? 1
				: row.existing_source_action == "replace" ? 2 : 0);
			if (dialog.ShowModal() != wxID_OK) return;
			auto selected = dialog.GetSelection();
			row.existing_source_action = selected == 1 ? "use" : selected == 2 ? "replace" : "skip";
			row.replace_idempotency_key.clear();
			row.finalize_idempotency_key.clear();
			row.state = SanaeBatchRowState::Pending;
			row.status.clear();
			Replan();
		}
		catch (std::exception const& error) {
			wxMessageBox(agi::wxformat(_("The existing episode could not be compared.\n\nDetails: %s"),
				to_wx(error.what())), _("Existing episode"), wxOK | wxICON_ERROR, this);
		}
	}

	void ToggleIncluded() {
		auto index = SelectedRow();
		if (index >= job.rows.size() || job.rows[index].state == SanaeBatchRowState::Succeeded) return;
		job.rows[index].included = !job.rows[index].included;
		job.rows[index].state = SanaeBatchRowState::Pending;
		Replan();
	}

	void RunImport() {
		int create_count = 0, replace_count = 0, finalize_count = 0, actionable = 0;
		for (auto action : actions) {
			if (action == BatchAction::Create || action == BatchAction::CreateAndFinalize) ++create_count;
			if (action == BatchAction::ReplaceSource || action == BatchAction::ReplaceAndFinalize) ++replace_count;
			if (action == BatchAction::CreateAndFinalize || action == BatchAction::FinalizeExisting
				|| action == BatchAction::ReplaceAndFinalize) ++finalize_count;
			if (action != BatchAction::Blocked && action != BatchAction::Skip) ++actionable;
		}
		if (!actionable) return;
		if (wxMessageBox(agi::wxformat(
			_("Create %d episode(s), replace %d ENSUB file(s), and finalize %d RUSUB file(s)?\n\nThe current ASS will not be changed."),
			create_count, replace_count, finalize_count), _("Start Batch Import"),
			wxYES_NO | wxICON_QUESTION, this) != wxYES) return;

		import_button->Enable(false);
		report_button->Enable(false);
		for (std::size_t i = 0; i < job.rows.size(); ++i)
			if (actions[i] == BatchAction::Skip && job.rows[i].state != SanaeBatchRowState::Succeeded)
				job.rows[i].state = SanaeBatchRowState::Skipped;
		try { SaveJob(); } catch (...) { }
		int completed = 0;
		bool cancelled = false;
		{
			wxProgressDialog progress(_("Sanae Batch Import"), _("Preparing import…"),
				actionable, this, wxPD_APP_MODAL | wxPD_CAN_ABORT | wxPD_ELAPSED_TIME | wxPD_REMAINING_TIME);
			for (std::size_t i = 0; i < job.rows.size(); ++i) {
				auto action = actions[i];
				if (action == BatchAction::Blocked || action == BatchAction::Skip) continue;
				auto& row = job.rows[i];
				if (!progress.Update(completed, agi::wxformat(_("Episode %s…"), to_wx(row.episode_code)))) {
					cancelled = true;
					break;
				}
				row.state = SanaeBatchRowState::Running;
				row.status = from_wx(_("Importing"));
				try {
					if (action == BatchAction::Create || action == BatchAction::CreateAndFinalize) {
						manager.ValidateBatchEnsub(row.ensub_path);
						if (row.create_idempotency_key.empty()) row.create_idempotency_key = SanaeBatchNewIdempotencyKey();
						SaveJob();
						auto episode = manager.CreateEpisodeDetached(project_id, row.episode_code,
							row.sort_order, row.ensub_path, row.create_idempotency_key);
						row.existing_episode_id = episode.id;
						row.status = from_wx(_("Episode created"));
						SaveJob();
					}
					if (action == BatchAction::ReplaceSource || action == BatchAction::ReplaceAndFinalize) {
						manager.ValidateBatchEnsub(row.ensub_path);
						if (row.replace_idempotency_key.empty())
							row.replace_idempotency_key = SanaeBatchNewIdempotencyKey();
						SaveJob();
						manager.ReplaceEpisodeSourceWithKey(row.existing_episode_id,
							row.ensub_path, row.replace_idempotency_key);
						row.status = from_wx(_("ENSUB replaced"));
						SaveJob();
					}
					if (action == BatchAction::CreateAndFinalize || action == BatchAction::FinalizeExisting
						|| action == BatchAction::ReplaceAndFinalize) {
						(void)manager.PreviewBatchRusub(row.rusub_path);
						if (row.finalize_idempotency_key.empty()) row.finalize_idempotency_key = SanaeBatchNewIdempotencyKey();
						SaveJob();
						manager.FinalizeEpisodeFromFile(row.existing_episode_id, row.rusub_path,
							row.finalize_idempotency_key);
						row.status = from_wx(_("RUSUB finalized"));
					}
					row.state = SanaeBatchRowState::Succeeded;
					SaveJob();
				}
				catch (SanaeApiError const& error) {
					row.state = SanaeBatchRowState::Failed;
					row.status = error.Code() == "source_changed"
						? from_wx(_("The server ENSUB changed after synchronization. Synchronize and resolve this row again."))
						: error.what();
					try { SaveJob(); } catch (...) { }
					if (!continue_after_error->GetValue()) { ++completed; break; }
				}
				catch (std::exception const& error) {
					row.state = SanaeBatchRowState::Failed;
					row.status = error.what();
					try { SaveJob(); } catch (...) { }
					if (!continue_after_error->GetValue()) { ++completed; break; }
				}
				++completed;
				UpdateTable();
				wxYieldIfNeeded();
			}
			progress.Update(completed);
		}
		try {
			manager.FinishBatchImport();
			if (sync_after_import->GetValue()) manager.SyncProject(project_id);
		}
		catch (std::exception const& error) {
			wxMessageBox(agi::wxformat(_("Import finished, but project synchronization failed:\n\n%s"),
				to_wx(error.what())), _("Batch Import"), wxOK | wxICON_WARNING, this);
		}
		Replan();
		report_button->Enable(true);
		if (cancelled)
			wxMessageBox(_("Batch import was paused. Completed rows are preserved; run it again to continue."),
				_("Batch Import"), wxOK | wxICON_INFORMATION, this);
		else {
			int failures = static_cast<int>(std::count_if(job.rows.begin(), job.rows.end(), [](auto const& row) {
				return row.state == SanaeBatchRowState::Failed;
			}));
			wxMessageBox(failures
				? agi::wxformat(_("Batch import completed with %d error(s). Correct the rows and retry."), failures)
				: _("Batch import completed successfully."),
				_("Batch Import"), wxOK | (failures ? wxICON_WARNING : wxICON_INFORMATION), this);
		}
	}

	void SaveReport() {
		wxFileDialog dialog(this, _("Save Batch Import Report"), wxEmptyString,
			"sanae-batch-import.txt", _("Text files (*.txt)|*.txt"),
			wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
		if (dialog.ShowModal() != wxID_OK) return;
		try {
			agi::io::Save output(agi::fs::path(from_wx(dialog.GetPath())), true);
			auto& stream = output.Get();
			stream << "Sanae batch import\nProject: " << project_name << "\n\n";
			for (auto const& row : job.rows) {
				stream << (row.episode_code.empty() ? "?" : row.episode_code)
					<< "\t" << row.ensub_path.string() << "\t" << row.rusub_path.string()
					<< "\t" << from_wx(StateText(row.state)) << "\t" << row.status << "\n";
			}
		}
		catch (std::exception const& error) {
			wxMessageBox(to_wx(error.what()), _("Batch Import"), wxOK | wxICON_ERROR, this);
		}
	}

	void RestoreJob() {
		SanaeBatchImportJob saved;
		if (!SanaeBatchLoadJob(state_path, saved) || saved.project_id != project_id || saved.rows.empty()) return;
		bool unfinished = std::any_of(saved.rows.begin(), saved.rows.end(), [](auto const& row) {
			return row.state == SanaeBatchRowState::Failed || row.state == SanaeBatchRowState::Running
				|| row.state == SanaeBatchRowState::Pending;
		});
		if (!unfinished || wxMessageBox(_("An unfinished batch-import queue was found. Restore it?"),
			_("Resume Batch Import"), wxYES_NO | wxICON_QUESTION, this) != wxYES) return;
		job = std::move(saved);
		for (auto& row : job.rows)
			if (row.status == "Interrupted; safe to retry")
				row.status = from_wx(_("Interrupted; safe to retry"));
		ensub_directory->SetValue(to_wx(job.ensub_directory.string()));
		rusub_directory->SetValue(to_wx(job.rusub_directory.string()));
		skip_finalized->SetValue(job.skip_finalized);
		continue_after_error->SetValue(job.continue_after_error);
		sync_after_import->SetValue(job.sync_after_import);
		Replan(false);
		report_button->Enable(true);
	}

public:
	SanaeBatchImportDialog(agi::Context *c, std::string project_id, std::string project_name)
	: wxDialog(c->parent, -1, _("Sanae Batch Import"), wxDefaultPosition, wxSize(980, 690),
		wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
	, manager(*c->sanaeProject)
	, project_id(std::move(project_id))
	, project_name(std::move(project_name))
	, state_path(manager.BatchImportStatePath(this->project_id))
	{
		job.project_id = this->project_id;
		auto main = new wxBoxSizer(wxVERTICAL);
		main->Add(new wxStaticText(this, -1,
			agi::wxformat(_("Project: %s"), to_wx(this->project_name))), 0, wxBOTTOM, 10);

		auto folders = new wxFlexGridSizer(3, 3, 6, 8);
		folders->AddGrowableCol(1, 1);
		folders->Add(new wxStaticText(this, -1, _("ENSUB folder:")), 0, wxALIGN_CENTER_VERTICAL);
		folders->Add(ensub_directory = new wxTextCtrl(this, -1), 1, wxEXPAND);
		auto browse_ensub = new wxButton(this, -1, _("Browse…"));
		folders->Add(browse_ensub);
		folders->Add(new wxStaticText(this, -1, _("RUSUB folder:")), 0, wxALIGN_CENTER_VERTICAL);
		folders->Add(rusub_directory = new wxTextCtrl(this, -1), 1, wxEXPAND);
		auto browse_rusub = new wxButton(this, -1, _("Browse…"));
		folders->Add(browse_rusub);
		folders->AddSpacer(1);
		folders->Add(new wxStaticText(this, -1,
			_("Choose either folder or both. Files are matched by episode number.")), 0, wxALIGN_CENTER_VERTICAL);
		auto scan = new wxButton(this, -1, _("Scan folders"));
		folders->Add(scan);
		main->Add(folders, 0, wxEXPAND | wxBOTTOM, 10);

		list = new wxListView(this, -1, wxDefaultPosition, wxDefaultSize,
			wxLC_REPORT | wxLC_SINGLE_SEL | wxLC_HRULES | wxLC_VRULES);
		list->InsertColumn(0, _("Episode"), wxLIST_FORMAT_LEFT, 75);
		list->InsertColumn(1, _("ENSUB"), wxLIST_FORMAT_LEFT, 190);
		list->InsertColumn(2, _("RUSUB"), wxLIST_FORMAT_LEFT, 190);
		list->InsertColumn(3, _("Server"), wxLIST_FORMAT_LEFT, 100);
		list->InsertColumn(4, _("Action"), wxLIST_FORMAT_LEFT, 150);
		list->InsertColumn(5, _("Result"), wxLIST_FORMAT_LEFT, 100);
		main->Add(list, 1, wxEXPAND | wxBOTTOM, 6);
		main->Add(summary = new wxStaticText(this, -1, _("Scan folders to build an import plan.")),
			0, wxEXPAND | wxBOTTOM, 6);
		main->Add(details = new wxStaticText(this, -1, _("Select a row to inspect or correct its mapping.")),
			0, wxEXPAND | wxBOTTOM, 8);

		auto edit_row = new wxBoxSizer(wxHORIZONTAL);
		auto edit_code = new wxButton(this, -1, _("Edit episode…"));
		auto choose_ensub = new wxButton(this, -1, _("Choose ENSUB…"));
		auto choose_rusub = new wxButton(this, -1, _("Choose RUSUB…"));
		auto resolve_existing = new wxButton(this, -1, _("Resolve existing…"));
		auto toggle = new wxButton(this, -1, _("Include / skip"));
		edit_row->Add(edit_code, 0, wxRIGHT, 6);
		edit_row->Add(choose_ensub, 0, wxRIGHT, 6);
		edit_row->Add(choose_rusub, 0, wxRIGHT, 6);
		edit_row->Add(resolve_existing, 0, wxRIGHT, 6);
		edit_row->Add(toggle);
		main->Add(edit_row, 0, wxEXPAND | wxBOTTOM, 8);

		auto options = new wxBoxSizer(wxHORIZONTAL);
		options->Add(skip_finalized = new wxCheckBox(this, -1, _("Protect finalized episodes")), 0, wxRIGHT, 14);
		options->Add(continue_after_error = new wxCheckBox(this, -1, _("Continue after an episode error")), 0, wxRIGHT, 14);
		options->Add(sync_after_import = new wxCheckBox(this, -1, _("Sync project when finished")));
		skip_finalized->SetValue(true);
		continue_after_error->SetValue(true);
		sync_after_import->SetValue(true);
		main->Add(options, 0, wxEXPAND | wxBOTTOM, 10);

		auto buttons = new wxBoxSizer(wxHORIZONTAL);
		buttons->Add(import_button = new wxButton(this, -1, _("Start / retry import")), 0, wxRIGHT, 6);
		buttons->Add(report_button = new wxButton(this, -1, _("Save report…")), 0, wxRIGHT, 6);
		buttons->AddStretchSpacer();
		buttons->Add(new wxButton(this, wxID_CLOSE));
		main->Add(buttons, 0, wxEXPAND);
		import_button->Enable(false);
		report_button->Enable(false);

		main->SetMinSize(wxSize(760, 520));
		SetSizer(main);
		SetMinSize(wxSize(780, 560));
		CentreOnParent();

		browse_ensub->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { BrowseDirectory(ensub_directory, _("Choose ENSUB folder")); });
		browse_rusub->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { BrowseDirectory(rusub_directory, _("Choose RUSUB folder")); });
		scan->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Scan(); });
		edit_code->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { EditEpisodeCode(); });
		choose_ensub->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { ChooseFile(true); });
		choose_rusub->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { ChooseFile(false); });
		resolve_existing->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { ResolveExistingEpisode(); });
		toggle->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { ToggleIncluded(); });
		import_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { RunImport(); });
		report_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { SaveReport(); });
		list->Bind(wxEVT_LIST_ITEM_SELECTED, [this](wxListEvent&) { UpdateDetails(); });
		skip_finalized->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) { Replan(); });
		continue_after_error->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) { SaveOptions(); });
		sync_after_import->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) { SaveOptions(); });
		Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { EndModal(wxID_CLOSE); }, wxID_CLOSE);

		RestoreJob();
	}
};
}

void ShowSanaeBatchImportDialog(agi::Context *context,
	std::string const& project_id, std::string const& project_name)
{
	SanaeBatchImportDialog(context, project_id, project_name).ShowModal();
}
