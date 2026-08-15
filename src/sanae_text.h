// Copyright (c) 2026, Aegisub Sanae contributors

#pragma once

#include <string>
#include <vector>

/// Normalize project-memory text using the server's NFKC/case-fold convention
/// followed by whitespace collapse.
std::string SanaeNormalizeSource(std::string text);

/// Canonical visible ENSUB text used only by Source Repeat matching.
/// Presentation-only ASS override blocks and common HTML-style subtitle tags
/// are ignored, explicit ASS line breaks become spaces, then normal project
/// normalization (NFKC/whitespace/case-folding) is applied.
std::string SanaeNormalizeRepeatSource(std::string text);

/// Conservative flashback/quotation fragment test. Returns true only when the
/// two source strings share a long continuous token run which covers most of
/// the shorter source. This deliberately rejects generic fragments such as
/// "I think".
bool SanaeSourceFragmentMatch(std::string const& left, std::string const& right);

/// Normalized Levenshtein similarity in the inclusive range [0, 1].
double SanaeSourceSimilarity(std::string const& left, std::string const& right,
	double minimum = 0.0);

/// Search normalization is intentionally broader than source-repeat
/// normalization: punctuation and ASS-visible separators become spaces and
/// Russian ё/е compare equal. Source Repeat never uses these helpers.
std::string SanaeNormalizeSearchText(std::string text);
std::vector<std::string> SanaeSearchTokens(std::string const& text);
std::string SanaeLightRussianStem(std::string token);
bool SanaeSearchTokenMatches(std::string const& query, std::string const& candidate);
