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

/// @file dialog_search_replace.h
/// @see dialog_search_replace.cpp
/// @ingroup secondary_ui
///

#include <memory>

#include <wx/dialog.h>

namespace agi { struct Context; }
class SearchReplaceEngine;
struct SearchReplaceSettings;
struct SanaeFindProjectState;
class wxComboBox;

template<bool has_replace>
class DialogSearchReplace final : public wxDialog {
	agi::Context *c;
	std::unique_ptr<SearchReplaceSettings> settings;
	std::unique_ptr<SanaeFindProjectState> sanae_project_search;
	wxComboBox *replace_edit;

	void UpdateDropDowns();
	void FindReplace(bool (SearchReplaceEngine::*func)());
	void RunSanaeProjectSearch();
	void UpdateSanaeSearchMode();
	void ShowSanaeSearchSelected();
	void NavigateSanaeSearchSelected();

public:
	wxComboBox *find_edit;
	DialogSearchReplace(agi::Context* c);
	~DialogSearchReplace();
};
