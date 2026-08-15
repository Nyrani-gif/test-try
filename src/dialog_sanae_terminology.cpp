// Copyright (c) 2026, Aegisub Sanae contributors

#include "dialog_sanae_terminology.h"

#include "compat.h"
#include "dialog_sanae_final_review.h"
#include "format.h"
#include "include/aegisub/context.h"
#include "sanae_project.h"
#include "sanae_text.h"

#include <libaegisub/ass/time.h>

#include <algorithm>
#include <limits>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <wx/button.h>
#include <wx/choicdlg.h>
#include <wx/dialog.h>
#include <wx/listctrl.h>
#include <wx/msgdlg.h>
#include <wx/notebook.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

namespace {
constexpr size_t no_index = std::numeric_limits<size_t>::max();

bool contains_filter(std::string const& value, std::string const& normalized_filter) {
	return normalized_filter.empty()
		|| SanaeNormalizeSource(value).find(normalized_filter) != std::string::npos;
}

wxString operation_label(std::string const& operation) {
	if (operation == "update") return _("Queued update");
	if (operation == "delete") return _("Queued deletion");
	if (operation == "restore") return _("Queued restore");
	return _("Queued creation");
}

wxString history_operation_label(std::string const& operation) {
	if (operation == "update") return _("Updated");
	if (operation == "delete") return _("Deleted");
	if (operation == "restore") return _("Restored");
	return _("Created");
}

wxString terminology_candidate_reason(SanaeTerminologyCandidate const& candidate) {
	if (candidate.reason == "unknown_word") return _("not in English dictionary");
	if (candidate.reason == "name_phrase") return _("repeated name-like phrase");
	if (candidate.reason == "unknown_phrase") return _("phrase contains an unknown word");
	if (candidate.reason == "repeated_phrase") return _("stable repeated phrase");
	return _("worth checking");
}

class TermEditDialog final : public wxDialog {
	wxTextCtrl *english;
	wxTextCtrl *russian;
	wxTextCtrl *note;

public:
	TermEditDialog(wxWindow *parent, wxString const& title, std::string english_value,
		std::string russian_value, std::string note_value,
		SanaeTerminologyEntry const *server_value = nullptr, int local_base_version = 0)
	: wxDialog(parent, -1, title, wxDefaultPosition, wxDefaultSize,
		wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
	{
		auto main = new wxBoxSizer(wxVERTICAL);
		if (server_value && local_base_version != server_value->version) {
			auto warning = new wxStaticText(this, -1, agi::wxformat(
				_("This term changed on the server after the local edit was queued.\n"
				  "Current server value: %s → %s%s\n"
				  "Saving below will apply your edit to the current server value."),
				to_wx(server_value->english), to_wx(server_value->russian),
				server_value->note.empty() ? wxString() : agi::wxformat(" (%s)", to_wx(server_value->note))));
			warning->Wrap(650);
			main->Add(warning, 0, wxEXPAND | wxBOTTOM, 12);
		}
		auto fields = new wxFlexGridSizer(2, 7, 9);
		fields->AddGrowableCol(1, 1);
		fields->Add(new wxStaticText(this, -1, _("English:")), 0, wxALIGN_CENTER_VERTICAL);
		fields->Add(english = new wxTextCtrl(this, -1, to_wx(english_value)), 1, wxEXPAND);
		fields->Add(new wxStaticText(this, -1, _("Russian:")), 0, wxALIGN_CENTER_VERTICAL);
		fields->Add(russian = new wxTextCtrl(this, -1, to_wx(russian_value)), 1, wxEXPAND);
		fields->Add(new wxStaticText(this, -1, _("Note (optional):")), 0, wxALIGN_CENTER_VERTICAL);
		fields->Add(note = new wxTextCtrl(this, -1, to_wx(note_value)), 1, wxEXPAND);
		main->Add(fields, 1, wxEXPAND | wxBOTTOM, 12);
		main->Add(CreateStdDialogButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND);
		SetSizerAndFit(main);
		SetSize(wxSize(700, GetSize().GetHeight()));
		CentreOnParent();
	}

	SanaeTerminologyDraft Value() const {
		return {from_wx(english->GetValue().Trim()), from_wx(russian->GetValue().Trim()),
			from_wx(note->GetValue().Trim())};
	}
};

class TerminologyCandidateDialog final : public wxDialog {
	agi::Context *context;
	SanaeProjectManager& manager;
	std::vector<SanaeTerminologyCandidate> candidates;
	wxListCtrl *list;
	wxStaticText *empty;
	wxTextCtrl *details;

	std::vector<long> SelectedRows() const {
		std::vector<long> rows;
		for (long row = list->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
			row != -1; row = list->GetNextItem(row, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED))
			rows.push_back(row);
		return rows;
	}
	void ShowSelected() {
		auto rows = SelectedRows();
		if (rows.empty()) {
			details->ChangeValue(_("Select a candidate to see aligned project examples."));
			return;
		}
		if (rows.size() > 1) {
			details->ChangeValue(agi::wxformat(_("Selected candidates: %d\n\nActions apply to the whole selection."),
				static_cast<int>(rows.size())));
			return;
		}
		auto selected = static_cast<std::size_t>(rows.front());
		if (selected >= candidates.size()) return;
		auto const& candidate = candidates[selected];
		wxString text = agi::wxformat(
			_("Episodes: %d\nIn the current episode: %d\nIn previous episodes: %d\nReason: %s"),
			candidate.project_episode_count, candidate.occurrences, candidate.previous_occurrences,
			terminology_candidate_reason(candidate));
		for (auto const& value : candidate.contexts) {
			text += agi::wxformat("\n\n%s · %s\nEN: %s\nRU: %s",
				value.current_episode ? _("Current episode") : to_wx(value.episode_code),
				to_wx(agi::Time(value.start).GetAssFormatted(true)), to_wx(value.source),
				to_wx(value.russian.empty() ? "—" : value.russian));
		}
		details->ChangeValue(text);
	}
	void Populate() {
		candidates = manager.GenerateCandidates();
		list->DeleteAllItems();
		for (auto const& candidate : candidates) {
			auto row = list->InsertItem(list->GetItemCount(), to_wx(candidate.english));
			list->SetItem(row, 1, agi::wxformat("%d", candidate.project_episode_count));
			list->SetItem(row, 2, agi::wxformat("%d", candidate.occurrences));
			list->SetItem(row, 3, agi::wxformat("%d", candidate.previous_occurrences));
			list->SetItem(row, 4, terminology_candidate_reason(candidate));
		}
		list->Show(!candidates.empty());
		empty->Show(candidates.empty());
		Layout();
		ShowSelected();
	}
	void Add() {
		auto rows = SelectedRows();
		for (auto row : rows) {
			if (row < 0 || static_cast<std::size_t>(row) >= candidates.size()) continue;
			if (!ShowSanaeTerminologyEntryDialog(context, this, candidates[static_cast<std::size_t>(row)].english, {})) break;
		}
		Populate();
	}
	void Ignore() {
		auto rows = SelectedRows();
		if (rows.empty()) return;
		wxArrayString choices{_("Whole project"), _("This episode only")};
		wxSingleChoiceDialog scope(this, _("Where should the selected candidates be ignored?"),
			_("Ignore terminology candidates"), choices);
		if (scope.ShowModal() != wxID_OK) return;
		try {
			for (auto row : rows)
				if (row >= 0 && static_cast<std::size_t>(row) < candidates.size())
					manager.QueueIgnore({scope.GetSelection() == 0 ? "project" : "episode",
						candidates[static_cast<std::size_t>(row)].english, "en"});
			Populate();
		}
		catch (std::exception const& error) {
			wxMessageBox(to_wx(error.what()), _("Possible new terms"), wxOK | wxICON_ERROR, this);
		}
	}

public:
	TerminologyCandidateDialog(agi::Context *c, wxWindow *parent)
	: wxDialog(parent, -1, _("Possible new terms"), wxDefaultPosition, wxSize(850, 600),
		wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
	, context(c), manager(*c->sanaeProject)
	{
		auto main = new wxBoxSizer(wxVERTICAL);
		main->Add(new wxStaticText(this, -1,
			_("Candidates come from the existing project ENSUB analysis. Nothing is added or ignored without your action.")),
			0, wxEXPAND | wxBOTTOM, 7);
		list = new wxListCtrl(this, -1, wxDefaultPosition, wxDefaultSize,
			wxLC_REPORT | wxLC_HRULES | wxLC_VRULES);
		list->InsertColumn(0, _("Candidate"), wxLIST_FORMAT_LEFT, 260);
		list->InsertColumn(1, _("Episodes"), wxLIST_FORMAT_RIGHT, 75);
		list->InsertColumn(2, _("This episode"), wxLIST_FORMAT_RIGHT, 90);
		list->InsertColumn(3, _("Earlier"), wxLIST_FORMAT_RIGHT, 75);
		list->InsertColumn(4, _("Reason"), wxLIST_FORMAT_LEFT, 250);
		main->Add(list, 1, wxEXPAND | wxBOTTOM, 7);
		empty = new wxStaticText(this, -1, _("✓ No useful terminology candidates were found."),
			wxDefaultPosition, wxDefaultSize, wxALIGN_CENTER_HORIZONTAL);
		empty->Hide();
		main->Add(empty, 1, wxEXPAND | wxALL, 24);
		details = new wxTextCtrl(this, -1, wxString(), wxDefaultPosition, wxSize(-1, 190),
			wxTE_MULTILINE | wxTE_READONLY);
		main->Add(details, 0, wxEXPAND | wxBOTTOM, 7);
		auto buttons = new wxBoxSizer(wxHORIZONTAL);
		auto add = new wxButton(this, -1, _("Add selected to terminology"));
		auto ignore = new wxButton(this, -1, _("Ignore selected"));
		buttons->Add(add, 0, wxRIGHT, 6);
		buttons->Add(ignore);
		buttons->AddStretchSpacer();
		buttons->Add(new wxButton(this, wxID_CLOSE));
		main->Add(buttons, 0, wxEXPAND);
		SetSizer(main);
		CentreOnParent();
		list->Bind(wxEVT_LIST_ITEM_SELECTED, [this](wxListEvent&) { ShowSelected(); });
		list->Bind(wxEVT_LIST_ITEM_DESELECTED, [this](wxListEvent&) { ShowSelected(); });
		list->Bind(wxEVT_KEY_DOWN, [this](wxKeyEvent& event) {
			if (event.ControlDown() && (event.GetKeyCode() == 'A' || event.GetKeyCode() == 'a')) {
				for (long i = 0; i < list->GetItemCount(); ++i)
					list->SetItemState(i, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
				return;
			}
			event.Skip();
		});
		add->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Add(); });
		ignore->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Ignore(); });
		Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { EndModal(wxID_CLOSE); }, wxID_CLOSE);
		Populate();
	}
};

class TerminologyDialog final : public wxDialog {
	struct Row {
		size_t term_index = no_index;
		size_t draft_index = no_index;
	};

	agi::Context *context;
	SanaeProjectManager& manager;
	wxTextCtrl *filter;
	wxListCtrl *terms;
	wxStaticText *terms_empty;
	wxListCtrl *history;
	wxStaticText *history_empty;
	wxButton *edit;
	wxButton *delete_restore;
	wxButton *discard;
	wxStaticText *help;
	std::vector<Row> rows;

	SanaeTerminologyDraft const *DraftFor(std::string const& term_id, size_t *index = nullptr) const {
		auto const& drafts = manager.TerminologyDrafts();
		for (size_t i = 0; i < drafts.size(); ++i) {
			if (drafts[i].term_id == term_id && !term_id.empty()) {
				if (index) *index = i;
				return &drafts[i];
			}
		}
		return nullptr;
	}

	Row const *Selected() const {
		long selected = terms->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
		return selected >= 0 && static_cast<size_t>(selected) < rows.size() ? &rows[selected] : nullptr;
	}

	void SetCell(long row, int column, wxString const& value) {
		terms->SetItem(row, column, value.empty() ? to_wx("—") : value);
	}

	void PopulateHistory(std::string const& normalized_filter) {
		history->DeleteAllItems();
		std::vector<SanaeTerminologyHistoryEntry const *> ordered;
		for (auto const& value : manager.TerminologyHistory()) {
			if (!contains_filter(value.english, normalized_filter)
				&& !contains_filter(value.russian, normalized_filter)
				&& !contains_filter(value.note, normalized_filter)) continue;
			ordered.push_back(&value);
		}
		std::sort(ordered.begin(), ordered.end(), [](auto left, auto right) {
			return std::tie(left->project_revision, left->term_version, left->changed_at)
				> std::tie(right->project_revision, right->term_version, right->changed_at);
		});
		for (auto value : ordered) {
			long row = history->InsertItem(history->GetItemCount(), to_wx(value->changed_at));
			history->SetItem(row, 1, history_operation_label(value->change_type));
			history->SetItem(row, 2, to_wx(value->english));
			history->SetItem(row, 3, to_wx(value->russian));
			history->SetItem(row, 4, to_wx(value->note.empty() ? "—" : value->note));
		}
		history->Show(!ordered.empty());
		history_empty->SetLabel(normalized_filter.empty()
			? _("No terminology history yet.")
			: _("No terminology history matches the current search."));
		history_empty->Show(ordered.empty());
	}

	void Populate() {
		auto normalized_filter = SanaeNormalizeSource(from_wx(filter->GetValue()));
		terms->DeleteAllItems();
		rows.clear();
		auto const& values = manager.Terminology();
		for (size_t index = 0; index < values.size(); ++index) {
			auto const& value = values[index];
			size_t draft_index = no_index;
			auto draft = DraftFor(value.id, &draft_index);
			auto english = draft && draft->operation == "update" ? draft->english : value.english;
			auto russian = draft && draft->operation == "update" ? draft->russian : value.russian;
			auto note = draft && draft->operation == "update" ? draft->note : value.note;
			if (!contains_filter(english, normalized_filter)
				&& !contains_filter(russian, normalized_filter)
				&& !contains_filter(note, normalized_filter)) continue;
			long row = terms->InsertItem(terms->GetItemCount(), to_wx(english));
			SetCell(row, 1, to_wx(russian));
			SetCell(row, 2, to_wx(note));
			wxString status = value.deleted ? _("Deleted") : _("Accepted");
			if (draft) {
				status = operation_label(draft->operation);
				if (draft->base_version != value.version)
					status = _("Changed on server — review queued edit");
			}
			SetCell(row, 3, status);
			rows.push_back({index, draft_index});
		}
		auto const& drafts = manager.TerminologyDrafts();
		for (size_t index = 0; index < drafts.size(); ++index) {
			auto const& value = drafts[index];
			if (value.operation != "create") continue;
			if (!contains_filter(value.english, normalized_filter)
				&& !contains_filter(value.russian, normalized_filter)
				&& !contains_filter(value.note, normalized_filter)) continue;
			long row = terms->InsertItem(terms->GetItemCount(), to_wx(value.english));
			SetCell(row, 1, to_wx(value.russian));
			SetCell(row, 2, to_wx(value.note));
			SetCell(row, 3, operation_label(value.operation));
			rows.push_back({no_index, index});
		}
		terms->Show(!rows.empty());
		terms_empty->SetLabel(normalized_filter.empty()
			? _("There are no project terms yet.")
			: _("No terms match the current search."));
		terms_empty->Show(rows.empty());
		PopulateHistory(normalized_filter);
		UpdateButtons();
		Layout();
	}

	void UpdateButtons() {
		auto selected = Selected();
		bool writable = manager.HasOpenEpisode();
		bool existing = selected && selected->term_index != no_index;
		bool have_draft = selected && selected->draft_index != no_index;
		edit->Enable(writable && selected);
		delete_restore->Enable(writable && existing);
		discard->Enable(writable && have_draft);
		if (existing) {
			auto const& value = manager.Terminology()[selected->term_index];
			delete_restore->SetLabel(value.deleted ? _("Restore") : _("Delete"));
		}
		else delete_restore->SetLabel(_("Delete"));
	}

	void Add() {
		if (!manager.HasOpenEpisode()) return;
		ShowSanaeTerminologyEntryDialog(context, this, {}, {});
		Populate();
	}
	void FindCandidates() {
		if (!manager.HasOpenEpisode()) return;
		TerminologyCandidateDialog(context, this).ShowModal();
		Populate();
	}

	void Edit() {
		auto selected = Selected();
		if (!selected || !manager.HasOpenEpisode()) return;
		try {
			if (selected->term_index == no_index) {
				auto old = manager.TerminologyDrafts()[selected->draft_index];
				TermEditDialog dialog(this, _("Edit queued term"), old.english, old.russian, old.note);
				if (dialog.ShowModal() != wxID_OK) return;
				auto replacement = dialog.Value();
				manager.RemoveTerminologyDraft(selected->draft_index);
				try { manager.QueueTerminology(std::move(replacement)); }
				catch (...) {
					manager.QueueTerminology(std::move(old));
					throw;
				}
			}
			else {
				auto const value = manager.Terminology()[selected->term_index];
				auto local = selected->draft_index == no_index
					? SanaeTerminologyDraft{value.english, value.russian, value.note}
					: manager.TerminologyDrafts()[selected->draft_index];
				if (value.deleted) {
					wxMessageBox(_("Restore this term before editing it."), _("Project terminology"),
						wxOK | wxICON_INFORMATION, this);
					return;
				}
				TermEditDialog dialog(this, _("Edit project term"), local.english, local.russian,
					local.note, selected->draft_index == no_index ? nullptr : &value,
					local.base_version);
				if (dialog.ShowModal() != wxID_OK) return;
				auto replacement = dialog.Value();
				manager.QueueTerminologyUpdate(value.id, value.version, std::move(replacement.english),
					std::move(replacement.russian), std::move(replacement.note));
			}
			Populate();
		}
		catch (std::exception const& error) {
			wxMessageBox(agi::wxformat(_("The terminology change could not be queued.\n\nDetails: %s"),
				to_wx(error.what())), _("Project terminology"), wxOK | wxICON_ERROR, this);
		}
	}

	void DeleteOrRestore() {
		auto selected = Selected();
		if (!selected || selected->term_index == no_index || !manager.HasOpenEpisode()) return;
		auto const value = manager.Terminology()[selected->term_index];
		wxString question = value.deleted
			? _("Queue restoration of this term for the next Finalize?")
			: _("Queue deletion of this term for the next Finalize?");
		if (wxMessageBox(question, _("Project terminology"),
			wxYES_NO | wxICON_QUESTION, this) != wxYES) return;
		try {
			if (value.deleted) manager.QueueTerminologyRestore(value.id, value.version);
			else manager.QueueTerminologyDelete(value.id, value.version);
			Populate();
		}
		catch (std::exception const& error) {
			wxMessageBox(to_wx(error.what()), _("Project terminology"), wxOK | wxICON_ERROR, this);
		}
	}

	void Discard() {
		auto selected = Selected();
		if (!selected || selected->draft_index == no_index) return;
		manager.RemoveTerminologyDraft(selected->draft_index);
		Populate();
	}

public:
	TerminologyDialog(agi::Context *c, wxWindow *parent)
	: wxDialog(parent ? parent : c->parent, -1, _("Project terminology"), wxDefaultPosition,
		wxSize(980, 650), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
	, context(c)
	, manager(*c->sanaeProject)
	{
		auto main = new wxBoxSizer(wxVERTICAL);
		auto search = new wxBoxSizer(wxHORIZONTAL);
		search->Add(new wxStaticText(this, -1, _("Search:")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
		search->Add(filter = new wxTextCtrl(this, -1), 1);
		main->Add(search, 0, wxEXPAND | wxBOTTOM, 8);

		auto book = new wxNotebook(this, -1);
		auto terms_page = new wxPanel(book);
		auto terms_sizer = new wxBoxSizer(wxVERTICAL);
		terms = new wxListCtrl(terms_page, -1, wxDefaultPosition, wxDefaultSize,
			wxLC_REPORT | wxLC_SINGLE_SEL);
		terms->InsertColumn(0, _("English"), wxLIST_FORMAT_LEFT, 190);
		terms->InsertColumn(1, _("Russian"), wxLIST_FORMAT_LEFT, 220);
		terms->InsertColumn(2, _("Note"), wxLIST_FORMAT_LEFT, 260);
		terms->InsertColumn(3, _("Status"), wxLIST_FORMAT_LEFT, 250);
		terms_sizer->Add(terms, 1, wxEXPAND | wxBOTTOM, 7);
		terms_empty = new wxStaticText(terms_page, -1, wxString(), wxDefaultPosition,
			wxDefaultSize, wxALIGN_CENTER_HORIZONTAL);
		terms_empty->Hide();
		terms_sizer->Add(terms_empty, 1, wxEXPAND | wxALL, 24);
		auto term_buttons = new wxBoxSizer(wxHORIZONTAL);
		auto add = new wxButton(terms_page, -1, _("Add term…"));
		auto find_candidates = new wxButton(terms_page, -1, _("Find new terms…"));
		edit = new wxButton(terms_page, -1, _("Edit…"));
		delete_restore = new wxButton(terms_page, -1, _("Delete"));
		discard = new wxButton(terms_page, -1, _("Discard queued change"));
		term_buttons->Add(add, 0, wxRIGHT, 6);
		term_buttons->Add(find_candidates, 0, wxRIGHT, 6);
		term_buttons->Add(edit, 0, wxRIGHT, 6);
		term_buttons->Add(delete_restore, 0, wxRIGHT, 6);
		term_buttons->Add(discard);
		terms_sizer->Add(term_buttons, 0, wxEXPAND);
		terms_page->SetSizer(terms_sizer);
		book->AddPage(terms_page, _("Terms"));

		auto history_page = new wxPanel(book);
		auto history_sizer = new wxBoxSizer(wxVERTICAL);
		history = new wxListCtrl(history_page, -1, wxDefaultPosition, wxDefaultSize,
			wxLC_REPORT | wxLC_SINGLE_SEL);
		history->InsertColumn(0, _("Changed at"), wxLIST_FORMAT_LEFT, 160);
		history->InsertColumn(1, _("Change"), wxLIST_FORMAT_LEFT, 90);
		history->InsertColumn(2, _("English"), wxLIST_FORMAT_LEFT, 180);
		history->InsertColumn(3, _("Russian"), wxLIST_FORMAT_LEFT, 220);
		history->InsertColumn(4, _("Note"), wxLIST_FORMAT_LEFT, 250);
		history_sizer->Add(history, 1, wxEXPAND);
		history_empty = new wxStaticText(history_page, -1, wxString(), wxDefaultPosition,
			wxDefaultSize, wxALIGN_CENTER_HORIZONTAL);
		history_empty->Hide();
		history_sizer->Add(history_empty, 1, wxEXPAND | wxALL, 24);
		history_page->SetSizer(history_sizer);
		book->AddPage(history_page, _("History"));
		main->Add(book, 1, wxEXPAND | wxBOTTOM, 8);

		help = new wxStaticText(this, -1, manager.HasOpenEpisode()
			? _("Changes are kept as a local draft and sent atomically with the next episode Finalize.")
			: _("Open a project episode to add or edit terms. The synchronized terminology and its history remain available offline."));
		help->Wrap(900);
		main->Add(help, 0, wxEXPAND | wxBOTTOM, 8);
		auto close_row = new wxBoxSizer(wxHORIZONTAL);
		close_row->AddStretchSpacer();
		close_row->Add(new wxButton(this, wxID_CLOSE));
		main->Add(close_row, 0, wxEXPAND);
		SetSizer(main);
		CentreOnParent();

		filter->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { Populate(); });
		terms->Bind(wxEVT_LIST_ITEM_SELECTED, [this](wxListEvent&) { UpdateButtons(); });
		terms->Bind(wxEVT_LIST_ITEM_DESELECTED, [this](wxListEvent&) { UpdateButtons(); });
		terms->Bind(wxEVT_LIST_ITEM_ACTIVATED, [this](wxListEvent&) { Edit(); });
		add->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Add(); });
		find_candidates->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { FindCandidates(); });
		edit->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Edit(); });
		delete_restore->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { DeleteOrRestore(); });
		discard->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Discard(); });
		Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { EndModal(wxID_CLOSE); }, wxID_CLOSE);
		Populate();
		find_candidates->Enable(manager.HasOpenEpisode());
	}
};
}

void ShowSanaeTerminologyDialog(agi::Context *context, wxWindow *parent) {
	if (context->sanaeProject->ActiveProjectId().empty()) {
		wxMessageBox(_("Open and synchronize a project first."), _("Project terminology"),
			wxOK | wxICON_INFORMATION, parent ? parent : context->parent);
		return;
	}
	TerminologyDialog(context, parent).ShowModal();
}
