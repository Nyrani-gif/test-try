// Copyright (c) 2026, Aegisub Sanae contributors

#include "sanae_text.h"

#include <boost/locale/conversion.hpp>
#include <unicode/uchar.h>
#include <unicode/utf8.h>

#include <algorithm>
#include <cctype>
#include <string_view>
#include <limits>
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

bool SanaeSourceFragmentMatch(std::string const& left, std::string const& right) {
	auto left_tokens = repeat_tokens(SanaeNormalizeRepeatSource(left));
	auto right_tokens = repeat_tokens(SanaeNormalizeRepeatSource(right));
	if (left_tokens.empty() || right_tokens.empty()) return false;
	auto const& shorter = left_tokens.size() <= right_tokens.size() ? left_tokens : right_tokens;
	auto const& longer = left_tokens.size() <= right_tokens.size() ? right_tokens : left_tokens;
	if (shorter.size() < 6) return false;

	std::size_t best = 0;
	for (std::size_t i = 0; i < shorter.size(); ++i) {
		for (std::size_t j = 0; j < longer.size(); ++j) {
			std::size_t run = 0;
			while (i + run < shorter.size() && j + run < longer.size()
				&& shorter[i + run] == longer[j + run]) ++run;
			best = std::max(best, run);
		}
	}
	if (best < 6 || best * 4 < shorter.size() * 3) return false;

	std::size_t information = 0;
	for (std::size_t i = 0; i < shorter.size(); ++i) {
		for (unsigned char value : shorter[i])
			if (value >= 0x80 || std::isalnum(value)) ++information;
	}
	return information >= 20;
}

double SanaeSourceSimilarity(std::string const& left, std::string const& right, double minimum) {
	if (left == right) return 1.0;
	if (left.empty() || right.empty()) return 0.0;
	auto maximum = std::max(left.size(), right.size());
	if (static_cast<double>(std::max(left.size(), right.size()) - std::min(left.size(), right.size()))
		> (1.0 - minimum) * maximum) return 0.0;

	std::vector<size_t> previous(right.size() + 1), current(right.size() + 1);
	for (size_t i = 0; i <= right.size(); ++i) previous[i] = i;
	for (size_t i = 1; i <= left.size(); ++i) {
		current[0] = i;
		size_t row_minimum = current[0];
		for (size_t j = 1; j <= right.size(); ++j) {
			current[j] = std::min({previous[j] + 1, current[j - 1] + 1,
				previous[j - 1] + (left[i - 1] == right[j - 1] ? 0 : 1)});
			row_minimum = std::min(row_minimum, current[j]);
		}
		if (minimum > 0.0 && static_cast<double>(row_minimum) > (1.0 - minimum) * maximum)
			return 0.0;
		previous.swap(current);
	}
	return 1.0 - static_cast<double>(previous.back()) / maximum;
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
	if (std::min(query.size(), candidate.size()) < 6) return false;
	return SanaeSourceSimilarity(query, candidate, 0.82) >= 0.82;
}
