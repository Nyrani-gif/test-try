// Copyright (c) 2014, Thomas Goyne <plorkyeran@aegisub.org>
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

#include "subtitle_format.h"

class AssSubtitleFormat final : public SubtitleFormat {
public:
	AssSubtitleFormat() : SubtitleFormat("Advanced SubStation Alpha") { }

	std::vector<std::string> GetReadWildcards() const override { return {"ass", "ssa"}; }
	std::vector<std::string> GetWriteWildcards() const override { return {"ass"}; }

	// Naturally the ASS subtitle format can save all ASS files
	bool CanSave(const AssFile*) const override { return true; }

	void ReadFile(AssFile *target, agi::fs::path const& filename, agi::vfr::Framerate const& fps, const char *forceEncoding) const override;
	void WriteFile(const AssFile *src, agi::fs::path const& filename, agi::vfr::Framerate const& fps, const char *encoding) const override;

	// Does not write [Aegisub Project Garbage] and [Aegisub Extradata] sections when exporting
	void ExportFile(const AssFile *src, agi::fs::path const& filename, agi::vfr::Framerate const& fps, const char *encoding) const override;
};

/// Serialize a complete production ASS using the normal ASS writer while
/// avoiding background-thread reads from the mutable global option store.
void WriteAssFileForRecovery(const AssFile *src, agi::fs::path const& filename,
	bool save_ui_state, const char *encoding = "UTF-8");
