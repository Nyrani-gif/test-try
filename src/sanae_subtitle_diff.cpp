// Copyright (c) 2026, Aegisub Sanae contributors

#include "sanae_subtitle_diff.h"

#include "sanae_text.h"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace {
void append_gap(SanaeSemanticDiff& result,
	std::vector<SanaeSemanticLine> const& before, std::size_t before_begin, std::size_t before_end,
	std::vector<SanaeSemanticLine> const& after, std::size_t after_begin, std::size_t after_end)
{
	auto paired = std::min(before_end - before_begin, after_end - after_begin);
	for (std::size_t offset = 0; offset < paired; ++offset) {
		auto const& old_line = before[before_begin + offset];
		auto const& new_line = after[after_begin + offset];
		++result.changed;
		result.entries.push_back({SanaeSemanticDiffKind::Changed,
			new_line.start, new_line.end, old_line.text, new_line.text});
	}
	for (std::size_t index = before_begin + paired; index < before_end; ++index) {
		++result.removed;
		result.entries.push_back({SanaeSemanticDiffKind::Removed,
			before[index].start, before[index].end, before[index].text, {}});
	}
	for (std::size_t index = after_begin + paired; index < after_end; ++index) {
		++result.added;
		result.entries.push_back({SanaeSemanticDiffKind::Added,
			after[index].start, after[index].end, {}, after[index].text});
	}
}
}

SanaeSemanticDiff SanaeCompareSemanticSubtitles(
	std::vector<SanaeSemanticLine> const& before,
	std::vector<SanaeSemanticLine> const& after)
{
	SanaeSemanticDiff result;
	std::vector<std::string> old_text, new_text;
	old_text.reserve(before.size());
	new_text.reserve(after.size());
	for (auto const& line : before) old_text.push_back(SanaeNormalizeSource(line.text));
	for (auto const& line : after) new_text.push_back(SanaeNormalizeSource(line.text));

	// Normal episodes are small enough for an exact LCS. Keep a deterministic
	// linear fallback for pathological production files.
	constexpr std::size_t maximum_lcs_cells = 4'000'000;
	std::vector<std::pair<std::size_t, std::size_t>> matches;
	if (!before.empty() && after.size() <= maximum_lcs_cells / before.size()) {
		std::vector<std::size_t> table((before.size() + 1) * (after.size() + 1));
		auto cell = [&](std::size_t i, std::size_t j) -> std::size_t& {
			return table[i * (after.size() + 1) + j];
		};
		for (std::size_t i = 1; i <= before.size(); ++i) {
			for (std::size_t j = 1; j <= after.size(); ++j) {
				if (!old_text[i - 1].empty() && old_text[i - 1] == new_text[j - 1])
					cell(i, j) = cell(i - 1, j - 1) + 1;
				else cell(i, j) = std::max(cell(i - 1, j), cell(i, j - 1));
			}
		}
		for (std::size_t i = before.size(), j = after.size(); i && j; ) {
			if (!old_text[i - 1].empty() && old_text[i - 1] == new_text[j - 1]) {
				matches.emplace_back(--i, --j);
			}
			else if (cell(i - 1, j) >= cell(i, j - 1)) --i;
			else --j;
		}
		std::reverse(matches.begin(), matches.end());
	}
	else {
		// Preserve order and prefer exact timing/text matches without quadratic
		// memory for unusually large ASS files.
		std::size_t cursor = 0;
		for (std::size_t i = 0; i < before.size(); ++i) {
			for (std::size_t j = cursor; j < after.size(); ++j) {
				if (!old_text[i].empty() && old_text[i] == new_text[j]) {
					matches.emplace_back(i, j);
					cursor = j + 1;
					break;
				}
			}
		}
	}

	std::size_t old_cursor = 0, new_cursor = 0;
	for (auto [old_index, new_index] : matches) {
		append_gap(result, before, old_cursor, old_index, after, new_cursor, new_index);
		++result.unchanged;
		old_cursor = old_index + 1;
		new_cursor = new_index + 1;
	}
	append_gap(result, before, old_cursor, before.size(), after, new_cursor, after.size());
	return result;
}
