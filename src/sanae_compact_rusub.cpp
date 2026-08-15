// Copyright (c) 2026, Aegisub Sanae contributors

#include "sanae_compact_rusub.h"

#include "ass_dialogue.h"
#include "ass_file.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {
bool has_drawing(AssDialogue const& line) {
	auto blocks = line.ParseTags();
	return std::any_of(blocks.begin(), blocks.end(), [](auto const& block) {
		return block->GetType() == AssBlockType::DRAWING;
	});
}

std::string normalized_visible(std::string text) {
	std::string result;
	result.reserve(text.size());
	bool pending_space = false;
	for (size_t i = 0; i < text.size(); ++i) {
		if (text[i] == '\\' && i + 1 < text.size()
			&& (text[i + 1] == 'N' || text[i + 1] == 'n' || text[i + 1] == 'h')) {
			pending_space = !result.empty();
			++i;
			continue;
		}
		unsigned char c = static_cast<unsigned char>(text[i]);
		if (std::isspace(c)) {
			pending_space = !result.empty();
			continue;
		}
		if (pending_space) result.push_back(' ');
		pending_space = false;
		result.push_back(static_cast<char>(c));
	}
	return result;
}

std::string technical_key(AssDialogue const& line, std::string const& visible) {
	std::string key;
	key.reserve(visible.size() + line.Actor.get().size()
		+ line.Effect.get().size() + 64);
	key += normalized_visible(visible);
	key.push_back('\x1f');
	key += line.Actor.get();
	key.push_back('\x1f');
	key += line.Effect.get();
	for (int margin : line.Margin) {
		key.push_back('\x1f');
		key += std::to_string(margin);
	}
	return key;
}

bool timings_form_technical_group(AssDialogue const& left, AssDialogue const& right) {
	int overlap = std::min<int>(left.End, right.End) - std::max<int>(left.Start, right.Start);
	int shorter = std::min<int>(left.End - left.Start, right.End - right.Start);
	if (shorter <= 0) return left.Start == right.Start && left.End == right.End;
	// Typesetting layers normally share essentially the same interval. Requiring
	// 90% overlap prevents sequential repetitions of the same words from being
	// mistaken for a technical layer group.
	return overlap > 0 && static_cast<long long>(overlap) * 10 >= static_cast<long long>(shorter) * 9;
}
}

AssFile BuildSanaeCompactRusub(AssFile const& source, SanaeCompactStats *out_stats) {
	SanaeCompactStats stats;
	stats.input_events = source.Events.size();

	AssFile compact(source);
	compact.ClearAttachments();
	compact.Extradata.clear();
	compact.next_extradata_id = 0;
	compact.Properties = {};
	compact.Events.clear_and_dispose([](AssDialogue *line) { delete line; });

	std::unordered_map<std::string, AssDialogue *> last_group;
	last_group.reserve(source.Events.size());
	for (auto const& input : source.Events) {
		if (input.Comment) {
			++stats.comments_removed;
			continue;
		}
		if (has_drawing(input)) {
			++stats.drawings_removed;
			continue;
		}

		std::string visible = input.GetStrippedText();
		if (normalized_visible(visible).empty()) {
			++stats.empty_events_removed;
			continue;
		}

		auto key = technical_key(input, visible);
		auto previous = last_group.find(key);
		// A repeated caption is considered a technical layer only while its
		// timing intersects the previous layer. The same words later in the
		// episode remain a distinct semantic subtitle.
		if (previous != last_group.end() && timings_form_technical_group(input, *previous->second)) {
			previous->second->Start = std::min(previous->second->Start, input.Start);
			previous->second->End = std::max(previous->second->End, input.End);
			++stats.technical_duplicates_collapsed;
			continue;
		}

		auto line = new AssDialogue(input);
		line->Text = std::move(visible);
		line->ExtradataIds = std::vector<uint32_t>{};
		compact.Events.push_back(*line);
		last_group[std::move(key)] = line;
	}

	int row = 0;
	for (auto& line : compact.Events) line.Row = row++;
	stats.output_events = compact.Events.size();
	if (out_stats) *out_stats = stats;
	return compact;
}
