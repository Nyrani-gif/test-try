// Copyright (c) 2026, Aegisub Sanae contributors

#include "dialog_sanae_final_review.h"

#include "ass_dialogue.h"
#include "ass_file.h"
#include "compat.h"
#include "format.h"
#include "include/aegisub/context.h"
#include "sanae_api.h"
#include "sanae_project.h"
#include "sanae_text.h"
#include "dialog_sanae_terminology.h"
#include "selection_controller.h"
#include "translation_project.h"

#include <libaegisub/ass/time.h>

#include <algorithm>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

#include <wx/button.h>
#include <wx/choicdlg.h>
#include <wx/dialog.h>
#include <wx/listbox.h>
#include <wx/listctrl.h>
#include <wx/msgdlg.h>
#include <wx/panel.h>
#include <wx/simplebook.h>
#include <wx/splitter.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

namespace {
class TerminologyEntryDialog final : public wxDialog {
	wxTextCtrl *english;
	wxTextCtrl *russian;
	wxTextCtrl *note;
public:
	TerminologyEntryDialog(wxWindow *parent, std::string english_value, std::string russian_value)
	: wxDialog(parent, -1, _("Add to project terminology"), wxDefaultPosition, wxDefaultSize,
		wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
	{
		auto fields = new wxFlexGridSizer(2, 6, 8);
		fields->AddGrowableCol(1, 1);
		fields->Add(new wxStaticText(this, -1, _("English:")), 0, wxALIGN_CENTER_VERTICAL);
		fields->Add(english = new wxTextCtrl(this, -1, to_wx(english_value)), 1, wxEXPAND);
		fields->Add(new wxStaticText(this, -1, _("Russian:")), 0, wxALIGN_CENTER_VERTICAL);
		fields->Add(russian = new wxTextCtrl(this, -1, to_wx(russian_value)), 1, wxEXPAND);
		fields->Add(new wxStaticText(this, -1, _("Note (optional):")), 0, wxALIGN_CENTER_VERTICAL);
		fields->Add(note = new wxTextCtrl(this, -1), 1, wxEXPAND);
		auto main = new wxBoxSizer(wxVERTICAL);
		main->Add(fields, 1, wxEXPAND | wxBOTTOM, 10);
		main->Add(CreateStdDialogButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND);
		SetSizerAndFit(main);
		SetSize(wxSize(560, GetSize().GetHeight()));
		CentreOnParent();
	}
	SanaeTerminologyDraft Value() const {
		return {from_wx(english->GetValue()), from_wx(russian->GetValue()), from_wx(note->GetValue())};
	}
};

wxString issue_title(std::string const& value) {
	if (value == "Different translations of the same ENSUB source")
		return _("Different translations of the same ENSUB source");
	if (value == "Possible inconsistent spelling")
		return _("Possible inconsistent spelling");
	if (value == "Exact ENSUB repeat") return _("Exact ENSUB repeat");
	if (value == "ENSUB fragment repeat") return _("Previous line fragment");
	if (value == "ENSUB split/merge repeat") return _("Split/merged previous lines");
	if (value == "Similar ENSUB repeat") return _("Similar ENSUB repeat");
	return to_wx(value);
}

wxString issue_detail(std::string const& value) {
	auto result = to_wx(value);
	result.Replace("Accepted:", _("Accepted:"));
	result.Replace("In this line:", _("In this line:"));
	result.Replace("not detected", _("not detected"));
	result.Replace("Current episode:", _("Current episode:"));
	result.Replace("Project:", _("Project:"));
	return result;
}

std::string readable_line(AssDialogue const& line) {
	auto text = line.GetStrippedText();
	for (size_t pos = 0; pos < text.size();) {
		auto found = text.find('\\', pos);
		if (found == std::string::npos || found + 1 >= text.size()) break;
		if (text[found + 1] == 'N' || text[found + 1] == 'n') {
			text.replace(found, 2, " ");
			pos = found + 1;
		}
		else pos = found + 2;
	}
	return text;
}

std::vector<long> selected_rows(wxListCtrl *list) {
	std::vector<long> result;
	for (long item = list->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
		item != -1; item = list->GetNextItem(item, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED))
		result.push_back(item);
	return result;
}

void bind_select_all(wxListCtrl *list) {
	list->Bind(wxEVT_KEY_DOWN, [list](wxKeyEvent& event) {
		if (event.ControlDown() && (event.GetKeyCode() == 'A' || event.GetKeyCode() == 'a')) {
			for (long i = 0; i < list->GetItemCount(); ++i)
				list->SetItemState(i, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
			return;
		}
		event.Skip();
	});
}

void bind_select_all(wxListBox *list) {
	list->Bind(wxEVT_KEY_DOWN, [list](wxKeyEvent& event) {
		if (event.ControlDown() && (event.GetKeyCode() == 'A' || event.GetKeyCode() == 'a')) {
			for (unsigned i = 0; i < list->GetCount(); ++i) list->SetSelection(i, true);
			return;
		}
		event.Skip();
	});
}

wxString candidate_reason(SanaeTerminologyCandidate const& candidate) {
	if (candidate.reason == "unknown_word") return _("not in English dictionary");
	if (candidate.reason == "name_phrase") return _("repeated name-like phrase");
	if (candidate.reason == "unknown_phrase") return _("phrase contains an unknown word");
	if (candidate.reason == "repeated_phrase") return _("stable repeated phrase");
	return _("worth checking");
}

wxString repeat_kind(SanaeRepeatKind kind) {
	if (kind == SanaeRepeatKind::Exact) return _("Exact match");
	if (kind == SanaeRepeatKind::Fragment) return _("Previous line fragment");
	if (kind == SanaeRepeatKind::Span) return _("Split/merged previous lines");
	if (kind == SanaeRepeatKind::Similar) return _("Similar line");
	return wxString();
}

class FinalReviewDialog final : public wxDialog {
	agi::Context *context;
	SanaeProjectManager& manager;
	wxListBox *categories = nullptr;
	wxSimplebook *book = nullptr;
	wxStaticText *summary = nullptr;
	wxButton *finalize_button = nullptr;

	wxListCtrl *candidate_list = nullptr;
	wxStaticText *candidate_empty = nullptr;
	wxTextCtrl *candidate_context = nullptr;
	wxListCtrl *terminology_list = nullptr;
	wxStaticText *terminology_empty = nullptr;
	wxTextCtrl *terminology_context = nullptr;
	wxListCtrl *repeat_list = nullptr;
	wxStaticText *repeat_empty = nullptr;
	wxTextCtrl *repeat_context = nullptr;
	wxListCtrl *internal_list = nullptr;
	wxStaticText *internal_empty = nullptr;
	wxTextCtrl *internal_context = nullptr;
	wxListBox *draft_list = nullptr;
	wxStaticText *draft_empty = nullptr;
	wxListBox *ignore_list = nullptr;
	wxStaticText *ignore_empty = nullptr;

	std::vector<SanaeTerminologyCandidate> candidates;
	std::vector<SanaeReviewIssue> term_issue_values;
	std::vector<SanaeReviewIssue> internal_issue_values;
	std::vector<SanaeReviewIssue> repeat_issue_values;
	bool finalized = false;


	wxPanel *MakeReviewPage(wxString explanation, wxListCtrl *&list, wxStaticText *&empty,
		wxString empty_text, wxTextCtrl *&details, wxBoxSizer *actions = nullptr)
	{
		auto page = new wxPanel(book);
		auto page_sizer = new wxBoxSizer(wxVERTICAL);
		auto help = new wxStaticText(page, -1, explanation);
		help->Wrap(760);
		page_sizer->Add(help, 0, wxEXPAND | wxBOTTOM, 8);

		auto splitter = new wxSplitterWindow(page, -1, wxDefaultPosition, wxDefaultSize,
			wxSP_LIVE_UPDATE | wxSP_3D);
		auto upper = new wxPanel(splitter);
		auto upper_sizer = new wxBoxSizer(wxVERTICAL);
		list = new wxListCtrl(upper, -1, wxDefaultPosition, wxDefaultSize,
			wxLC_REPORT | wxLC_HRULES | wxLC_VRULES);
		upper_sizer->Add(list, 1, wxEXPAND);
		empty = new wxStaticText(upper, -1, empty_text, wxDefaultPosition, wxDefaultSize,
			wxALIGN_CENTER_HORIZONTAL);
		empty->Wrap(640);
		upper_sizer->Add(empty, 1, wxEXPAND | wxALL, 24);
		if (actions) upper_sizer->Add(actions, 0, wxTOP, 8);
		upper->SetSizer(upper_sizer);

		auto lower = new wxPanel(splitter);
		auto lower_sizer = new wxBoxSizer(wxVERTICAL);
		lower_sizer->Add(new wxStaticText(lower, -1, _("Context")), 0, wxBOTTOM, 5);
		details = new wxTextCtrl(lower, -1, wxString(), wxDefaultPosition, wxDefaultSize,
			wxTE_MULTILINE | wxTE_READONLY | wxTE_RICH2);
		lower_sizer->Add(details, 1, wxEXPAND);
		lower->SetSizer(lower_sizer);
		splitter->SetMinimumPaneSize(90);
		splitter->SetSashGravity(0.68);
		splitter->SplitHorizontally(upper, lower, -190);
		page_sizer->Add(splitter, 1, wxEXPAND);
		page->SetSizer(page_sizer);
		bind_select_all(list);
		return page;
	}

	wxPanel *MakeSimplePage(wxString explanation, wxListBox *&list, wxStaticText *&empty,
		wxString empty_text, wxButton *&remove_button, wxString remove_text)
	{
		auto page = new wxPanel(book);
		auto sizer = new wxBoxSizer(wxVERTICAL);
		auto help = new wxStaticText(page, -1, explanation);
		help->Wrap(760);
		sizer->Add(help, 0, wxEXPAND | wxBOTTOM, 8);
		list = new wxListBox(page, -1, wxDefaultPosition, wxDefaultSize, 0, nullptr, wxLB_EXTENDED);
		sizer->Add(list, 1, wxEXPAND);
		empty = new wxStaticText(page, -1, empty_text, wxDefaultPosition, wxDefaultSize,
			wxALIGN_CENTER_HORIZONTAL);
		empty->Wrap(640);
		sizer->Add(empty, 1, wxEXPAND | wxALL, 24);
		remove_button = new wxButton(page, -1, remove_text);
		sizer->Add(remove_button, 0, wxTOP, 8);
		page->SetSizer(sizer);
		bind_select_all(list);
		return page;
	}

	void ShowListOrEmpty(wxWindow *list, wxStaticText *empty, bool has_items) {
		list->Show(has_items);
		empty->Show(!has_items);
		if (auto parent = list->GetParent()) parent->Layout();
	}

	void SetCategoryLabels() {
		categories->Clear();
		categories->Append(agi::wxformat(_("Candidates  %d"), static_cast<int>(candidates.size())));
		categories->Append(agi::wxformat(_("Terminology  %d"), static_cast<int>(term_issue_values.size())));
		categories->Append(agi::wxformat(_("Source repeats  %d"), static_cast<int>(repeat_issue_values.size())));
		categories->Append(agi::wxformat(_("Consistency  %d"), static_cast<int>(internal_issue_values.size())));
		categories->Append(agi::wxformat(_("Prepared terms  %d"), static_cast<int>(manager.TerminologyDrafts().size())));
		categories->Append(agi::wxformat(_("Exclusions  %d"), static_cast<int>(manager.IgnoreDrafts().size())));
		if (book->GetSelection() >= 0) categories->SetSelection(book->GetSelection());
		else categories->SetSelection(0);
	}

	void UpdateCandidateContext() {
		auto selected = selected_rows(candidate_list);
		if (selected.empty()) {
			candidate_context->ChangeValue(_("Select a candidate to see where it occurs and why Sanae suggested it."));
			return;
		}
		if (selected.size() > 1) {
			candidate_context->ChangeValue(agi::wxformat(_("Selected candidates: %d\n\nAvailable actions apply to the whole selection."),
				static_cast<int>(selected.size())));
			return;
		}
		auto index = static_cast<std::size_t>(selected.front());
		if (index >= candidates.size()) return;
		auto const& candidate = candidates[index];
		wxString text = agi::wxformat(_("%s\n\nIn this episode: %d\nIn previous episodes: %d\nReason: %s"),
			to_wx(candidate.english), candidate.occurrences, candidate.previous_occurrences,
			candidate_reason(candidate));
		for (auto const& value : candidate.contexts) {
			text += agi::wxformat("\n\n%s · %s\nEN: %s\nRU: %s",
				value.current_episode ? _("Current episode") : to_wx(value.episode_code),
				to_wx(agi::Time(value.start).GetAssFormatted(true)), to_wx(value.source),
				to_wx(value.russian.empty() ? "—" : value.russian));
		}
		candidate_context->ChangeValue(text);
	}

	void UpdateTerminologyContext() {
		auto selected = selected_rows(terminology_list);
		if (selected.empty()) {
			terminology_context->ChangeValue(_("Select a terminology warning to see the accepted form and the aligned subtitle line."));
			return;
		}
		if (selected.size() > 1) {
			terminology_context->ChangeValue(agi::wxformat(_("Selected occurrences: %d\n\nOnly the selected, unambiguous lines will be corrected."),
				static_cast<int>(selected.size())));
			return;
		}
		auto index = static_cast<std::size_t>(selected.front());
		if (index >= term_issue_values.size()) return;
		auto const& issue = term_issue_values[index];
		wxString text = issue_title(issue.title);
		if (!issue.replacement_to.empty())
			text += agi::wxformat(_("\n\nAccepted in project: %s"), to_wx(issue.replacement_to));
		if (!issue.replacement_from.empty())
			text += agi::wxformat(_("\nFound in this line: %s"), to_wx(issue.replacement_from));
		if (issue.line) {
			text += agi::wxformat("\n\n%s · %s\nEN: %s\nRU: %s",
				_("Current episode"), to_wx(agi::Time(issue.line->Start).GetAssFormatted(true)),
				to_wx(context->translationProject->SourceDisplayTextCached(issue.line)),
				to_wx(readable_line(*issue.line)));
		}
		terminology_context->ChangeValue(text);
	}

	void UpdateRepeatContext() {
		auto selected = selected_rows(repeat_list);
		if (selected.empty()) {
			repeat_context->ChangeValue(_("Select a repeat to compare the current line with the previous episode."));
			return;
		}
		if (selected.size() > 1) {
			repeat_context->ChangeValue(agi::wxformat(_("Selected repeats: %d\n\nOnly exact matches with a different translation are eligible for reuse."),
				static_cast<int>(selected.size())));
			return;
		}
		auto index = static_cast<std::size_t>(selected.front());
		if (index >= repeat_issue_values.size()) return;
		auto const& issue = repeat_issue_values[index];
		auto match = manager.RepeatFor(issue.line);
		if (!issue.line || !match) return;
		auto current_ru = match->kind == SanaeRepeatKind::Span && !match->current_span_russian.empty()
			? match->current_span_russian : readable_line(*issue.line);
		auto current_en = match->kind == SanaeRepeatKind::Span && !match->current_span_source.empty()
			? match->current_span_source : context->translationProject->SourceDisplayTextCached(issue.line);
		bool same_translation = !match->russian.empty()
			&& SanaeNormalizeSource(current_ru) == SanaeNormalizeSource(match->russian);
		wxString text = repeat_kind(match->kind);
		text += agi::wxformat("\n\n%s · %s\nEN: %s\nRU: %s",
			_("Now"), to_wx(agi::Time(issue.line->Start).GetAssFormatted(true)),
			to_wx(current_en), to_wx(current_ru));
		text += agi::wxformat("\n\n%s · %s · %s\nEN: %s\nRU: %s",
			_("Earlier"), to_wx(match->episode_code), to_wx(agi::Time(match->start).GetAssFormatted(true)),
			to_wx(match->source), to_wx(match->russian.empty() ? "—" : match->russian));
		if (match->kind == SanaeRepeatKind::Exact && same_translation)
			text += _("\n\n✓ Translation already matches.");
		else if (match->kind == SanaeRepeatKind::Exact)
			text += _("\n\nThe previous translation can be reused for this exact match.");
		else if (match->kind == SanaeRepeatKind::Fragment)
			text += _("\n\nThis is a long exact fragment of an earlier line. The previous translation is reference only.");
		else if (match->kind == SanaeRepeatKind::Span)
			text += agi::wxformat(_("\n\nThe same or very similar source was split/merged differently (%d current lines ↔ %d previous lines). The previous translation is reference only."),
				match->current_span_lines, match->source_span_lines);
		else
			text += _("\n\nThis is a similar line. The previous translation is reference only.");
		repeat_context->ChangeValue(text);
	}

	void UpdateInternalContext() {
		auto selected = selected_rows(internal_list);
		if (selected.empty()) {
			internal_context->ChangeValue(_("Select a recommendation to see the source and current Russian line."));
			return;
		}
		auto index = static_cast<std::size_t>(selected.front());
		if (index >= internal_issue_values.size()) return;
		auto const& issue = internal_issue_values[index];
		wxString text = issue_title(issue.title) + "\n\n" + issue_detail(issue.detail);
		if (issue.line)
			text += agi::wxformat("\n\n%s · %s\nEN: %s\nRU: %s", _("Current episode"),
				to_wx(agi::Time(issue.line->Start).GetAssFormatted(true)),
				to_wx(context->translationProject->SourceDisplayTextCached(issue.line)),
				to_wx(readable_line(*issue.line)));
		internal_context->ChangeValue(text);
	}

	void Populate() {
		int page = book ? book->GetSelection() : 0;
		candidates = manager.GenerateCandidates();
		term_issue_values = manager.TerminologyConsistencyIssues();
		internal_issue_values = manager.InternalConsistencyIssues();
		repeat_issue_values = manager.SourceRepeatIssues();

		candidate_list->DeleteAllItems();
		for (std::size_t i = 0; i < candidates.size(); ++i) {
			auto const& candidate = candidates[i];
			long row = candidate_list->InsertItem(static_cast<long>(i), to_wx(candidate.english));
			candidate_list->SetItem(row, 1, to_wx(std::to_string(candidate.project_episode_count)));
			candidate_list->SetItem(row, 2, to_wx(std::to_string(candidate.occurrences)));
			candidate_list->SetItem(row, 3, to_wx(std::to_string(candidate.previous_occurrences)));
			candidate_list->SetItem(row, 4, candidate_reason(candidate));
		}
		ShowListOrEmpty(candidate_list, candidate_empty, !candidates.empty());
		UpdateCandidateContext();

		terminology_list->DeleteAllItems();
		for (std::size_t i = 0; i < term_issue_values.size(); ++i) {
			auto const& issue = term_issue_values[i];
			long row = terminology_list->InsertItem(static_cast<long>(i), issue_title(issue.title));
			terminology_list->SetItem(row, 1, to_wx(issue.replacement_to.empty() ? "—" : issue.replacement_to));
			terminology_list->SetItem(row, 2, to_wx(issue.replacement_from.empty() ? "—" : issue.replacement_from));
			terminology_list->SetItem(row, 3, issue.line
				? to_wx(agi::Time(issue.line->Start).GetAssFormatted(true)) : wxString("—"));
		}
		ShowListOrEmpty(terminology_list, terminology_empty, !term_issue_values.empty());
		UpdateTerminologyContext();

		repeat_list->DeleteAllItems();
		for (std::size_t i = 0; i < repeat_issue_values.size(); ++i) {
			auto const& issue = repeat_issue_values[i];
			auto match = manager.RepeatFor(issue.line);
			long row = repeat_list->InsertItem(static_cast<long>(i), match ? repeat_kind(match->kind) : issue_title(issue.title));
			repeat_list->SetItem(row, 1, issue.line
				? to_wx(agi::Time(issue.line->Start).GetAssFormatted(true)) : wxString("—"));
			repeat_list->SetItem(row, 2, match ? to_wx(match->episode_code) : wxString("—"));
			wxString state = _("Reference");
			if (match && match->kind == SanaeRepeatKind::Exact) {
				auto current_ru = issue.line ? readable_line(*issue.line) : std::string();
				state = !match->russian.empty()
					&& SanaeNormalizeSource(current_ru) == SanaeNormalizeSource(match->russian)
					? _("Translation matches") : _("Can reuse translation");
			}
			repeat_list->SetItem(row, 3, state);
		}
		ShowListOrEmpty(repeat_list, repeat_empty, !repeat_issue_values.empty());
		UpdateRepeatContext();

		internal_list->DeleteAllItems();
		for (std::size_t i = 0; i < internal_issue_values.size(); ++i) {
			auto const& issue = internal_issue_values[i];
			long row = internal_list->InsertItem(static_cast<long>(i), issue_title(issue.title));
			internal_list->SetItem(row, 1, issue_detail(issue.detail));
			internal_list->SetItem(row, 2, issue.line
				? to_wx(agi::Time(issue.line->Start).GetAssFormatted(true)) : wxString("—"));
		}
		ShowListOrEmpty(internal_list, internal_empty, !internal_issue_values.empty());
		UpdateInternalContext();

		draft_list->Clear();
		for (auto const& draft : manager.TerminologyDrafts()) {
			wxString operation = draft.operation == "update" ? _("Update")
				: draft.operation == "delete" ? _("Delete")
				: draft.operation == "restore" ? _("Restore") : _("Create");
			draft_list->Append(agi::wxformat("[%s] %s → %s%s", operation,
				to_wx(draft.english), to_wx(draft.russian),
				draft.note.empty() ? wxString() : agi::wxformat(" (%s)", to_wx(draft.note))));
		}
		ShowListOrEmpty(draft_list, draft_empty, draft_list->GetCount() > 0);

		ignore_list->Clear();
		for (auto const& draft : manager.IgnoreDrafts())
			ignore_list->Append(agi::wxformat("%s — %s", draft.scope == "project" ? _("Whole project") : _("This episode only"),
				to_wx(draft.text)));
		ShowListOrEmpty(ignore_list, ignore_empty, ignore_list->GetCount() > 0);

		SetCategoryLabels();
		if (page >= 0 && page < static_cast<int>(book->GetPageCount())) {
			book->SetSelection(page);
			categories->SetSelection(page);
		}
		int warnings = static_cast<int>(term_issue_values.size());
		int informational = 0;
		int repeat_recommendations = 0;
		for (auto const& issue : repeat_issue_values) {
			auto match = manager.RepeatFor(issue.line);
			bool already_matches = match && match->kind == SanaeRepeatKind::Exact && issue.line
				&& !match->russian.empty()
				&& SanaeNormalizeSource(readable_line(*issue.line)) == SanaeNormalizeSource(match->russian);
			if (already_matches) ++informational;
			else ++repeat_recommendations;
		}
		int recommendations = static_cast<int>(candidates.size() + internal_issue_values.size()) + repeat_recommendations;
		summary->SetLabel(informational
			? agi::wxformat(_("%d warning(s) · %d recommendation(s) · %d informational. Finalization is available at any time."),
				warnings, recommendations, informational)
			: agi::wxformat(_("%d warning(s) · %d recommendation(s). Finalization is available at any time."),
				warnings, recommendations));
		Layout();
	}

	void AddCandidates() {
		auto rows = selected_rows(candidate_list);
		if (rows.empty()) return;
		std::vector<std::string> values;
		for (auto row : rows)
			if (row >= 0 && static_cast<std::size_t>(row) < candidates.size()) values.push_back(candidates[row].english);
		for (auto const& english : values)
			if (!ShowSanaeTerminologyEntryDialog(context, this, english, {})) break;
		Populate();
	}

	void IgnoreCandidates() {
		auto rows = selected_rows(candidate_list);
		if (rows.empty()) return;
		wxArrayString choices{_("Whole project"), _("This episode only")};
		wxSingleChoiceDialog scope(this, _("Where should the selected candidates be ignored?"),
			_("Ignore terminology candidates"), choices);
		if (scope.ShowModal() != wxID_OK) return;
		try {
			for (auto row : rows)
				if (row >= 0 && static_cast<std::size_t>(row) < candidates.size())
					manager.QueueIgnore({scope.GetSelection() == 0 ? "project" : "episode", candidates[row].english, "en"});
			Populate();
		}
		catch (std::exception const& error) {
			wxMessageBox(to_wx(error.what()), _("Final review"), wxOK | wxICON_ERROR, this);
		}
	}

	void RemoveDrafts() {
		wxArrayInt selected;
		draft_list->GetSelections(selected);
		std::vector<int> rows;
		rows.reserve(selected.GetCount());
		for (std::size_t i = 0; i < selected.GetCount(); ++i) rows.push_back(selected[i]);
		std::sort(rows.rbegin(), rows.rend());
		for (int row : rows) manager.RemoveTerminologyDraft(static_cast<size_t>(row));
		Populate();
	}

	void RemoveIgnores() {
		wxArrayInt selected;
		ignore_list->GetSelections(selected);
		std::vector<int> rows;
		rows.reserve(selected.GetCount());
		for (std::size_t i = 0; i < selected.GetCount(); ++i) rows.push_back(selected[i]);
		std::sort(rows.rbegin(), rows.rend());
		for (int row : rows) manager.RemoveIgnoreDraft(static_cast<size_t>(row));
		Populate();
	}

	void Navigate(std::vector<SanaeReviewIssue> const& issues, wxListCtrl *list) {
		auto rows = selected_rows(list);
		if (rows.empty()) return;
		auto index = static_cast<std::size_t>(rows.front());
		if (index >= issues.size() || !issues[index].line) return;
		context->selectionController->SetSelectionAndActive({issues[index].line}, issues[index].line);
	}

	void ApplySelectedExactRepeats() {
		std::vector<std::pair<AssDialogue *, std::string>> replacements;
		int protected_lines = 0;
		for (auto row : selected_rows(repeat_list)) {
			if (row < 0 || static_cast<std::size_t>(row) >= repeat_issue_values.size()) continue;
			auto line = repeat_issue_values[row].line;
			auto match = manager.RepeatFor(line);
			if (!line || !match || match->kind != SanaeRepeatKind::Exact || match->russian.empty()) continue;
			if (SanaeNormalizeSource(readable_line(*line)) == SanaeNormalizeSource(match->russian)) continue;
			if (line->Text.get().find('{') != std::string::npos) {
				++protected_lines;
				continue;
			}
			replacements.emplace_back(line, match->russian);
		}
		if (replacements.empty()) {
			wxMessageBox(protected_lines
				? _("Selected lines contain ASS override tags. Open those lines individually to review the replacement safely.")
				: _("No selected exact repeat needs a translation change."),
				_("Exact source repeats"), wxOK | wxICON_INFORMATION, this);
			return;
		}
		wxString message = agi::wxformat(_("Use the previous translation for %d selected exact repeat(s)?"),
			static_cast<int>(replacements.size()));
		if (protected_lines)
			message += agi::wxformat(_("\n\n%d selected line(s) with ASS override tags will be left unchanged."), protected_lines);
		if (wxMessageBox(message, _("Exact source repeats"),
			wxYES_NO | wxNO_DEFAULT | wxICON_QUESTION, this) != wxYES) return;
		for (auto const& replacement : replacements) replacement.first->Text = replacement.second;
		context->ass->Commit(_("use selected previous project translations"), AssFile::COMMIT_DIAG_TEXT);
		Populate();
	}

	void ApplySelectedTerminologyFixes() {
		std::vector<std::pair<AssDialogue *, wxString>> replacements;
		int unsafe = 0;
		for (auto row : selected_rows(terminology_list)) {
			if (row < 0 || static_cast<std::size_t>(row) >= term_issue_values.size()) continue;
			auto const& issue = term_issue_values[row];
			if (!issue.line || issue.replacement_from.empty() || issue.replacement_to.empty()
				|| issue.line->Text.get().find('{') != std::string::npos) {
				++unsafe;
				continue;
			}
			auto existing = std::find_if(replacements.begin(), replacements.end(),
				[&](auto const& value) { return value.first == issue.line; });
			if (existing == replacements.end()) {
				replacements.emplace_back(issue.line, to_wx(issue.line->Text.get()));
				existing = std::prev(replacements.end());
			}
			auto& raw = existing->second;
			wxString folded = raw.Lower();
			wxString needle = to_wx(issue.replacement_from).Lower();
			auto position = folded.find(needle);
			if (needle.empty() || position == wxString::npos
				|| folded.find(needle, position + needle.size()) != wxString::npos) {
				++unsafe;
				continue;
			}
			raw.replace(position, needle.size(), to_wx(issue.replacement_to));
		}
		replacements.erase(std::remove_if(replacements.begin(), replacements.end(), [](auto const& value) {
			return from_wx(value.second) == value.first->Text.get();
		}), replacements.end());
		if (replacements.empty()) {
			wxMessageBox(unsafe
				? _("The selected occurrences cannot be changed safely as a direct text substitution. Open them individually for review.")
				: _("No terminology corrections are selected."),
				_("Terminology consistency"), wxOK | wxICON_INFORMATION, this);
			return;
		}
		wxString message = agi::wxformat(_("Correct %d selected terminology occurrence(s)?"),
			static_cast<int>(replacements.size()));
		if (unsafe) message += agi::wxformat(_("\n\n%d ambiguous or formatted occurrence(s) will be left unchanged."), unsafe);
		if (wxMessageBox(message, _("Terminology consistency"),
			wxYES_NO | wxNO_DEFAULT | wxICON_QUESTION, this) != wxYES) return;
		for (auto const& replacement : replacements) replacement.first->Text = from_wx(replacement.second);
		context->ass->Commit(_("correct selected project terminology"), AssFile::COMMIT_DIAG_TEXT);
		Populate();
	}

	void Finalize() {
		auto term_count = manager.TerminologyDrafts().size();
		auto ignore_count = manager.IgnoreDrafts().size();
		if (term_count || ignore_count) {
			auto message = agi::wxformat(_("The finalization will save:\n\nTerminology changes: %d\nExclusions: %d\n\nContinue?"),
				static_cast<int>(term_count), static_cast<int>(ignore_count));
			if (wxMessageBox(message, _("Finalize episode"),
				wxYES_NO | wxYES_DEFAULT | wxICON_QUESTION, this) != wxYES) return;
		}
		try {
			auto stats = manager.Finalize();
			finalized = true;
			auto message = agi::wxformat(
				_("Episode finalized successfully.\n\nCompact RUSUB: %d events; %d drawing events removed; %d technical duplicates collapsed."),
				static_cast<int>(stats.output_events), static_cast<int>(stats.drawings_removed),
				static_cast<int>(stats.technical_duplicates_collapsed));
			bool local_warning = !manager.LastFinalizeWarning().empty();
			if (local_warning)
				message += agi::wxformat(_("\n\nThe server committed Finalize, but some local cache updates failed. Use Sync to repair them:\n%s"),
					to_wx(manager.LastFinalizeWarning()));
			wxMessageBox(message, _("Final review"), wxOK | (local_warning ? wxICON_WARNING : wxICON_INFORMATION), this);
			EndModal(wxID_OK);
		}
		catch (SanaeApiError const& error) {
			wxString message;
			if (error.Code() == "term_version_conflict") {
				message = _("A terminology entry was changed on the server after your last synchronization.\n"
					"The local review draft and production ASS were not discarded.");
				try { manager.SyncProject(manager.ActiveProjectId()); }
				catch (std::exception const& sync_error) {
					message += agi::wxformat(_("\n\nThe latest server version could not be loaded.\nDetails: %s"), to_wx(sync_error.what()));
				}
				wxMessageBox(message, _("Terminology conflict"), wxOK | wxICON_WARNING, this);
				ShowSanaeTerminologyDialog(context, this);
				Populate();
				return;
			}
			if (error.Code() == "competing_finalize")
				message = _("This episode was finalized by another device after your last synchronization.");
			else if (error.Code() == "source_changed")
				message = _("The English source file changed on the server. Synchronize the project before finalizing.");
			else if (error.Code() == "authentication_required")
				message = _("The saved device authorization is no longer valid. Register the device again.");
			else message = _("The server rejected Finalize.");
			message += agi::wxformat(_("\n\nThe local review draft and production ASS were not discarded.\nDetails: %s"), to_wx(error.what()));
			wxMessageBox(message, _("Final review"), wxOK | wxICON_ERROR, this);
		}
		catch (std::exception const& error) {
			wxMessageBox(agi::wxformat(_("Finalize failed. The local review draft and production ASS were not discarded.\n\n%s"),
				to_wx(error.what())), _("Final review"), wxOK | wxICON_ERROR, this);
		}
	}

public:
	FinalReviewDialog(agi::Context *c, wxWindow *parent)
	: wxDialog(parent ? parent : c->parent, -1, _("Final review"), wxDefaultPosition,
		wxSize(1080, 720), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
	, context(c)
	, manager(*c->sanaeProject)
	{
		auto main = new wxBoxSizer(wxVERTICAL);
		auto episode = manager.ActiveEpisode();
		auto heading = new wxStaticText(this, -1, episode
			? agi::wxformat(_("Final review — episode %s"), to_wx(episode->episode_code)) : _("Final review"));
		auto heading_font = heading->GetFont();
		heading_font.SetPointSize(heading_font.GetPointSize() + 2);
		heading_font.SetWeight(wxFONTWEIGHT_BOLD);
		heading->SetFont(heading_font);
		main->Add(heading, 0, wxEXPAND | wxBOTTOM, 10);

		auto content = new wxBoxSizer(wxHORIZONTAL);
		categories = new wxListBox(this, -1, wxDefaultPosition, wxSize(220, -1));
		content->Add(categories, 0, wxEXPAND | wxRIGHT, 10);
		book = new wxSimplebook(this, -1);
		content->Add(book, 1, wxEXPAND);
		main->Add(content, 1, wxEXPAND | wxBOTTOM, 10);

		auto candidate_page = MakeReviewPage(
			_("Potential project terms. Ordinary English dictionary words are filtered out; nothing is added automatically."),
			candidate_list, candidate_empty, _("✓ No useful terminology candidates were found."), candidate_context);
		candidate_list->AppendColumn(_("Candidate"), wxLIST_FORMAT_LEFT, 230);
		candidate_list->AppendColumn(_("Episodes"), wxLIST_FORMAT_RIGHT, 75);
		candidate_list->AppendColumn(_("This episode"), wxLIST_FORMAT_RIGHT, 90);
		candidate_list->AppendColumn(_("Earlier"), wxLIST_FORMAT_RIGHT, 75);
		candidate_list->AppendColumn(_("Reason"), wxLIST_FORMAT_LEFT, 240);
		auto candidate_buttons = new wxBoxSizer(wxHORIZONTAL);
		auto add = new wxButton(candidate_page, -1, _("Add selected to terminology"));
		auto ignore = new wxButton(candidate_page, -1, _("Ignore selected"));
		candidate_buttons->Add(add, 0, wxRIGHT, 6);
		candidate_buttons->Add(ignore);
		// Actions live outside the splitter so they stay visible while the context pane is resized.
		candidate_page->GetSizer()->Add(candidate_buttons, 0, wxTOP, 8);
		book->AddPage(candidate_page, wxString());

		auto terminology_page = MakeReviewPage(
			_("Known project terms whose aligned Russian line may differ from the accepted translation."),
			terminology_list, terminology_empty,
			_("✓ No terminology violations were found. Known project terms are used consistently in this episode."),
			terminology_context);
		terminology_list->AppendColumn(_("Term"), wxLIST_FORMAT_LEFT, 260);
		terminology_list->AppendColumn(_("Accepted"), wxLIST_FORMAT_LEFT, 180);
		terminology_list->AppendColumn(_("Found"), wxLIST_FORMAT_LEFT, 180);
		terminology_list->AppendColumn(_("Time"), wxLIST_FORMAT_LEFT, 100);
		auto terminology_actions = new wxBoxSizer(wxHORIZONTAL);
		auto apply_terms = new wxButton(terminology_page, -1, _("Correct selected"));
		auto go_to_term = new wxButton(terminology_page, -1, _("Go to line"));
		terminology_actions->Add(apply_terms, 0, wxRIGHT, 6);
		terminology_actions->Add(go_to_term);
		terminology_page->GetSizer()->Add(terminology_actions, 0, wxTOP, 8);
		book->AddPage(terminology_page, wxString());

		auto repeat_page = MakeReviewPage(
			_("Previous ENSUB matches. Exact matches can reuse a translation; fragments and similar lines are reference only."),
			repeat_list, repeat_empty, _("✓ No suspicious source repeats were found."), repeat_context);
		repeat_list->AppendColumn(_("Type"), wxLIST_FORMAT_LEFT, 190);
		repeat_list->AppendColumn(_("Now"), wxLIST_FORMAT_LEFT, 105);
		repeat_list->AppendColumn(_("Earlier episode"), wxLIST_FORMAT_LEFT, 120);
		repeat_list->AppendColumn(_("Status"), wxLIST_FORMAT_LEFT, 190);
		auto repeat_actions = new wxBoxSizer(wxHORIZONTAL);
		auto apply_repeats = new wxButton(repeat_page, -1, _("Use previous translation for selected exact matches"));
		auto go_to_repeat = new wxButton(repeat_page, -1, _("Go to line"));
		repeat_actions->Add(apply_repeats, 0, wxRIGHT, 6);
		repeat_actions->Add(go_to_repeat);
		repeat_page->GetSizer()->Add(repeat_actions, 0, wxTOP, 8);
		book->AddPage(repeat_page, wxString());

		auto internal_page = MakeReviewPage(
			_("Conservative consistency hints. Short everyday replies such as “yeah”, “what?” and “thanks” are ignored."),
			internal_list, internal_empty, _("✓ No meaningful consistency issues were found."), internal_context);
		internal_list->AppendColumn(_("Check"), wxLIST_FORMAT_LEFT, 270);
		internal_list->AppendColumn(_("Details"), wxLIST_FORMAT_LEFT, 420);
		internal_list->AppendColumn(_("Time"), wxLIST_FORMAT_LEFT, 100);
		auto go_to_internal = new wxButton(internal_page, -1, _("Go to line"));
		internal_page->GetSizer()->Add(go_to_internal, 0, wxTOP, 8);
		book->AddPage(internal_page, wxString());

		wxButton *remove_draft = nullptr;
		auto draft_page = MakeSimplePage(
			_("Terminology changes prepared for this episode. They are sent only when you finalize."),
			draft_list, draft_empty, _("No terminology changes are prepared."), remove_draft, _("Remove selected"));
		book->AddPage(draft_page, wxString());

		wxButton *remove_ignore = nullptr;
		auto ignore_page = MakeSimplePage(
			_("Candidates you chose not to show again. These changes are saved when you finalize."),
			ignore_list, ignore_empty, _("No exclusions are prepared."), remove_ignore, _("Remove selected"));
		book->AddPage(ignore_page, wxString());

		summary = new wxStaticText(this, -1, wxString());
		main->Add(summary, 0, wxEXPAND | wxBOTTOM, 8);
		auto buttons = new wxBoxSizer(wxHORIZONTAL);
		buttons->AddStretchSpacer();
		auto close = new wxButton(this, wxID_CANCEL, _("Close"));
		finalize_button = new wxButton(this, -1, _("Finalize episode"));
		finalize_button->SetDefault();
		buttons->Add(close, 0, wxRIGHT, 8);
		buttons->Add(finalize_button);
		main->Add(buttons, 0, wxEXPAND);
		SetSizer(main);
		SetMinSize(wxSize(860, 560));
		CentreOnParent();

		categories->Bind(wxEVT_LISTBOX, [this](wxCommandEvent&) {
			int selected = categories->GetSelection();
			if (selected >= 0 && selected < static_cast<int>(book->GetPageCount())) book->SetSelection(selected);
		});
		add->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { AddCandidates(); });
		ignore->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { IgnoreCandidates(); });
		apply_terms->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { ApplySelectedTerminologyFixes(); });
		go_to_term->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Navigate(term_issue_values, terminology_list); });
		apply_repeats->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { ApplySelectedExactRepeats(); });
		go_to_repeat->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Navigate(repeat_issue_values, repeat_list); });
		go_to_internal->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Navigate(internal_issue_values, internal_list); });
		remove_draft->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { RemoveDrafts(); });
		remove_ignore->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { RemoveIgnores(); });
		finalize_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Finalize(); });

		auto bind_context = [](wxListCtrl *list, auto update) {
			list->Bind(wxEVT_LIST_ITEM_SELECTED, update);
			list->Bind(wxEVT_LIST_ITEM_DESELECTED, update);
		};
		bind_context(candidate_list, [this](wxListEvent&) { UpdateCandidateContext(); });
		bind_context(terminology_list, [this](wxListEvent&) { UpdateTerminologyContext(); });
		bind_context(repeat_list, [this](wxListEvent&) { UpdateRepeatContext(); });
		bind_context(internal_list, [this](wxListEvent&) { UpdateInternalContext(); });
		terminology_list->Bind(wxEVT_LIST_ITEM_ACTIVATED,
			[this](wxListEvent&) { Navigate(term_issue_values, terminology_list); });
		repeat_list->Bind(wxEVT_LIST_ITEM_ACTIVATED,
			[this](wxListEvent&) { Navigate(repeat_issue_values, repeat_list); });
		internal_list->Bind(wxEVT_LIST_ITEM_ACTIVATED,
			[this](wxListEvent&) { Navigate(internal_issue_values, internal_list); });

		Populate();
		book->SetSelection(0);
		categories->SetSelection(0);
	}

	bool WasFinalized() const { return finalized; }
};
}

bool ShowSanaeTerminologyEntryDialog(
	agi::Context *context,
	wxWindow *parent,
	std::string english,
	std::string russian)
{
	TerminologyEntryDialog dialog(
		parent ? parent : context->parent,
		std::move(english),
		std::move(russian));

	if (dialog.ShowModal() != wxID_OK)
		return false;

	try {
		context->sanaeProject->QueueTerminology(dialog.Value());
		return true;
	}
	catch (std::exception const& error) {
		wxMessageBox(
			to_wx(error.what()),
			_("Terminology draft"),
			wxOK | wxICON_ERROR,
			parent);
		return false;
	}
}

bool ShowSanaeFinalReview(agi::Context *context, wxWindow *parent) {
	if (!context->sanaeProject->HasOpenEpisode())
		return false;

	FinalReviewDialog dialog(context, parent);
	dialog.ShowModal();
	return dialog.WasFinalized();
}