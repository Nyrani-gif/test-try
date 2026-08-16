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

/// Conservative flashback/quotation fragment score. Returns 0 when no useful
/// continuous fragment exists; otherwise returns the coverage of the shorter
/// source in the inclusive range (0, 1]. Six-token runs use the normal rule;
/// distinctive five-token runs are accepted only under stricter information
/// requirements.
double SanaeSourceFragmentScore(std::string const& left, std::string const& right);
bool SanaeSourceFragmentMatch(std::string const& left, std::string const& right);

/// Candidate keys used by the project-memory fragment index. Keeping this in
/// the text layer ensures that the index and final verifier use one policy.
std::vector<std::string> SanaeRepeatFragmentKeys(std::string const& text);

/// Normalized Levenshtein similarity in the inclusive range [0, 1].
double SanaeSourceSimilarity(std::string const& left, std::string const& right,
	double minimum = 0.0);

/// Repeat-oriented phrase similarity. This combines character edit distance
/// with token overlap and token order, so small insertions or local word moves
/// can still match without treating arbitrary bags of words as equivalent.
double SanaeSourcePhraseSimilarity(std::string const& left, std::string const& right,
	double minimum = 0.0);

/// Search normalization is intentionally broader than source-repeat
/// normalization: punctuation and ASS-visible separators become spaces and
/// Russian ё/е compare equal. Source Repeat never uses these helpers.
std::string SanaeNormalizeSearchText(std::string text);
std::vector<std::string> SanaeSearchTokens(std::string const& text);
std::string SanaeLightRussianStem(std::string token);
bool SanaeSearchTokenMatches(std::string const& query, std::string const& candidate);

/// Score a manual fuzzy project-search hit. Every query token must map to a
/// distinct candidate token. Ordered hits rank above scattered hits while the
/// old "all query words are present" behaviour remains available.
double SanaeSearchPhraseScore(std::string const& query, std::string const& candidate);
