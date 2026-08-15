// Copyright (c) 2011, Thomas Goyne <plorkyeran@aegisub.org>
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

#include <libaegisub/exception.h>
#include <libaegisub/signal.h>

#include <memory>
#include <vector>
#include <wx/dialog.h>

namespace agi { struct Context; }
class AssDialogue;
class AssDialogueBlock;
class PersistLocation;
class SubsTextEditCtrl;
class wxCheckBox;
class wxStaticText;
class wxStyledTextCtrl;

/// Assistant for translating subtitles in one language to another language
class DialogTranslation final : public wxDialog {
	agi::Context *c;

	agi::signal::Connection file_change_connection;
	agi::signal::Connection active_line_connection;
	agi::signal::Connection project_change_connection;

	/// The active line
	AssDialogue *active_line;
	/// The parsed dialogue blocks for the active line
	std::vector<std::unique_ptr<AssDialogueBlock>> blocks;
	/// Which dialogue block in the active line is currently being translated
	size_t cur_block = 0;

	/// Total number of dialogue lines in the file
	size_t line_count;

	/// Should active line change announcements be ignored?
	bool switching_lines = false;
	bool workspace_mode = false;
	bool translator_mode = true;

	wxStaticText *line_number_display;
	wxStaticText *source_path_display;
	wxStaticText *mode_description;
	wxStaticText *previous_context;
	wxStaticText *next_context;
	wxStaticText *qc_display;
	wxStyledTextCtrl *original_text;
	SubsTextEditCtrl *translated_text;
	wxCheckBox *seek_video;
	class wxChoice *status_choice;
	class wxChoice *filter_choice;
	class wxChoice *assistant_mode;
	class wxTextCtrl *search_box;
	wxCheckBox *match_case;
	wxCheckBox *protect_tags;
	class wxButton *quick_review_button;
	class wxSizer *review_sizer;
	class wxSizer *tags_sizer;

	std::unique_ptr<PersistLocation> persist;

	void OnPlayAudioButton(wxCommandEvent &);
	void OnPlayVideoButton(wxCommandEvent &);
	void OnKeyDown(wxKeyEvent &evt);
	void OnExternalCommit(int commit_type);
	void OnProjectChanged();
	void OnLoadSource(wxCommandEvent &);
	void OnModeChanged(wxCommandEvent &);
	void OnStatusChanged(wxCommandEvent &);
	void OnApplyStatusSelected(wxCommandEvent &);
	void OnMeaningCheckedNext(wxCommandEvent &);
	void OnFilterChanged(wxCommandEvent &);
	void OnSearch(wxCommandEvent &);
	void OnHistory(wxCommandEvent &);
	void OnInspectTags(wxCommandEvent &);
	bool MoveWorkspaceLine(int direction);
	void UpdateModePresentation();

	void UpdateDisplay();
	bool FinishOrStartFinalReview();

	void OnActiveLineChanged(AssDialogue *new_line);

public:
	DialogTranslation(agi::Context *context);
	~DialogTranslation();

	bool NextBlock();
	bool PrevBlock();
	bool Commit(bool next);
	void InsertOriginal();

	struct NothingToTranslate { };
};
