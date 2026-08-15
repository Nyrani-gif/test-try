// Copyright (c) 2026, Aegisub Sanae contributors

#pragma once

#include <cstddef>
#include <string>
#include <vector>

struct SanaeSemanticLine {
	int start = 0;
	int end = 0;
	std::string text;
};

enum class SanaeSemanticDiffKind { Changed, Added, Removed };

struct SanaeSemanticDiffEntry {
	SanaeSemanticDiffKind kind = SanaeSemanticDiffKind::Changed;
	int start = 0;
	int end = 0;
	std::string before;
	std::string after;
};

struct SanaeSemanticDiff {
	std::size_t unchanged = 0;
	std::size_t changed = 0;
	std::size_t added = 0;
	std::size_t removed = 0;
	std::vector<SanaeSemanticDiffEntry> entries;
};

/// Compare subtitle meaning rather than ASS serialization. The caller supplies
/// visible Event text (without override tags); whitespace/case normalization is
/// applied here. Equal lines are aligned with an LCS so insertions do not turn
/// every following line into a false change.
SanaeSemanticDiff SanaeCompareSemanticSubtitles(
	std::vector<SanaeSemanticLine> const& before,
	std::vector<SanaeSemanticLine> const& after);
