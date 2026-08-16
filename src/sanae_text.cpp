// Copyright (c) 2026, Aegisub Sanae contributors

#include "sanae_text.h"

#include <boost/locale/conversion.hpp>
#include <unicode/uchar.h>
#include <unicode/utf8.h>

#include <algorithm>
#include <cctype>
#include <functional>
#include <string_view>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <stdexcept>
#include <vector>

namespace {
void append_utf8(std::string& target, UChar32 value) {
	char buffer[U8_MAX_LENGTH];
	int32_t length = 0;
	U8_APPEND_UNSAFE(buffer, length, value);
	target.append(buffer, static_cast<std::size_t>(length));
}

bool presentation_html_tag(std::string_view raw) {
	while (!raw.empty() && std::isspace(static_cast<unsigned char>(raw.front()))) raw.remove_prefix(1);
	while (!raw.empty() && std::isspace(static_cast<unsigned char>(raw.back()))) raw.remove_suffix(1);
	if (!raw.empty() && raw.front() == '/') {
		raw.remove_prefix(1);
		while (!raw.empty() && std::isspace(static_cast<unsigned char>(raw.front()))) raw.remove_prefix(1);
	}
	if (raw.size() != 1) return false;
	char tag = static_cast<char>(std::tolower(static_cast<unsigned char>(raw.front())));
	return tag == 'i' || tag == 'b' || tag == 'u' || tag == 's';
}

std::vector<std::string> repeat_tokens(std::string const& normalized) {
	std::vector<std::string> result;
	std::string token;
	for (unsigned char value : normalized) {
		if (value >= 0x80 || std::isalnum(value) || value == '\'') {
			token.push_back(static_cast<char>(value));
		}
		else if (!token.empty()) {
			result.push_back(std::move(token));
			token.clear();
		}
	}
	if (!token.empty()) result.push_back(std::move(token));
	return result;
}


std::vector<UChar32> utf8_codepoints(std::string const& text) {
	std::vector<UChar32> result;
	if (text.size() > static_cast<std::size_t>(std::numeric_limits<int32_t>::max()))
		throw std::invalid_argument("Sanae text is too large to compare");
	for (int32_t offset = 0, length = static_cast<int32_t>(text.size()); offset < length; ) {
		UChar32 codepoint = 0;
		U8_NEXT(text.data(), offset, length, codepoint);
		if (codepoint < 0) throw std::invalid_argument("Sanae text is not valid UTF-8");
		result.push_back(codepoint);
	}
	return result;
}

std::size_t token_information(std::string const& token) {
	std::size_t result = 0;
	for (unsigned char value : token)
		if (value >= 0x80 || std::isalnum(value)) ++result;
	return result;
}

bool distinctive_five_token_window(std::vector<std::string> const& tokens, std::size_t start) {
	if (start + 5 > tokens.size()) return false;
	std::size_t information = 0;
	std::size_t long_words = 0;
	for (std::size_t i = start; i < start + 5; ++i) {
		auto info = token_information(tokens[i]);
		information += info;
		if (info >= 5) ++long_words;
	}
	return information >= 24 && long_words >= 2;
}

std::string token_window_key(std::vector<std::string> const& tokens, std::size_t start, std::size_t count) {
	std::string key;
	for (std::size_t i = 0; i < count; ++i) {
		if (!key.empty()) key.push_back('\x1f');
		key += tokens[start + i];
	}
	return key;
}

std::size_t token_lcs(std::vector<std::string> const& left, std::vector<std::string> const& right) {
	std::vector<std::size_t> previous(right.size() + 1), current(right.size() + 1);
	for (std::size_t i = 1; i <= left.size(); ++i) {
		std::fill(current.begin(), current.end(), 0);
		for (std::size_t j = 1; j <= right.size(); ++j)
			current[j] = left[i - 1] == right[j - 1]
				? previous[j - 1] + 1 : std::max(previous[j], current[j - 1]);
		previous.swap(current);
	}
	return previous.back();
}

bool has_negation_token(std::vector<std::string> const& tokens) {
	static std::unordered_set<std::string> const negation = {
		"no", "not", "never", "without", "cannot", "can't", "couldn't", "didn't",
		"doesn't", "don't", "hadn't", "hasn't", "haven't", "isn't", "mustn't",
		"shouldn't", "wasn't", "weren't", "won't", "wouldn't"
	};
	return std::any_of(tokens.begin(), tokens.end(), [&](auto const& token) { return negation.count(token) != 0; });
}

std::unordered_set<std::string> numeric_tokens(std::vector<std::string> const& tokens) {
	std::unordered_set<std::string> result;
	for (auto const& token : tokens) {
		if (!token.empty() && std::all_of(token.begin(), token.end(), [](unsigned char value) {
			return std::isdigit(value) != 0;
		})) result.insert(token);
	}
	return result;
}

std::size_t multiset_token_overlap(std::vector<std::string> const& left, std::vector<std::string> const& right) {
	std::unordered_map<std::string, std::size_t> counts;
	for (auto const& token : left) ++counts[token];
	std::size_t overlap = 0;
	for (auto const& token : right) {
		auto found = counts.find(token);
		if (found == counts.end() || found->second == 0) continue;
		--found->second;
		++overlap;
	}
	return overlap;
}
}

std::string SanaeNormalizeSource(std::string text) {
	// Mirror the server's terminology normalization: Unicode NFKC, whitespace
	// collapse, then Unicode case-folding. ENSUB is normally ASCII,
	// but this keeps Russian memory searches and non-ASCII names predictable.
	text = boost::locale::normalize(text, boost::locale::norm_nfkc);
	std::string result;
	result.reserve(text.size());
	bool space = false;
	if (text.size() > static_cast<size_t>(std::numeric_limits<int32_t>::max()))
		throw std::invalid_argument("Sanae text is too large to normalize");
	for (int32_t offset = 0, length = static_cast<int32_t>(text.size()); offset < length; ) {
		int32_t start = offset;
		UChar32 codepoint = 0;
		U8_NEXT(text.data(), offset, length, codepoint);
		if (codepoint < 0) throw std::invalid_argument("Sanae text is not valid UTF-8");
		if (u_isUWhiteSpace(codepoint)
			|| (codepoint <= 0x7f && std::isspace(static_cast<unsigned char>(codepoint)))) {
			space = !result.empty();
			continue;
		}
		if (space) result.push_back(' ');
		space = false;
		result.append(text, static_cast<size_t>(start), static_cast<size_t>(offset - start));
	}
	return boost::locale::fold_case(result);
}

std::string SanaeNormalizeRepeatSource(std::string text) {
	std::string visible;
	visible.reserve(text.size());
	for (std::size_t i = 0; i < text.size();) {
		if (text[i] == '{') {
			auto end = text.find('}', i + 1);
			if (end != std::string::npos) {
				i = end + 1;
				continue;
			}
		}
		if (text[i] == '<') {
			auto end = text.find('>', i + 1);
			if (end != std::string::npos
				&& presentation_html_tag(std::string_view(text).substr(i + 1, end - i - 1))) {
				i = end + 1;
				continue;
			}
		}
		if (text[i] == '\\' && i + 1 < text.size()
			&& (text[i + 1] == 'N' || text[i + 1] == 'n')) {
			visible.push_back(' ');
			i += 2;
			continue;
		}
		visible.push_back(text[i++]);
	}
	return SanaeNormalizeSource(std::move(visible));
}

double SanaeSourceFragmentScore(std::string const& left, std::string const& right) {
	auto left_tokens = repeat_tokens(SanaeNormalizeRepeatSource(left));
	auto right_tokens = repeat_tokens(SanaeNormalizeRepeatSource(right));
	if (left_tokens.empty() || right_tokens.empty()) return 0.0;
	auto const& shorter = left_tokens.size() <= right_tokens.size() ? left_tokens : right_tokens;
	auto const& longer = left_tokens.size() <= right_tokens.size() ? right_tokens : left_tokens;
	if (shorter.size() < 5) return 0.0;

	double best_score = 0.0;
	for (std::size_t i = 0; i < shorter.size(); ++i) {
		for (std::size_t j = 0; j < longer.size(); ++j) {
			std::size_t run = 0;
			while (i + run < shorter.size() && j + run < longer.size()
				&& shorter[i + run] == longer[j + run]) ++run;
			if (run < 5) continue;
			double coverage = static_cast<double>(run) / shorter.size();
			if (run >= 6) {
				if (coverage < 0.75) continue;
				std::size_t information = 0;
				for (auto const& token : shorter) information += token_information(token);
				if (information < 20) continue;
			}
			else if (coverage < 0.80 || !distinctive_five_token_window(shorter, i)) continue;
			best_score = std::max(best_score, coverage);
		}
	}
	return best_score;
}

bool SanaeSourceFragmentMatch(std::string const& left, std::string const& right) {
	return SanaeSourceFragmentScore(left, right) > 0.0;
}

std::vector<std::string> SanaeRepeatFragmentKeys(std::string const& text) {
	auto tokens = repeat_tokens(SanaeNormalizeRepeatSource(text));
	std::vector<std::string> result;
	if (tokens.size() >= 6) {
		result.reserve((tokens.size() - 5) + (tokens.size() - 4));
		for (std::size_t i = 0; i + 6 <= tokens.size(); ++i)
			result.push_back(token_window_key(tokens, i, 6));
	}
	for (std::size_t i = 0; i + 5 <= tokens.size(); ++i)
		if (distinctive_five_token_window(tokens, i))
			result.push_back(token_window_key(tokens, i, 5));
	return result;
}

double SanaeSourceSimilarity(std::string const& left, std::string const& right, double minimum) {
	if (left == right) return 1.0;
	if (left.empty() || right.empty()) return 0.0;
	auto left_chars = utf8_codepoints(left);
	auto right_chars = utf8_codepoints(right);
	auto maximum = std::max(left_chars.size(), right_chars.size());
	if (maximum == 0) return 1.0;
	if (static_cast<double>(maximum - std::min(left_chars.size(), right_chars.size()))
		> (1.0 - minimum) * maximum) return 0.0;

	std::vector<size_t> previous(right_chars.size() + 1), current(right_chars.size() + 1);
	for (size_t i = 0; i <= right_chars.size(); ++i) previous[i] = i;
	for (size_t i = 1; i <= left_chars.size(); ++i) {
		current[0] = i;
		size_t row_minimum = current[0];
		for (size_t j = 1; j <= right_chars.size(); ++j) {
			current[j] = std::min({previous[j] + 1, current[j - 1] + 1,
				previous[j - 1] + (left_chars[i - 1] == right_chars[j - 1] ? 0 : 1)});
			row_minimum = std::min(row_minimum, current[j]);
		}
		if (minimum > 0.0 && static_cast<double>(row_minimum) > (1.0 - minimum) * maximum)
			return 0.0;
		previous.swap(current);
	}
	return 1.0 - static_cast<double>(previous.back()) / maximum;
}

double SanaeSourcePhraseSimilarity(std::string const& left, std::string const& right, double minimum) {
	auto normalized_left = SanaeNormalizeRepeatSource(left);
	auto normalized_right = SanaeNormalizeRepeatSource(right);
	if (normalized_left == normalized_right) return 1.0;
	if (normalized_left.empty() || normalized_right.empty()) return 0.0;

	double character_score = SanaeSourceSimilarity(normalized_left, normalized_right, 0.0);
	auto left_tokens = repeat_tokens(normalized_left);
	auto right_tokens = repeat_tokens(normalized_right);
	if (left_tokens.size() < 3 || right_tokens.size() < 3)
		return character_score >= minimum ? character_score : 0.0;

	auto shorter = std::min(left_tokens.size(), right_tokens.size());
	auto longer = std::max(left_tokens.size(), right_tokens.size());
	auto lcs = token_lcs(left_tokens, right_tokens);
	auto overlap = multiset_token_overlap(left_tokens, right_tokens);
	double ordered_coverage = static_cast<double>(lcs) / shorter;
	double overlap_coverage = static_cast<double>(overlap) / longer;
	double length_coverage = static_cast<double>(shorter) / longer;
	double phrase_score = 0.55 * ordered_coverage + 0.35 * overlap_coverage + 0.10 * length_coverage;
	double score = std::max(character_score, phrase_score);
	// A polarity flip is too dangerous to call a high-confidence reusable
	// repeat even when the surrounding sentence is almost identical.
	if (has_negation_token(left_tokens) != has_negation_token(right_tokens)) score = std::min(score, 0.90);
	// Numbers often carry the one fact which must not be silently inherited
	// from a previous translation (dates, ages, quantities, episode counts).
	// Keep the line discoverable at lower thresholds, but not as the default
	// high-confidence Similar match when the numeric payload changed.
	if (numeric_tokens(left_tokens) != numeric_tokens(right_tokens)) score = std::min(score, 0.90);
	return score >= minimum ? score : 0.0;
}

std::string SanaeNormalizeSearchText(std::string text) {
	text = SanaeNormalizeSource(std::move(text));
	std::string result;
	result.reserve(text.size());
	bool separator = false;
	for (int32_t offset = 0, length = static_cast<int32_t>(text.size()); offset < length; ) {
		UChar32 codepoint = 0;
		U8_NEXT(text.data(), offset, length, codepoint);
		if (codepoint < 0) throw std::invalid_argument("Sanae text is not valid UTF-8");
		if (codepoint == 0x0451) codepoint = 0x0435; // ё → е for manual search only
		if (u_isalnum(codepoint)) {
			if (separator && !result.empty()) result.push_back(' ');
			separator = false;
			append_utf8(result, codepoint);
		}
		else separator = !result.empty();
	}
	return result;
}

std::vector<std::string> SanaeSearchTokens(std::string const& text) {
	std::vector<std::string> result;
	auto normalized = SanaeNormalizeSearchText(text);
	for (std::size_t start = 0; start < normalized.size();) {
		auto end = normalized.find(' ', start);
		if (end == std::string::npos) end = normalized.size();
		if (end > start) result.emplace_back(normalized.substr(start, end - start));
		start = end + 1;
	}
	return result;
}

std::string SanaeLightRussianStem(std::string token) {
	bool cyrillic = std::any_of(token.begin(), token.end(), [](unsigned char value) {
		return value == 0xd0 || value == 0xd1;
	});
	if (!cyrillic || token.size() < 10) return token;
	static std::string const suffixes[] = {
		"иями", "ями", "ами", "его", "ого", "ему", "ому", "ыми", "ими",
		"ую", "юю", "ая", "яя", "ое", "ее", "ые", "ие", "ый", "ий", "ой",
		"ам", "ям", "ах", "ях", "ом", "ем", "ов", "ев",
		"а", "я", "ы", "и", "е", "о", "у", "ю"
	};
	for (auto const& suffix : suffixes) {
		if (token.size() > suffix.size() + 7 && token.ends_with(suffix)) {
			token.resize(token.size() - suffix.size());
			break;
		}
	}
	return token;
}

bool SanaeSearchTokenMatches(std::string const& query, std::string const& candidate) {
	if (query == candidate) return true;
	auto query_stem = SanaeLightRussianStem(query);
	auto candidate_stem = SanaeLightRussianStem(candidate);
	if (query_stem == candidate_stem) return true;
	if (std::min(utf8_codepoints(query).size(), utf8_codepoints(candidate).size()) < 6) return false;
	return SanaeSourceSimilarity(query, candidate, 0.82) >= 0.82;
}


double SanaeSearchPhraseScore(std::string const& query, std::string const& candidate) {
	auto query_tokens = SanaeSearchTokens(query);
	auto candidate_tokens = SanaeSearchTokens(candidate);
	if (query_tokens.empty() || candidate_tokens.empty() || query_tokens.size() > candidate_tokens.size()) return 0.0;

	// Maximum bipartite matching prevents two query terms from reusing one
	// candidate token (for example "cat cats" matching one lone "cat").
	std::vector<int> assigned(candidate_tokens.size(), -1);
	std::function<bool(std::size_t, std::vector<bool>&)> assign = [&](std::size_t qi, std::vector<bool>& seen) {
		for (std::size_t ci = 0; ci < candidate_tokens.size(); ++ci) {
			if (seen[ci] || !SanaeSearchTokenMatches(query_tokens[qi], candidate_tokens[ci])) continue;
			seen[ci] = true;
			if (assigned[ci] < 0 || assign(static_cast<std::size_t>(assigned[ci]), seen)) {
				assigned[ci] = static_cast<int>(qi);
				return true;
			}
		}
		return false;
	};
	for (std::size_t qi = 0; qi < query_tokens.size(); ++qi) {
		std::vector<bool> seen(candidate_tokens.size());
		if (!assign(qi, seen)) return 0.0;
	}

	std::vector<std::size_t> previous(candidate_tokens.size() + 1), current(candidate_tokens.size() + 1);
	for (std::size_t i = 1; i <= query_tokens.size(); ++i) {
		std::fill(current.begin(), current.end(), 0);
		for (std::size_t j = 1; j <= candidate_tokens.size(); ++j)
			current[j] = SanaeSearchTokenMatches(query_tokens[i - 1], candidate_tokens[j - 1])
				? previous[j - 1] + 1 : std::max(previous[j], current[j - 1]);
		previous.swap(current);
	}
	double ordered = static_cast<double>(previous.back()) / query_tokens.size();

	std::size_t best_span = std::numeric_limits<std::size_t>::max();
	for (std::size_t start = 0; start < candidate_tokens.size(); ++start) {
		if (!SanaeSearchTokenMatches(query_tokens.front(), candidate_tokens[start])) continue;
		std::size_t qi = 1;
		std::size_t ci = start + 1;
		for (; qi < query_tokens.size() && ci < candidate_tokens.size(); ++ci)
			if (SanaeSearchTokenMatches(query_tokens[qi], candidate_tokens[ci])) ++qi;
		if (qi == query_tokens.size()) best_span = std::min(best_span, ci - start);
	}
	double compactness = best_span == std::numeric_limits<std::size_t>::max()
		? 0.0 : std::min(1.0, static_cast<double>(query_tokens.size()) / best_span);
	return 0.70 + 0.20 * ordered + 0.10 * compactness;
}
