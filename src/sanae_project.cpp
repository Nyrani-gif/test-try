// Copyright (c) 2026, Aegisub Sanae contributors

#include "sanae_project.h"

#include "ass_dialogue.h"
#include "ass_file.h"
#include "charset_detect.h"
#include "compat.h"
#include "format.h"
#include "frame_main.h"
#include "include/aegisub/context.h"
#include "include/aegisub/spellchecker.h"
#include "options.h"
#include "project.h"
#include "sanae_api.h"
#include "subs_controller.h"
#include "subtitle_format.h"
#include "subtitle_format_ass.h"
#include "translation_project.h"

#include <libaegisub/cajun/elements.h>
#include <libaegisub/cajun/reader.h>
#include <libaegisub/cajun/writer.h>
#include <libaegisub/ass/time.h>
#include <libaegisub/dispatch.h>
#include <libaegisub/fs.h>
#include <libaegisub/io.h>
#include <libaegisub/path.h>
#include <libaegisub/spellchecker.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cctype>
#include <cstdint>
#include <functional>
#include <iomanip>
#include <limits>
#include <map>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <unordered_set>
#include <utility>

#include <wx/timer.h>
#include <wx/msgdlg.h>

#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>

// windows.h maps CreateDirectory to CreateDirectoryW. Keep that macro from
// rewriting the qualified agi::fs::CreateDirectory calls below.
#undef CreateDirectory
#endif

namespace {
class RecoveryResponseError final : public std::runtime_error {
public:
	using std::runtime_error::runtime_error;
};

template<typename T>
T get(json::Object const& object, char const *key, T fallback = {}) {
	auto it = object.find(key);
	if (it == object.end()) return fallback;
	try {
		if constexpr (std::is_integral_v<T> && !std::is_same_v<T, bool>) {
			auto value = static_cast<json::Integer const&>(it->second);
			if constexpr (std::is_signed_v<T>) {
				if constexpr (sizeof(T) < sizeof(json::Integer))
					if (value < static_cast<json::Integer>(std::numeric_limits<T>::min())
						|| value > static_cast<json::Integer>(std::numeric_limits<T>::max())) return fallback;
			}
			else {
				if (value < 0 || static_cast<std::make_unsigned_t<json::Integer>>(value)
					> static_cast<std::make_unsigned_t<json::Integer>>(std::numeric_limits<T>::max())) return fallback;
			}
			return static_cast<T>(value);
		}
		else return static_cast<T>(it->second);
	}
	catch (json::Exception const&) { return fallback; }
}

json::Object const *object_at(json::Object const& object, char const *key) {
	auto it = object.find(key);
	if (it == object.end()) return nullptr;
	try { return &static_cast<json::Object const&>(it->second); }
	catch (json::Exception const&) { return nullptr; }
}

json::Array const *array_at(json::Object const& object, char const *key) {
	auto it = object.find(key);
	if (it == object.end()) return nullptr;
	try { return &static_cast<json::Array const&>(it->second); }
	catch (json::Exception const&) { return nullptr; }
}

json::Object parse_json_object(std::string const& text) {
	std::istringstream input(text);
	json::UnknownElement root;
	json::Reader::Read(root, input);
	return std::move(static_cast<json::Object&>(root));
}

template<typename T>
std::string write_json(T const& value) {
	std::ostringstream output;
	agi::JsonWriter::Write(value, output);
	return output.str();
}

std::string read_file(agi::fs::path const& path) {
	auto stream = agi::io::Open(path, true);
	std::ostringstream data;
	data << stream->rdbuf();
	return data.str();
}

std::string build_recovery_ass(AssFile const& ass, agi::fs::path const& temporary_path,
	bool save_ui_state) {
	try {
		agi::fs::CreateDirectory(temporary_path.parent_path());
		WriteAssFileForRecovery(&ass, temporary_path, save_ui_state, "UTF-8");
		auto bytes = read_file(temporary_path);
		agi::fs::Remove(temporary_path);
		return bytes;
	}
	catch (...) {
		try { agi::fs::Remove(temporary_path); }
		catch (...) { }
		throw;
	}
}

void write_file(agi::fs::path const& path, std::string const& data) {
	agi::fs::CreateDirectory(path.parent_path());
	agi::io::Save output(path, true);
	output.Get().write(data.data(), static_cast<std::streamsize>(data.size()));
}

std::string new_uuid() {
	std::array<unsigned char, 16> bytes{};
	std::random_device random;
	for (auto& byte : bytes) byte = static_cast<unsigned char>(random());
	bytes[6] = static_cast<unsigned char>((bytes[6] & 0x0f) | 0x40);
	bytes[8] = static_cast<unsigned char>((bytes[8] & 0x3f) | 0x80);
	std::ostringstream output;
	output << std::hex << std::setfill('0');
	for (size_t i = 0; i < bytes.size(); ++i) {
		if (i == 4 || i == 6 || i == 8 || i == 10) output << '-';
		output << std::setw(2) << static_cast<int>(bytes[i]);
	}
	return output.str();
}

std::string sha256_hex(std::string const& data) {
#ifdef _WIN32
	BCRYPT_ALG_HANDLE algorithm = nullptr;
	BCRYPT_HASH_HANDLE hash = nullptr;
	auto close = [&] {
		if (hash) BCryptDestroyHash(hash);
		if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
	};
	if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
		throw std::runtime_error("Could not initialize SHA-256");
	DWORD object_size = 0, hash_size = 0, bytes = 0;
	if (BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
		reinterpret_cast<PUCHAR>(&object_size), sizeof(object_size), &bytes, 0) < 0
		|| BCryptGetProperty(algorithm, BCRYPT_HASH_LENGTH,
			reinterpret_cast<PUCHAR>(&hash_size), sizeof(hash_size), &bytes, 0) < 0) {
		close();
		throw std::runtime_error("Could not query SHA-256 properties");
	}
	std::vector<unsigned char> object(object_size), digest(hash_size);
	if (BCryptCreateHash(algorithm, &hash, object.data(), object_size, nullptr, 0, 0) < 0
		|| BCryptHashData(hash, reinterpret_cast<PUCHAR>(const_cast<char *>(data.data())),
			static_cast<ULONG>(data.size()), 0) < 0
		|| BCryptFinishHash(hash, digest.data(), hash_size, 0) < 0) {
		close();
		throw std::runtime_error("Could not calculate SHA-256");
	}
	close();
	std::ostringstream output;
	output << std::hex << std::setfill('0');
	for (auto byte : digest) output << std::setw(2) << static_cast<int>(byte);
	return output.str();
#else
	(void)data;
	return {};
#endif
}

SanaeApiClient make_api() {
	return SanaeApiClient(OPT_GET("Sanae/Server/Base URL")->GetString());
}

SanaeSeasonInfo parse_season(json::Object const& value) {
	return {get<std::string>(value, "id"), get<int>(value, "year"),
		get<std::string>(value, "code"), get<std::string>(value, "display_name"),
		get<double>(value, "sort_order", 0.0)};
}

SanaeDeviceInfo parse_device(json::Object const& value) {
	return {get<std::string>(value, "id"), get<std::string>(value, "display_name"),
		get<std::string>(value, "device_name")};
}

SanaeRecoverySnapshotInfo parse_recovery_snapshot(json::Object const& value) {
	SanaeRecoverySnapshotInfo result;
	result.id = get<std::string>(value, "id");
	result.project_id = get<std::string>(value, "project_id");
	result.episode_id = get<std::string>(value, "episode_id");
	result.source_file_id = get<std::string>(value, "source_file_id");
	result.created_by_device_id = get<std::string>(value, "created_by_device_id");
	result.sha256 = get<std::string>(value, "sha256");
	auto size_bytes = get<std::int64_t>(value, "size_bytes", 0);
	result.size_bytes = size_bytes < 0 ? 0 : static_cast<std::size_t>(size_bytes);
	result.created_at = get<std::string>(value, "created_at");
	if (auto device = object_at(value, "device")) {
		result.device_id = get<std::string>(*device, "id");
		result.device_display_name = get<std::string>(*device, "display_name");
		result.device_name = get<std::string>(*device, "device_name");
	}
	return result;
}

SanaeProjectInfo parse_project(json::Object const& value) {
	SanaeProjectInfo result;
	result.id = get<std::string>(value, "id");
	result.season_id = get<std::string>(value, "season_id");
	result.slug = get<std::string>(value, "slug");
	result.name = get<std::string>(value, "name");
	result.status = get<std::string>(value, "status");
	result.current_revision = get<int>(value, "current_revision", 0);
	return result;
}

SanaeEpisodeInfo parse_episode(json::Object const& value) {
	SanaeEpisodeInfo result;
	result.id = get<std::string>(value, "id");
	result.project_id = get<std::string>(value, "project_id");
	result.episode_code = get<std::string>(value, "episode_code");
	result.sort_order = get<double>(value, "sort_order", 0.0);
	result.status = get<std::string>(value, "status");
	result.current_source_file_id = get<std::string>(value, "current_source_file_id");
	result.current_finalized_revision_id = get<std::string>(value, "current_finalized_revision_id");
	result.created_at = get<std::string>(value, "created_at");
	result.finalized_at = get<std::string>(value, "finalized_at");
	result.deleted_at = get<std::string>(value, "deleted_at");
	return result;
}

SanaeEpisodeFileInfo parse_file(json::Object const& value) {
	SanaeEpisodeFileInfo result;
	result.id = get<std::string>(value, "id");
	result.project_id = get<std::string>(value, "project_id");
	result.episode_id = get<std::string>(value, "episode_id");
	result.kind = get<std::string>(value, "kind");
	result.revision_number = get<int>(value, "revision_number", 0);
	result.sha256 = get<std::string>(value, "sha256");
	result.size_bytes = static_cast<std::size_t>(get<std::int64_t>(value, "size_bytes", 0));
	result.created_at = get<std::string>(value, "created_at");
	return result;
}

SanaeFinalizedRevisionInfo parse_finalized(json::Object const& value) {
	SanaeFinalizedRevisionInfo result;
	result.id = get<std::string>(value, "id");
	result.project_id = get<std::string>(value, "project_id");
	result.episode_id = get<std::string>(value, "episode_id");
	result.revision_number = get<int>(value, "revision_number", 0);
	result.source_file_id = get<std::string>(value, "source_file_id");
	result.compact_rusub_file_id = get<std::string>(value, "compact_rusub_file_id");
	result.project_revision = get<int>(value, "project_revision", 0);
	result.created_at = get<std::string>(value, "created_at");
	return result;
}

SanaeTerminologyEntry parse_term(json::Object const& value) {
	SanaeTerminologyEntry result;
	result.id = get<std::string>(value, "id");
	result.english = get<std::string>(value, "english");
	result.english_normalized = get<std::string>(value, "english_normalized");
	result.russian = get<std::string>(value, "russian");
	result.note = get<std::string>(value, "note");
	result.version = get<int>(value, "version", 0);
	result.deleted = !get<std::string>(value, "deleted_at").empty();
	return result;
}

SanaeTerminologyHistoryEntry parse_term_history(json::Object const& value) {
	SanaeTerminologyHistoryEntry result;
	result.id = get<std::string>(value, "id");
	result.term_id = get<std::string>(value, "term_id");
	result.term_version = get<int>(value, "term_version", 0);
	result.english = get<std::string>(value, "english");
	result.russian = get<std::string>(value, "russian");
	result.note = get<std::string>(value, "note");
	result.episode_id = get<std::string>(value, "episode_id");
	result.changed_at = get<std::string>(value, "changed_at");
	result.project_revision = get<int>(value, "project_revision", 0);
	result.change_type = get<std::string>(value, "change_type");
	return result;
}

SanaeIgnoredCandidate parse_ignore(json::Object const& value) {
	SanaeIgnoredCandidate result;
	result.id = get<std::string>(value, "id");
	result.episode_id = get<std::string>(value, "episode_id");
	result.scope = get<std::string>(value, "scope");
	result.text = get<std::string>(value, "text");
	result.normalized_text = get<std::string>(value, "normalized_text");
	result.language = get<std::string>(value, "language");
	result.deleted = !get<std::string>(value, "deleted_at").empty();
	return result;
}

template<typename T, typename Parse>
std::vector<T> parse_array(json::Object const& root, char const *key, Parse parse) {
	std::vector<T> result;
	auto values = array_at(root, key);
	if (!values) return result;
	result.reserve(values->size());
	for (auto const& value : *values)
		result.push_back(parse(static_cast<json::Object const&>(value)));
	return result;
}

template<typename T>
void merge_by_id(std::vector<T>& target, std::vector<T> update) {
	std::unordered_map<std::string, size_t> index;
	index.reserve(target.size());
	for (size_t i = 0; i < target.size(); ++i) index.emplace(target[i].id, i);
	for (auto& item : update) {
		auto existing = index.find(item.id);
		if (existing == index.end()) {
			index.emplace(item.id, target.size());
			target.push_back(std::move(item));
		}
		else target[existing->second] = std::move(item);
	}
}

json::Object serialize_project(SanaeProjectInfo const& value) {
	json::Object out;
	out["id"] = value.id; out["season_id"] = value.season_id; out["slug"] = value.slug;
	out["name"] = value.name; out["status"] = value.status;
	out["current_revision"] = value.current_revision;
	return out;
}

json::Object serialize_season(SanaeSeasonInfo const& value) {
	json::Object out;
	out["id"] = value.id; out["year"] = value.year; out["code"] = value.code;
	out["display_name"] = value.display_name; out["sort_order"] = value.sort_order;
	return out;
}

json::Object serialize_episode(SanaeEpisodeInfo const& value) {
	json::Object out;
	out["id"] = value.id; out["project_id"] = value.project_id;
	out["episode_code"] = value.episode_code; out["sort_order"] = value.sort_order;
	out["status"] = value.status; out["current_source_file_id"] = value.current_source_file_id;
	out["current_finalized_revision_id"] = value.current_finalized_revision_id;
	out["created_at"] = value.created_at; out["finalized_at"] = value.finalized_at;
	out["deleted_at"] = value.deleted_at;
	return out;
}

json::Object serialize_file(SanaeEpisodeFileInfo const& value) {
	json::Object out;
	out["id"] = value.id; out["project_id"] = value.project_id; out["episode_id"] = value.episode_id;
	out["kind"] = value.kind; out["revision_number"] = value.revision_number;
	out["sha256"] = value.sha256; out["size_bytes"] = static_cast<std::int64_t>(value.size_bytes);
	out["created_at"] = value.created_at;
	return out;
}

json::Object serialize_finalized(SanaeFinalizedRevisionInfo const& value) {
	json::Object out;
	out["id"] = value.id; out["project_id"] = value.project_id; out["episode_id"] = value.episode_id;
	out["revision_number"] = value.revision_number; out["source_file_id"] = value.source_file_id;
	out["compact_rusub_file_id"] = value.compact_rusub_file_id;
	out["project_revision"] = value.project_revision;
	out["created_at"] = value.created_at;
	return out;
}

json::Object serialize_term(SanaeTerminologyEntry const& value) {
	json::Object out;
	out["id"] = value.id; out["english"] = value.english;
	out["english_normalized"] = value.english_normalized; out["russian"] = value.russian;
	out["note"] = value.note; out["version"] = value.version;
	out["deleted"] = value.deleted;
	return out;
}

json::Object serialize_term_history(SanaeTerminologyHistoryEntry const& value) {
	json::Object out;
	out["id"] = value.id; out["term_id"] = value.term_id;
	out["term_version"] = value.term_version; out["english"] = value.english;
	out["russian"] = value.russian; out["note"] = value.note;
	out["episode_id"] = value.episode_id; out["changed_at"] = value.changed_at;
	out["project_revision"] = value.project_revision; out["change_type"] = value.change_type;
	return out;
}

json::Object serialize_ignore(SanaeIgnoredCandidate const& value) {
	json::Object out;
	out["id"] = value.id; out["episode_id"] = value.episode_id; out["scope"] = value.scope;
	out["text"] = value.text; out["normalized_text"] = value.normalized_text;
	out["language"] = value.language; out["deleted"] = value.deleted;
	return out;
}

template<typename T, typename Serialize>
json::Array serialize_array(std::vector<T> const& values, Serialize serialize) {
	json::Array out;
	out.reserve(values.size());
	for (auto const& value : values) out.emplace_back(serialize(value));
	return out;
}

bool has_drawing(AssDialogue const& line) {
	auto blocks = line.ParseTags();
	return std::any_of(blocks.begin(), blocks.end(), [](auto const& block) {
		return block->GetType() == AssBlockType::DRAWING;
	});
}

std::string visible_text(AssDialogue const& line) {
	auto text = line.GetStrippedText();
	for (size_t pos = 0; (pos = text.find("\\N", pos)) != std::string::npos; ) {
		text.replace(pos, 2, " ");
		++pos;
	}
	return text;
}

std::unique_ptr<AssFile> load_ass(agi::Context *context, agi::fs::path const& path) {
	auto loaded = std::make_unique<AssFile>();
	auto encoding = CharSetDetect::GetEncoding(path);
	SubtitleFormat::GetReader(path, encoding.c_str())->ReadFile(loaded.get(), path,
		context->project->Timecodes(), encoding.c_str());
	return loaded;
}

std::vector<SanaeSemanticLine> semantic_lines(AssFile const& file) {
	std::vector<SanaeSemanticLine> result;
	result.reserve(file.Events.size());
	for (auto const& line : file.Events) {
		if (line.Comment || has_drawing(line)) continue;
		auto text = visible_text(line);
		if (SanaeNormalizeSource(text).empty()) continue;
		result.push_back({static_cast<int>(line.Start), static_cast<int>(line.End), std::move(text)});
	}
	return result;
}

std::uint64_t timing_key(int start, int end) {
	return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(start)) << 32)
		| static_cast<std::uint32_t>(end);
}

std::string lower_ascii(std::string value) {
	std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
		return static_cast<char>(std::tolower(c));
	});
	return value;
}

bool valid_utf8(std::string_view text) {
	for (std::size_t i = 0; i < text.size();) {
		auto lead = static_cast<unsigned char>(text[i]);
		if (lead <= 0x7f) { ++i; continue; }
		std::size_t count = 0;
		std::uint32_t codepoint = 0;
		if (lead >= 0xc2 && lead <= 0xdf) { count = 1; codepoint = lead & 0x1f; }
		else if (lead >= 0xe0 && lead <= 0xef) { count = 2; codepoint = lead & 0x0f; }
		else if (lead >= 0xf0 && lead <= 0xf4) { count = 3; codepoint = lead & 0x07; }
		else return false;
		if (i + count >= text.size()) return false;
		for (std::size_t offset = 1; offset <= count; ++offset) {
			auto continuation = static_cast<unsigned char>(text[i + offset]);
			if ((continuation & 0xc0) != 0x80) return false;
			codepoint = (codepoint << 6) | (continuation & 0x3f);
		}
		if ((count == 2 && codepoint < 0x800)
			|| (count == 3 && codepoint < 0x10000)
			|| (codepoint >= 0xd800 && codepoint <= 0xdfff)
			|| codepoint > 0x10ffff)
			return false;
		i += count + 1;
	}
	return true;
}

bool contains_normalized(std::string const& text, std::string const& needle) {
	return SanaeNormalizeSource(text).find(SanaeNormalizeSource(needle)) != std::string::npos;
}

bool informative_for_similar(std::string const& normalized) {
	int information = 0;
	int words = 0;
	bool in_word = false;
	for (unsigned char value : normalized) {
		bool word = value >= 0x80 || std::isalnum(value);
		if (word) {
			++information;
			if (!in_word) ++words;
		}
		in_word = word;
	}
	return information >= 10 || (information >= 8 && words >= 3);
}

std::vector<std::string> repeat_fragment_keys(std::string const& normalized) {
	auto tokens = SanaeSearchTokens(normalized);
	std::vector<std::string> result;
	if (tokens.size() < 6) return result;
	result.reserve(tokens.size() - 5);
	for (std::size_t i = 0; i + 6 <= tokens.size(); ++i) {
		std::string key;
		for (std::size_t j = 0; j < 6; ++j) {
			if (!key.empty()) key.push_back('\x1f');
			key += tokens[i + j];
		}
		result.push_back(std::move(key));
	}
	return result;
}

bool informative_for_internal_consistency(std::string const& normalized) {
	static std::unordered_set<std::string> const trivial = {
		"yes", "no", "yeah", "yep", "nope", "what", "why", "hey",
		"thanks", "thank you", "okay", "ok", "sure", "right", "huh", "hmm",
		"hm", "uh", "um", "hello", "hi", "bye", "sorry", "really", "fine"
	};
	if (trivial.count(SanaeNormalizeSearchText(normalized))) return false;
	auto tokens = SanaeSearchTokens(normalized);
	std::size_t information = 0;
	for (auto const& token : tokens)
		for (unsigned char value : token)
			if (value >= 0x80 || std::isalnum(value)) ++information;
	return tokens.size() >= 4 || information >= 18;
}

std::vector<std::string> english_words(std::string const& text) {
	std::vector<std::string> result;
	std::string word;
	for (unsigned char c : text) {
		if (std::isalpha(c) || ((c == '\'' || c == '-') && !word.empty()))
			word.push_back(static_cast<char>(c));
		else if (!word.empty()) {
			if (word.size() > 1) result.push_back(std::move(word));
			word.clear();
		}
	}
	if (word.size() > 1) result.push_back(std::move(word));
	return result;
}

struct SanaeUnicodeWord {
	std::vector<std::uint32_t> letters;
	std::string display;
};

void append_utf8(std::string& target, std::uint32_t value) {
	if (value <= 0x7f) target.push_back(static_cast<char>(value));
	else if (value <= 0x7ff) {
		target.push_back(static_cast<char>(0xc0 | (value >> 6)));
		target.push_back(static_cast<char>(0x80 | (value & 0x3f)));
	}
	else if (value <= 0xffff) {
		target.push_back(static_cast<char>(0xe0 | (value >> 12)));
		target.push_back(static_cast<char>(0x80 | ((value >> 6) & 0x3f)));
		target.push_back(static_cast<char>(0x80 | (value & 0x3f)));
	}
	else {
		target.push_back(static_cast<char>(0xf0 | (value >> 18)));
		target.push_back(static_cast<char>(0x80 | ((value >> 12) & 0x3f)));
		target.push_back(static_cast<char>(0x80 | ((value >> 6) & 0x3f)));
		target.push_back(static_cast<char>(0x80 | (value & 0x3f)));
	}
}

std::vector<SanaeUnicodeWord> russian_words(std::string const& text) {
	std::vector<SanaeUnicodeWord> result;
	std::vector<std::uint32_t> word;
	auto flush = [&] {
		if (word.size() >= 6 && word.size() <= 32) {
			std::string display;
			for (auto value : word) append_utf8(display, value);
			result.push_back({word, std::move(display)});
		}
		word.clear();
	};
	for (size_t i = 0; i < text.size();) {
		auto lead = static_cast<unsigned char>(text[i]);
		std::uint32_t value = 0;
		size_t length = 1;
		if (lead <= 0x7f) value = lead;
		else if ((lead & 0xe0) == 0xc0 && i + 1 < text.size()) {
			value = lead & 0x1f; length = 2;
		}
		else if ((lead & 0xf0) == 0xe0 && i + 2 < text.size()) {
			value = lead & 0x0f; length = 3;
		}
		else if ((lead & 0xf8) == 0xf0 && i + 3 < text.size()) {
			value = lead & 0x07; length = 4;
		}
		else { flush(); ++i; continue; }
		bool valid = true;
		for (size_t offset = 1; offset < length; ++offset) {
			auto continuation = static_cast<unsigned char>(text[i + offset]);
			if ((continuation & 0xc0) != 0x80) { valid = false; break; }
			value = (value << 6) | (continuation & 0x3f);
		}
		i += valid ? length : 1;
		if (!valid) { flush(); continue; }
		bool cyrillic = (value >= 0x0400 && value <= 0x052f);
		if (!cyrillic) { flush(); continue; }
		if (value >= 0x0410 && value <= 0x042f) value += 0x20;
		else if (value == 0x0401) value = 0x0451;
		word.push_back(value);
	}
	flush();
	return result;
}

bool edit_distance_one(std::vector<std::uint32_t> const& left,
	std::vector<std::uint32_t> const& right)
{
	if (left.empty() || right.empty() || left.front() != right.front()
		|| left.back() != right.back()) return false;
	if (left.size() == right.size()) {
		int differences = 0;
		for (size_t i = 0; i < left.size(); ++i)
			if (left[i] != right[i] && ++differences > 1) return false;
		return differences == 1;
	}
	auto const& shorter = left.size() < right.size() ? left : right;
	auto const& longer = left.size() < right.size() ? right : left;
	if (longer.size() != shorter.size() + 1) return false;
	size_t i = 0, j = 0;
	bool skipped = false;
	while (i < shorter.size() && j < longer.size()) {
		if (shorter[i] == longer[j]) { ++i; ++j; continue; }
		if (skipped) return false;
		skipped = true;
		++j;
	}
	return skipped;
}
}

SanaeProjectManager::SanaeProjectManager(agi::Context *c)
: context(c)
, recovery_alive(std::make_shared<std::atomic<bool>>(true))
{
	LoadDirectoryCache();
	BindConnection(context->subsController->AddFileOpenListener(&SanaeProjectManager::OnSubtitleOpened, this));
	BindConnection(context->ass->AddCommitListener(&SanaeProjectManager::OnAssCommit, this));
	BindConnection(context->translationProject->AddChangeListener(
		[this](TranslationProjectChange, AssDialogue const *line) { OnTranslationProjectChanged(line); }));
	BindConnection(OPT_SUB("Sanae/Project/Source Repeat/Similar Threshold",
		[this](agi::OptionValue const&) { RebuildRepeatCache(); }));
	BindConnection(OPT_SUB("Sanae/Project/Source Repeat/Enabled",
		[this](agi::OptionValue const&) { RebuildRepeatCache(); }));
	BindConnection(OPT_SUB("Sanae/Project/Recovery/Enabled",
		[this](agi::OptionValue const&) { ConfigureRecoveryTimer(); }));
	BindConnection(OPT_SUB("Sanae/Project/Recovery/Check Interval Minutes",
		[this](agi::OptionValue const&) { ConfigureRecoveryTimer(); }));
	recovery_timer.Bind(wxEVT_TIMER, [this](wxTimerEvent&) { CheckRecovery(false); });
	ConfigureRecoveryTimer();
}

SanaeProjectManager::~SanaeProjectManager() {
	*recovery_alive = false;
	recovery_timer.Stop();
	try { SaveDrafts(); }
	catch (...) { }
}

void SanaeProjectManager::ConfigureRecoveryTimer() {
	int minutes = static_cast<int>(std::clamp(
		OPT_GET("Sanae/Project/Recovery/Check Interval Minutes")->GetInt(),
		int64_t{5},
		int64_t{15}));
	if (OPT_GET("Sanae/Project/Recovery/Enabled")->GetBool())
		recovery_timer.Start(minutes * 60 * 1000);
	else recovery_timer.Stop();
}

void SanaeProjectManager::ShowRecoveryStatus(wxString const& text) const {
	if (context && context->frame) context->frame->StatusTimeout(text, 10000);
}

void SanaeProjectManager::ResetRecoveryBinding() {
	SanaeRecoveryBinding binding;
	if (context->translationProject->HasSanaeBinding()) {
		auto const& value = context->translationProject->GetSanaeBinding();
		binding = {value.episode_id, value.source_file_id};
	}
	recovery_state.Bind(binding);
	recovery_baseline_binding_key.clear();
	recovery_baseline_loading = false;
	recovery_manual_after_baseline = false;
	recovery_too_large_warned = false;
	if (binding.IsValid() && IsEnrolled()) RefreshRecoveryBaselineAsync();
}

void SanaeProjectManager::RefreshRecoveryBaselineAsync() {
	auto binding = recovery_state.Binding();
	if (!binding.IsValid() || !IsEnrolled() || recovery_baseline_loading) return;
	recovery_baseline_loading = true;
	auto alive = recovery_alive;
	auto base_url = ServerBaseUrl();
	agi::dispatch::Background().Async([this, alive, base_url, binding] {
		std::vector<SanaeRecoverySnapshotInfo> snapshots;
		std::string device_id;
		std::string error;
		try {
			SanaeApiClient api(base_url);
			auto me = parse_json_object(api.Get("/api/v1/me").body);
			if (auto device = object_at(me, "device")) device_id = get<std::string>(*device, "id");
			auto root = parse_json_object(api.Get("/api/v1/episodes/" + binding.episode_id
				+ "/recovery-snapshots").body);
			snapshots = parse_array<SanaeRecoverySnapshotInfo>(
				root, "snapshots", parse_recovery_snapshot);
		}
		catch (std::exception const& e) { error = e.what(); }
		if (!*alive) return;
		agi::dispatch::Main().Async([this, alive, binding, snapshots = std::move(snapshots),
			device_id = std::move(device_id), error = std::move(error)]() mutable {
			if (!*alive) return;
			if (recovery_state.Binding() != binding) return;
			recovery_baseline_loading = false;
			if (!error.empty()) {
				recovery_last_error = error;
				if (recovery_manual_after_baseline)
					ShowRecoveryStatus(_("Recovery copies could not be checked. Another attempt will be made later."));
				recovery_manual_after_baseline = false;
				return;
			}
			recovery_device_id = std::move(device_id);
			recovery_snapshots[binding.episode_id] = snapshots;
			recovery_baseline_binding_key = SanaeRecoveryBindingKey(binding);
			if (auto baseline = SanaeNewestRecoveryBaseline(
				snapshots, binding.source_file_id, recovery_device_id))
				recovery_state.SetBaseline(binding, *baseline);
			else recovery_state.SetBaseline(binding, {});
			recovery_last_error.clear();
			DetectRecoveryDifferenceAsync(binding, snapshots);
			bool manual = std::exchange(recovery_manual_after_baseline, false);
			if (manual) CheckRecovery(true);
		});
	});
}

void SanaeProjectManager::DetectRecoveryDifferenceAsync(SanaeRecoveryBinding binding,
	std::vector<SanaeRecoverySnapshotInfo> snapshots)
{
	auto newest = snapshots.end();
	for (auto it = snapshots.begin(); it != snapshots.end(); ++it) {
		if (it->source_file_id != binding.source_file_id) continue;
		if (newest == snapshots.end() || newest->created_at < it->created_at) newest = it;
	}
	if (newest == snapshots.end()) return;
	auto snapshot = *newest;
	auto immutable_ass = std::make_shared<AssFile const>(*context->ass);
	auto temporary_path = CacheRoot(active_project.id) / "recovery" / "tmp"
		/ (new_uuid() + ".ass");
	bool save_ui_state = OPT_GET("App/Save UI State")->GetBool();
	auto alive = recovery_alive;
	agi::dispatch::Background().Async([this, alive, binding, snapshot,
		immutable_ass = std::move(immutable_ass), temporary_path, save_ui_state] {
		std::string sha;
		try { sha = SanaeSha256(build_recovery_ass(
			*immutable_ass, temporary_path, save_ui_state)); }
		catch (...) { return; }
		if (!*alive) return;
		agi::dispatch::Main().Async([this, alive, binding, snapshot, sha = std::move(sha)] {
			if (!*alive || recovery_state.Binding() != binding || sha == snapshot.sha256) return;
			if (snapshot.device_id == recovery_device_id)
				ShowRecoveryStatus(_("A server recovery copy differs from the current file."));
			else ShowRecoveryStatus(_("A recovery copy of this episode exists on another device."));
		});
	});
}

void SanaeProjectManager::CheckRecovery(bool manual) {
	if (!OPT_GET("Sanae/Project/Recovery/Enabled")->GetBool() && !manual) return;
	if (!HasOpenEpisode() || !IsEnrolled()) return;
	auto const& project_binding = context->translationProject->GetSanaeBinding();
	SanaeRecoveryBinding binding{project_binding.episode_id, project_binding.source_file_id};
	if (recovery_state.Binding() != binding) ResetRecoveryBinding();
	auto episode = ActiveEpisode();
	if (!episode || episode->current_source_file_id != binding.source_file_id) {
		recovery_state.PauseForSourceChange();
		recovery_last_error = "source_changed";
		ShowRecoveryStatus(_("The English subtitles were changed. Recovery saving is paused."));
		return;
	}
	if (recovery_baseline_binding_key != SanaeRecoveryBindingKey(binding)) {
		if (manual) recovery_manual_after_baseline = true;
		RefreshRecoveryBaselineAsync();
		return;
	}
	if (auto retry = recovery_state.BeginRetry()) {
		QueueRecoveryUpload(std::move(*retry), {}, manual);
		return;
	}
	if (!recovery_state.BeginCheck(manual)) {
		if (manual && recovery_state.IsUploading())
			ShowRecoveryStatus(_("A recovery copy is already being saved; the newest state will be used."));
		return;
	}
	SanaeRecoveryUpload upload;
	upload.binding = binding;
	upload.generation = recovery_state.Generation();
	upload.idempotency_key = new_uuid();
	recovery_state.StartUpload(upload);
	QueueRecoveryUpload(std::move(upload),
		std::make_shared<AssFile const>(*context->ass), manual);
}

void SanaeProjectManager::QueueRecoveryUpload(SanaeRecoveryUpload upload,
	std::shared_ptr<AssFile const> immutable_ass, bool manual)
{
	auto alive = recovery_alive;
	auto base_url = ServerBaseUrl();
	auto last_sha = recovery_state.LastSuccessfulSha256();
	auto temporary_path = CacheRoot(active_project.id) / "recovery" / "tmp"
		/ (upload.idempotency_key + ".ass");
	bool save_ui_state = OPT_GET("App/Save UI State")->GetBool();
	agi::dispatch::Background().Async([this, alive, base_url, last_sha,
		upload = std::move(upload), immutable_ass = std::move(immutable_ass),
		temporary_path, save_ui_state, manual]() mutable {
		SanaeRecoveryPayloadDecision decision = SanaeRecoveryPayloadDecision::Upload;
		SanaeRecoverySnapshotInfo snapshot;
		std::string error;
		std::string machine_code;
		bool retryable = false;
		try {
			if (immutable_ass) {
				upload.full_ass = build_recovery_ass(
					*immutable_ass, temporary_path, save_ui_state);
				upload.sha256 = SanaeSha256(upload.full_ass);
			}
			std::string effective_last_sha = last_sha;
			if (upload.full_ass.size() > SANAE_RECOVERY_MAX_BYTES)
				decision = SanaeRecoveryPayloadDecision::TooLarge;
			else if (effective_last_sha.empty()) {
				SanaeApiClient api(base_url);
				auto me = parse_json_object(api.Get("/api/v1/me").body);
				std::string device_id;
				if (auto device = object_at(me, "device"))
					device_id = get<std::string>(*device, "id");
				auto list = parse_json_object(api.Get("/api/v1/episodes/"
					+ upload.binding.episode_id + "/recovery-snapshots").body);
				auto snapshots = parse_array<SanaeRecoverySnapshotInfo>(
					list, "snapshots", parse_recovery_snapshot);
				if (auto baseline = SanaeNewestRecoveryBaseline(
					snapshots, upload.binding.source_file_id, device_id))
					effective_last_sha = *baseline;
			}
			if (decision != SanaeRecoveryPayloadDecision::TooLarge)
				decision = SanaeClassifyRecoveryPayload(
					upload.full_ass.size(), upload.sha256, effective_last_sha);
			if (decision == SanaeRecoveryPayloadDecision::Upload) {
				json::Object metadata;
				metadata["source_file_id"] = upload.binding.source_file_id;
				SanaeApiClient api(base_url);
				auto response = api.PostMultipart(
					"/api/v1/episodes/" + upload.binding.episode_id + "/recovery-snapshots",
					write_json(metadata), "full_ass", "recovery.ass", upload.full_ass,
					upload.idempotency_key);
				try {
					auto root = parse_json_object(response.body);
					auto value = object_at(root, "snapshot");
					if (!value) throw RecoveryResponseError("Invalid recovery snapshot response");
					snapshot = parse_recovery_snapshot(*value);
					if (snapshot.episode_id != upload.binding.episode_id
						|| snapshot.source_file_id != upload.binding.source_file_id
						|| lower_ascii(snapshot.sha256) != lower_ascii(upload.sha256))
						throw RecoveryResponseError("Recovery snapshot response did not match the uploaded ASS");
				}
				catch (RecoveryResponseError const&) { throw; }
				catch (std::exception const& e) {
					throw RecoveryResponseError(std::string("Invalid recovery snapshot response: ") + e.what());
				}
			}
		}
		catch (SanaeApiError const& e) {
			error = e.what(); machine_code = e.Code();
			retryable = e.Status() >= 500 || e.Status() == 408 || e.Status() == 429;
		}
		catch (RecoveryResponseError const& e) { error = e.what(); retryable = false; }
		catch (std::exception const& e) { error = e.what(); retryable = true; }

		if (!*alive) return;
		agi::dispatch::Main().Async([this, alive, upload = std::move(upload), snapshot,
			decision, error = std::move(error), machine_code = std::move(machine_code),
			retryable, manual] {
			if (!*alive) return;
			if (!error.empty()) {
				bool source_changed = machine_code == "source_changed";
				bool newer = recovery_state.FinishFailure(
					std::move(upload), retryable, source_changed);
				recovery_last_error = machine_code.empty() ? error : machine_code + ": " + error;
				if (source_changed)
					ShowRecoveryStatus(_("The English subtitles were changed. Recovery saving is paused."));
				else if (manual) ShowRecoveryStatus(retryable
					? _("Recovery saving could not be confirmed. Another attempt will be made later.")
					: _("The recovery copy could not be saved on the server."));
				if (newer) CheckRecovery(false);
				return;
			}
			if (decision == SanaeRecoveryPayloadDecision::TooLarge) {
				recovery_state.FinishWithoutUpload(upload);
				recovery_last_error = "recovery_too_large";
				if (!recovery_too_large_warned) {
					recovery_too_large_warned = true;
					ShowRecoveryStatus(_("The episode recovery copy is too large for the server (maximum 64 MiB). Local autosave continues to work."));
				}
				return;
			}
			recovery_too_large_warned = false;
			recovery_last_error.clear();
			if (decision == SanaeRecoveryPayloadDecision::AlreadyStored) {
				recovery_state.FinishWithoutUpload(upload);
				recovery_state.SetBaseline(upload.binding, upload.sha256);
				if (manual) ShowRecoveryStatus(_("The current version is already saved on the server."));
				CheckRecovery(false);
				return;
			}
			bool newer = recovery_state.FinishSuccess(upload, snapshot.sha256);
			recovery_last_success_at = snapshot.created_at;
			auto& cached = recovery_snapshots[snapshot.episode_id];
			cached.erase(std::remove_if(cached.begin(), cached.end(), [&](auto const& value) {
				return value.id == snapshot.id;
			}), cached.end());
			cached.insert(cached.begin(), snapshot);
			std::sort(cached.begin(), cached.end(), [](auto const& left, auto const& right) {
				return left.created_at > right.created_at;
			});
			std::size_t same_device = 0;
			cached.erase(std::remove_if(cached.begin(), cached.end(), [&](auto const& value) {
				if (value.device_id != snapshot.device_id) return false;
				return ++same_device > 3;
			}), cached.end());
			ShowRecoveryStatus(_("The episode recovery copy was saved on the server."));
			if (newer) CheckRecovery(false);
		});
	});
}

bool SanaeProjectManager::IsEnrolled() const {
	return SanaeApiClient::HasStoredDeviceToken();
}

std::string SanaeProjectManager::ServerBaseUrl() const {
	return OPT_GET("Sanae/Server/Base URL")->GetString();
}

SanaeDeviceInfo SanaeProjectManager::CheckConnection() const {
	auto root = parse_json_object(make_api().Get("/api/v1/me").body);
	auto device = object_at(root, "device");
	if (!device) throw std::runtime_error("Invalid /api/v1/me response");
	return parse_device(*device);
}

void SanaeProjectManager::Enroll(std::string const& display_name, std::string const& device_name,
	std::string const& invitation_key)
{
	json::Object request;
	request["display_name"] = display_name;
	if (!device_name.empty()) request["device_name"] = device_name;
	request["invitation_key"] = invitation_key;
	auto request_json = write_json(request);
	if (request_json != pending_enroll_request) {
		pending_enroll_request = request_json;
		pending_enroll_key = new_uuid();
	}
	auto response = make_api().PostJson("/api/v1/auth/enroll", request_json, false, pending_enroll_key);
	auto root = parse_json_object(response.body);
	auto token = get<std::string>(root, "device_token");
	if (token.empty()) throw std::runtime_error("Sanae enrollment response did not contain device_token");
	SanaeApiClient::StoreDeviceToken(token);
	pending_enroll_request.clear();
	pending_enroll_key.clear();
}

void SanaeProjectManager::ForgetEnrollment() {
	SanaeApiClient::DeleteStoredDeviceToken();
	pending_enroll_request.clear();
	pending_enroll_key.clear();
}

SanaeSeasonInfo SanaeProjectManager::CreateSeason(int year, std::string const& code,
	std::string const& display_name, double sort_order)
{
	if (year < 1900 || year > 3000) throw std::invalid_argument("Season year must be between 1900 and 3000");
	if (code.empty() || code.size() > 32) throw std::invalid_argument("Season code is required and must not exceed 32 characters");
	if (display_name.empty() || display_name.size() > 255)
		throw std::invalid_argument("Season name is required and must not exceed 255 characters");
	json::Object request;
	request["year"] = year;
	request["code"] = code;
	request["display_name"] = display_name;
	request["sort_order"] = sort_order;
	auto request_json = write_json(request);
	if (request_json != pending_season_request) {
		pending_season_request = request_json;
		pending_season_key = new_uuid();
	}
	auto root = parse_json_object(make_api().PostJson("/api/v1/seasons", request_json,
		true, pending_season_key).body);
	auto value = object_at(root, "season");
	if (!value) throw std::runtime_error("Invalid create-season response");
	auto season = parse_season(*value);
	merge_by_id(seasons, std::vector<SanaeSeasonInfo>{season});
	std::sort(seasons.begin(), seasons.end(), [](auto const& a, auto const& b) {
		return std::tie(a.year, a.sort_order, a.display_name) > std::tie(b.year, b.sort_order, b.display_name);
	});
	pending_season_request.clear();
	pending_season_key.clear();
	SaveDirectoryCache();
	AnnounceChanged(SanaeProjectChange::Cache);
	return season;
}

SanaeProjectInfo SanaeProjectManager::CreateProject(std::string const& season_id,
	std::string const& slug, std::string const& name)
{
	if (season_id.empty()) throw std::invalid_argument("A season must be selected");
	if (slug.empty() || slug.size() > 255) throw std::invalid_argument("Project slug is required and must not exceed 255 characters");
	if (name.empty() || name.size() > 255) throw std::invalid_argument("Project name is required and must not exceed 255 characters");
	json::Object request;
	request["season_id"] = season_id;
	request["slug"] = slug;
	request["name"] = name;
	auto request_json = write_json(request);
	if (request_json != pending_project_request) {
		pending_project_request = request_json;
		pending_project_key = new_uuid();
	}
	auto root = parse_json_object(make_api().PostJson("/api/v1/projects", request_json,
		true, pending_project_key).body);
	auto value = object_at(root, "project");
	if (!value) throw std::runtime_error("Invalid create-project response");
	auto project = parse_project(*value);
	merge_by_id(projects, std::vector<SanaeProjectInfo>{project});
	std::sort(projects.begin(), projects.end(), [](auto const& a, auto const& b) { return a.name < b.name; });
	pending_project_request.clear();
	pending_project_key.clear();
	SaveDirectoryCache();
	AnnounceChanged(SanaeProjectChange::Cache);
	return project;
}

void SanaeProjectManager::RefreshDirectory() {
	auto api = make_api();
	auto seasons_root = parse_json_object(api.Get("/api/v1/seasons").body);
	auto projects_root = parse_json_object(api.Get("/api/v1/projects").body);
	seasons = parse_array<SanaeSeasonInfo>(seasons_root, "seasons", parse_season);
	projects = parse_array<SanaeProjectInfo>(projects_root, "projects", parse_project);
	std::sort(seasons.begin(), seasons.end(), [](auto const& a, auto const& b) {
		return std::tie(a.year, a.sort_order, a.display_name) > std::tie(b.year, b.sort_order, b.display_name);
	});
	std::sort(projects.begin(), projects.end(), [](auto const& a, auto const& b) { return a.name < b.name; });
	SaveDirectoryCache();
	AnnounceChanged(SanaeProjectChange::Cache);
}

agi::fs::path SanaeProjectManager::DirectoryCachePath() const {
	return config::path->Decode("?user/sanae") / "directory.json";
}

agi::fs::path SanaeProjectManager::CacheRoot(std::string const& project_id) const {
	return config::path->Decode("?user/sanae/projects") / project_id;
}

agi::fs::path SanaeProjectManager::SnapshotPath(std::string const& project_id) const {
	return CacheRoot(project_id) / "snapshot.json";
}

agi::fs::path SanaeProjectManager::DraftPath(std::string const& project_id, std::string const& episode_id) const {
	return CacheRoot(project_id) / "drafts" / (episode_id + ".json");
}

agi::fs::path SanaeProjectManager::BatchImportStatePath(std::string const& project_id) const {
	return CacheRoot(project_id) / "imports" / "batch-import.json";
}

agi::fs::path SanaeProjectManager::FilePath(std::string const& project_id, std::string const& file_id) const {
	return CacheRoot(project_id) / "files" / (file_id + ".ass");
}

void SanaeProjectManager::LoadDirectoryCache() {
	try {
		auto path = DirectoryCachePath();
		if (!agi::fs::FileExists(path)) return;
		auto root = parse_json_object(read_file(path));
		seasons = parse_array<SanaeSeasonInfo>(root, "seasons", parse_season);
		projects = parse_array<SanaeProjectInfo>(root, "projects", parse_project);
	}
	catch (...) {
		seasons.clear();
		projects.clear();
	}
}

void SanaeProjectManager::SaveDirectoryCache() const {
	json::Object root;
	root["version"] = 1;
	root["seasons"] = serialize_array(seasons, serialize_season);
	root["projects"] = serialize_array(projects, serialize_project);
	write_file(DirectoryCachePath(), write_json(root));
}

void SanaeProjectManager::LoadSnapshot(std::string const& project_id) {
	active_project = {};
	active_project.id = project_id;
	episodes.clear(); files.clear(); finalized_revisions.clear(); terminology.clear();
	terminology_history.clear(); ignored_candidates.clear();
	cache_loaded = false;
	terminology_history_complete = false;
	auto path = SnapshotPath(project_id);
	if (!agi::fs::FileExists(path)) return;
	try {
		auto root = parse_json_object(read_file(path));
		if (auto project = object_at(root, "project")) active_project = parse_project(*project);
		episodes = parse_array<SanaeEpisodeInfo>(root, "episodes", parse_episode);
		files = parse_array<SanaeEpisodeFileInfo>(root, "files", parse_file);
		finalized_revisions = parse_array<SanaeFinalizedRevisionInfo>(root, "finalized_revisions", parse_finalized);
		terminology = parse_array<SanaeTerminologyEntry>(root, "terminology", [](json::Object const& value) {
			auto parsed = parse_term(value);
			parsed.deleted = get<bool>(value, "deleted", parsed.deleted);
			return parsed;
		});
		if (array_at(root, "terminology_history")) {
			terminology_history = parse_array<SanaeTerminologyHistoryEntry>(
				root, "terminology_history", parse_term_history);
			terminology_history_complete = get<bool>(root, "terminology_history_complete", true);
		}
		ignored_candidates = parse_array<SanaeIgnoredCandidate>(root, "ignored_candidates", [](json::Object const& value) {
			auto parsed = parse_ignore(value);
			parsed.deleted = get<bool>(value, "deleted", parsed.deleted);
			return parsed;
		});
		cache_loaded = active_project.id == project_id;
	}
	catch (...) {
		active_project = {};
		active_project.id = project_id;
		episodes.clear(); files.clear(); finalized_revisions.clear(); terminology.clear();
		terminology_history.clear(); ignored_candidates.clear();
		terminology_history_complete = false;
	}
}

void SanaeProjectManager::SaveSnapshot() const {
	if (active_project.id.empty()) return;
	json::Object root;
	root["version"] = 2;
	root["project"] = serialize_project(active_project);
	root["episodes"] = serialize_array(episodes, serialize_episode);
	root["files"] = serialize_array(files, serialize_file);
	root["finalized_revisions"] = serialize_array(finalized_revisions, serialize_finalized);
	root["terminology"] = serialize_array(terminology, serialize_term);
	root["terminology_history"] = serialize_array(terminology_history, serialize_term_history);
	root["terminology_history_complete"] = terminology_history_complete;
	root["ignored_candidates"] = serialize_array(ignored_candidates, serialize_ignore);
	write_file(SnapshotPath(active_project.id), write_json(root));
}

void SanaeProjectManager::SyncProject(std::string const& project_id) {
	if (active_project.id != project_id || !cache_loaded) LoadSnapshot(project_id);
	// v0.1 snapshots did not store terminology history. Force one full
	// snapshot after upgrading so old history cannot remain silently absent.
	int since = cache_loaded && terminology_history_complete ? active_project.current_revision : 0;
	json::Object request;
	request["since_revision"] = since;
	SanaeHttpResponse response;
	try {
		response = make_api().PostJson("/api/v1/projects/" + project_id + "/sync", write_json(request));
	}
	catch (SanaeApiError const& error) {
		if (error.Code() != "invalid_since_revision" || since == 0) throw;
		request["since_revision"] = 0;
		response = make_api().PostJson("/api/v1/projects/" + project_id + "/sync", write_json(request));
	}

	auto root = parse_json_object(response.body);
	bool full = get<bool>(root, "full_snapshot", since == 0);
	if (auto project = object_at(root, "project")) active_project = parse_project(*project);
	auto new_episodes = parse_array<SanaeEpisodeInfo>(root, "episodes", parse_episode);
	auto new_files = parse_array<SanaeEpisodeFileInfo>(root, "files", parse_file);
	auto new_finalized = parse_array<SanaeFinalizedRevisionInfo>(root, "finalized_revisions", parse_finalized);
	auto new_terms = parse_array<SanaeTerminologyEntry>(root, "terminology", parse_term);
	auto new_term_history = parse_array<SanaeTerminologyHistoryEntry>(
		root, "terminology_history", parse_term_history);
	auto new_ignores = parse_array<SanaeIgnoredCandidate>(root, "ignored_candidates", parse_ignore);
	if (full) {
		episodes = std::move(new_episodes); files = std::move(new_files);
		finalized_revisions = std::move(new_finalized); terminology = std::move(new_terms);
		terminology_history = std::move(new_term_history);
		ignored_candidates = std::move(new_ignores);
	}
	else {
		merge_by_id(episodes, std::move(new_episodes));
		merge_by_id(files, std::move(new_files));
		merge_by_id(finalized_revisions, std::move(new_finalized));
		merge_by_id(terminology, std::move(new_terms));
		merge_by_id(terminology_history, std::move(new_term_history));
		merge_by_id(ignored_candidates, std::move(new_ignores));
	}
	cache_loaded = true;
	terminology_history_complete = true;
	DownloadMissingFiles();
	SaveSnapshot();
	if (context->translationProject->HasSanaeBinding()) {
		auto const& binding = context->translationProject->GetSanaeBinding();
		if (binding.project_id == project_id) {
			auto bound_episode = FindEpisode(binding.episode_id);
			if (!bound_episode || bound_episode->IsDeleted()) CloseEpisode();
			else {
				auto source_path = FilePath(project_id, bound_episode->current_source_file_id);
				if (agi::fs::FileExists(source_path)) context->translationProject->LoadSource(source_path);
			}
		}
	}
	LoadDrafts();
	RebuildMemory();
	RebuildRepeatCache();
	AnnounceChanged(SanaeProjectChange::Cache);
}

void SanaeProjectManager::DownloadMissingFiles() {
	if (active_project.id.empty()) return;
	auto api = make_api();
	for (auto const& file : files) {
		if (file.project_id != active_project.id) continue;
		auto path = FilePath(active_project.id, file.id);
		bool have_valid_file = false;
		try {
			have_valid_file = agi::fs::FileExists(path) && agi::fs::Size(path) == file.size_bytes;
			if (have_valid_file && !file.sha256.empty()) {
				auto digest = sha256_hex(read_file(path));
				if (!digest.empty()) have_valid_file = digest == lower_ascii(file.sha256);
			}
		}
		catch (...) { }
		if (have_valid_file) continue;
		// Never make a conditional request for a missing or size-mismatched cache
		// entry: a 304 would leave us without the bytes needed for offline use.
		auto response = api.Get("/api/v1/files/" + file.id, true);
		if (response.status == 304 && agi::fs::FileExists(path)) continue;
		if (response.body.size() != file.size_bytes)
			throw std::runtime_error("Downloaded Sanae file size does not match the server snapshot");
		if (!file.sha256.empty()) {
			auto digest = sha256_hex(response.body);
			if (!digest.empty() && digest != lower_ascii(file.sha256))
				throw std::runtime_error("Downloaded Sanae file SHA-256 does not match the server snapshot");
			auto etag = response.etag;
			if (etag.size() >= 2 && etag.front() == '"' && etag.back() == '"')
				etag = etag.substr(1, etag.size() - 2);
			if (!etag.empty() && lower_ascii(etag) != lower_ascii(file.sha256))
				throw std::runtime_error("Downloaded Sanae file ETag does not match the server snapshot");
		}
		write_file(path, response.body);
	}
}

void SanaeProjectManager::RefreshTerminologyHistory() {
	if (active_project.id.empty()) return;
	try {
		auto root = parse_json_object(make_api().Get("/api/v1/projects/" + active_project.id
			+ "/terminology/history").body);
		terminology_history = parse_array<SanaeTerminologyHistoryEntry>(
			root, "terminology_history", parse_term_history);
		terminology_history_complete = true;
	}
	catch (...) {
		terminology_history_complete = false;
		throw;
	}
}

SanaeEpisodeInfo SanaeProjectManager::CreateEpisode(std::string const& project_id,
	std::string const& episode_code, double sort_order, agi::fs::path const& ensub_path)
{
	json::Object metadata;
	metadata["episode_code"] = episode_code;
	metadata["sort_order"] = sort_order;
	auto source_data = read_file(ensub_path);
	auto metadata_json = write_json(metadata);
	auto digest = sha256_hex(source_data);
	if (digest.empty()) digest = std::to_string(std::hash<std::string>{}(source_data));
	auto request_signature = project_id + "\n" + metadata_json + "\n"
		+ ensub_path.filename().string() + "\n" + digest;
	if (request_signature != pending_episode_request) {
		pending_episode_request = request_signature;
		pending_episode_key = new_uuid();
	}
	auto episode = CreateEpisodeRequest(project_id, episode_code, sort_order,
		ensub_path, source_data, pending_episode_key, true);
	pending_episode_request.clear();
	pending_episode_key.clear();
	return episode;
}

SanaeEpisodeInfo SanaeProjectManager::CreateEpisodeDetached(std::string const& project_id,
	std::string const& episode_code, double sort_order, agi::fs::path const& ensub_path,
	std::string const& idempotency_key)
{
	if (idempotency_key.empty())
		throw std::invalid_argument("Batch episode creation requires an idempotency key");
	auto source_data = read_file(ensub_path);
	return CreateEpisodeRequest(project_id, episode_code, sort_order,
		ensub_path, source_data, idempotency_key, false);
}

SanaeEpisodeInfo SanaeProjectManager::CreateEpisodeRequest(std::string const& project_id,
	std::string const& episode_code, double sort_order, agi::fs::path const& ensub_path,
	std::string const& source_data, std::string const& idempotency_key,
	bool attach_to_current_ass)
{
	if (active_project.id != project_id || !cache_loaded) LoadSnapshot(project_id);
	json::Object metadata;
	metadata["episode_code"] = episode_code;
	metadata["sort_order"] = sort_order;
	auto metadata_json = write_json(metadata);
	auto response = make_api().PostMultipart("/api/v1/projects/" + project_id + "/episodes",
		metadata_json, "ensub", ensub_path.filename().string(), source_data, idempotency_key);
	auto root = parse_json_object(response.body);
	auto episode_object = object_at(root, "episode");
	auto file_object = object_at(root, "source_file");
	if (!episode_object || !file_object) throw std::runtime_error("Invalid create-episode response");
	auto episode = parse_episode(*episode_object);
	auto file = parse_file(*file_object);
	merge_by_id(episodes, std::vector<SanaeEpisodeInfo>{episode});
	merge_by_id(files, std::vector<SanaeEpisodeFileInfo>{file});
	active_project.current_revision = get<int>(root, "project_revision", active_project.current_revision);
	write_file(FilePath(project_id, file.id), source_data);
	SaveSnapshot();
	if (attach_to_current_ass) AttachEpisode(episode.id);
	return episode;
}

SanaeEpisodeInfo const *SanaeProjectManager::FindEpisode(std::string const& episode_id) const {
	auto found = std::find_if(episodes.begin(), episodes.end(), [&](auto const& episode) {
		return episode.id == episode_id;
	});
	return found == episodes.end() ? nullptr : &*found;
}

SanaeEpisodeFileInfo const *SanaeProjectManager::FindFile(std::string const& file_id) const {
	auto found = std::find_if(files.begin(), files.end(), [&](auto const& file) {
		return file.id == file_id;
	});
	return found == files.end() ? nullptr : &*found;
}

SanaeEpisodeDetails SanaeProjectManager::GetEpisodeDetails(std::string const& episode_id) {
	if (episode_id.empty()) throw std::invalid_argument("Episode id is required");
	try {
		auto root = parse_json_object(make_api().Get("/api/v1/episodes/" + episode_id).body);
		auto episode_object = object_at(root, "episode");
		if (!episode_object) throw std::runtime_error("Invalid episode-details response");
		SanaeEpisodeDetails result;
		result.episode = parse_episode(*episode_object);
		result.files = parse_array<SanaeEpisodeFileInfo>(root, "files", parse_file);
		result.finalized_revisions = parse_array<SanaeFinalizedRevisionInfo>(
			root, "finalized_revisions", parse_finalized);
		merge_by_id(episodes, std::vector<SanaeEpisodeInfo>{result.episode});
		merge_by_id(files, result.files);
		merge_by_id(finalized_revisions, result.finalized_revisions);
		SaveSnapshot();
		return result;
	}
	catch (...) {
		auto episode = FindEpisode(episode_id);
		if (!episode) throw;
		SanaeEpisodeDetails result;
		result.episode = *episode;
		result.local_cache_only = true;
		for (auto const& file : files)
			if (file.episode_id == episode_id) result.files.push_back(file);
		for (auto const& revision : finalized_revisions)
			if (revision.episode_id == episode_id) result.finalized_revisions.push_back(revision);
		return result;
	}
}

std::vector<SanaeRecoverySnapshotInfo> SanaeProjectManager::ListRecoverySnapshots(
	std::string const& episode_id)
{
	if (episode_id.empty()) throw std::invalid_argument("Episode id is required");
	auto root = parse_json_object(make_api().Get("/api/v1/episodes/" + episode_id
		+ "/recovery-snapshots").body);
	auto snapshots = parse_array<SanaeRecoverySnapshotInfo>(
		root, "snapshots", parse_recovery_snapshot);
	recovery_snapshots[episode_id] = snapshots;
	return snapshots;
}

std::string SanaeProjectManager::ReadRecoverySnapshot(
	SanaeRecoverySnapshotInfo const& snapshot)
{
	if (snapshot.id.empty()) throw std::invalid_argument("Recovery snapshot id is required");
	auto response = make_api().Get("/api/v1/recovery-snapshots/" + snapshot.id + "/file");
	if (response.body.size() != snapshot.size_bytes)
		throw std::runtime_error("Downloaded recovery size does not match its metadata");
	auto digest = SanaeSha256(response.body);
	if (digest != snapshot.sha256)
		throw std::runtime_error("Downloaded recovery SHA-256 does not match its metadata");
	auto etag = response.etag;
	if (etag.size() >= 2 && etag.front() == '"' && etag.back() == '"')
		etag = etag.substr(1, etag.size() - 2);
	if (!etag.empty() && lower_ascii(etag) != lower_ascii(snapshot.sha256))
		throw std::runtime_error("Downloaded recovery ETag does not match its metadata");
	return response.body;
}

SanaeSemanticDiff SanaeProjectManager::CompareRecoverySnapshot(
	SanaeRecoverySnapshotInfo const& snapshot)
{
	auto bytes = ReadRecoverySnapshot(snapshot);
	auto path = CacheRoot(snapshot.project_id) / "recovery" / "preview"
		/ (snapshot.id + ".ass");
	write_file(path, bytes);
	try {
		auto recovered = load_ass(context, path);
		auto diff = SanaeCompareSemanticSubtitles(
			semantic_lines(*context->ass), semantic_lines(*recovered));
		agi::fs::Remove(path);
		return diff;
	}
	catch (...) {
		try { agi::fs::Remove(path); }
		catch (...) { }
		throw;
	}
}

void SanaeProjectManager::DeleteRecoverySnapshot(std::string const& snapshot_id) {
	if (snapshot_id.empty()) throw std::invalid_argument("Recovery snapshot id is required");
	if (pending_recovery_delete_id != snapshot_id) {
		pending_recovery_delete_id = snapshot_id;
		pending_recovery_delete_key = new_uuid();
	}
	auto root = parse_json_object(make_api().Delete(
		"/api/v1/recovery-snapshots/" + snapshot_id, pending_recovery_delete_key).body);
	auto value = object_at(root, "snapshot");
	if (!value || get<std::string>(*value, "id") != snapshot_id)
		throw std::runtime_error("Invalid delete-recovery response");
	for (auto& [episode_id, snapshots] : recovery_snapshots) {
		(void)episode_id;
		snapshots.erase(std::remove_if(snapshots.begin(), snapshots.end(),
			[&](auto const& snapshot) { return snapshot.id == snapshot_id; }), snapshots.end());
	}
	pending_recovery_delete_id.clear();
	pending_recovery_delete_key.clear();
}

void SanaeProjectManager::RequestRecoveryNow() {
	if (recovery_state.IsPaused()) {
		wxMessageDialog dialog(context->parent,
			_("The English subtitles of this episode were changed on the server.\n\n"
			  "Recovery saving is paused because the current working ASS is linked to the previous source version."),
			_("Recovery saving paused"), wxYES_NO | wxICON_WARNING);
		dialog.SetYesNoLabels(_("Synchronize"), _("Cancel"));
		if (dialog.ShowModal() == wxID_YES) {
			try { SyncProject(ActiveProjectId()); }
			catch (std::exception const& error) {
				wxMessageBox(agi::wxformat(
					_("The project could not be synchronized.\n\nDetails: %s"),
					to_wx(error.what())), _("Recovery saving paused"),
					wxOK | wxICON_ERROR, context->parent);
			}
		}
		return;
	}
	CheckRecovery(true);
}

void SanaeProjectManager::PrepareRecoveryOnShutdown() {
	if (!OPT_GET("Sanae/Project/Recovery/Enabled")->GetBool()
		|| !HasOpenEpisode() || !IsEnrolled()) return;
	if (auto retry = recovery_state.BeginRetry()) {
		QueueRecoveryUpload(std::move(*retry), {}, false);
		return;
	}
	if (!recovery_state.BeginCheck(true)) return;
	auto const& value = context->translationProject->GetSanaeBinding();
	SanaeRecoveryUpload upload;
	upload.binding = {value.episode_id, value.source_file_id};
	upload.generation = recovery_state.Generation();
	upload.idempotency_key = new_uuid();
	recovery_state.StartUpload(upload);
	QueueRecoveryUpload(std::move(upload),
		std::make_shared<AssFile const>(*context->ass), false);
}

std::vector<SanaeRecoverySnapshotInfo> const& SanaeProjectManager::CachedRecoverySnapshots(
	std::string const& episode_id) const
{
	static std::vector<SanaeRecoverySnapshotInfo> const empty;
	auto found = recovery_snapshots.find(episode_id);
	return found == recovery_snapshots.end() ? empty : found->second;
}

std::string SanaeProjectManager::ReadServerFile(SanaeEpisodeFileInfo const& file) {
	if (active_project.id.empty() || file.project_id != active_project.id)
		throw std::runtime_error("The requested file is not part of the active project");
	auto path = FilePath(file.project_id, file.id);
	auto valid_cached = [&] {
		if (!agi::fs::FileExists(path) || agi::fs::Size(path) != file.size_bytes) return false;
		if (file.sha256.empty()) return true;
		auto digest = sha256_hex(read_file(path));
		return digest.empty() || lower_ascii(digest) == lower_ascii(file.sha256);
	};
	try {
		if (valid_cached()) return read_file(path);
	}
	catch (...) { }
	auto response = make_api().Get("/api/v1/files/" + file.id, true);
	if (response.body.size() != file.size_bytes)
		throw std::runtime_error("Downloaded Sanae file size does not match its metadata");
	if (!file.sha256.empty()) {
		auto digest = sha256_hex(response.body);
		if (!digest.empty() && lower_ascii(digest) != lower_ascii(file.sha256))
			throw std::runtime_error("Downloaded Sanae file SHA-256 does not match its metadata");
		auto etag = response.etag;
		if (etag.size() >= 2 && etag.front() == '"' && etag.back() == '"')
			etag = etag.substr(1, etag.size() - 2);
		if (!etag.empty() && lower_ascii(etag) != lower_ascii(file.sha256))
			throw std::runtime_error("Downloaded Sanae file ETag does not match its metadata");
	}
	write_file(path, response.body);
	return response.body;
}

std::string SanaeProjectManager::ReadEpisodeFile(std::string const& file_id) {
	auto file = FindFile(file_id);
	if (!file) throw std::invalid_argument("Episode file is not present in the local project snapshot");
	return ReadServerFile(*file);
}

SanaeSemanticDiff SanaeProjectManager::CompareEpisodeSource(std::string const& episode_id,
	agi::fs::path const& ensub_path)
{
	ValidateBatchEnsub(ensub_path);
	auto episode = FindEpisode(episode_id);
	if (!episode || episode->IsDeleted())
		throw std::invalid_argument("Active episode is not present in the local project snapshot");
	auto source_file = FindFile(episode->current_source_file_id);
	if (!source_file) {
		(void)GetEpisodeDetails(episode_id);
		episode = FindEpisode(episode_id);
		source_file = episode ? FindFile(episode->current_source_file_id) : nullptr;
	}
	if (!source_file) throw std::runtime_error("Current ENSUB metadata is unavailable");
	(void)ReadServerFile(*source_file);
	auto before = load_ass(context, FilePath(source_file->project_id, source_file->id));
	auto after = load_ass(context, ensub_path);
	return SanaeCompareSemanticSubtitles(semantic_lines(*before), semantic_lines(*after));
}

SanaeEpisodeInfo SanaeProjectManager::ReplaceEpisodeSource(std::string const& episode_id,
	agi::fs::path const& ensub_path)
{
	ValidateBatchEnsub(ensub_path);
	auto episode = FindEpisode(episode_id);
	if (!episode || episode->IsDeleted())
		throw std::invalid_argument("Active episode is not present in the local project snapshot");
	auto source_data = read_file(ensub_path);
	auto digest = sha256_hex(source_data);
	if (digest.empty()) digest = std::to_string(std::hash<std::string>{}(source_data));
	auto signature = episode_id + "\n" + episode->current_source_file_id + "\n"
		+ ensub_path.filename().string() + "\n" + digest;
	if (signature != pending_source_request) {
		pending_source_request = std::move(signature);
		pending_source_key = new_uuid();
	}
	auto result = ReplaceEpisodeSourceRequest(episode_id, ensub_path, source_data, pending_source_key);
	pending_source_request.clear();
	pending_source_key.clear();
	return result;
}

SanaeEpisodeInfo SanaeProjectManager::ReplaceEpisodeSourceWithKey(
	std::string const& episode_id, agi::fs::path const& ensub_path,
	std::string const& idempotency_key)
{
	if (idempotency_key.empty())
		throw std::invalid_argument("Source replacement requires an idempotency key");
	ValidateBatchEnsub(ensub_path);
	return ReplaceEpisodeSourceRequest(episode_id, ensub_path, read_file(ensub_path), idempotency_key);
}

SanaeEpisodeInfo SanaeProjectManager::ReplaceEpisodeSourceRequest(
	std::string const& episode_id, agi::fs::path const& ensub_path,
	std::string const& source_data, std::string const& idempotency_key)
{
	auto episode = FindEpisode(episode_id);
	if (!episode || episode->IsDeleted())
		throw std::invalid_argument("Active episode is not present in the local project snapshot");
	if (episode->current_source_file_id.empty())
		throw std::runtime_error("Episode has no current ENSUB");
	auto project_id = episode->project_id;
	auto base_source_file_id = episode->current_source_file_id;
	json::Object metadata;
	metadata["base_source_file_id"] = base_source_file_id;
	auto response = make_api().PostMultipart("/api/v1/episodes/" + episode_id + "/source",
		write_json(metadata), "ensub", ensub_path.filename().string(), source_data, idempotency_key);
	auto root = parse_json_object(response.body);
	auto episode_object = object_at(root, "episode");
	auto file_object = object_at(root, "source_file");
	if (!episode_object || !file_object)
		throw std::runtime_error("Invalid replace-source response");
	auto updated_episode = parse_episode(*episode_object);
	auto updated_file = parse_file(*file_object);
	merge_by_id(episodes, std::vector<SanaeEpisodeInfo>{updated_episode});
	merge_by_id(files, std::vector<SanaeEpisodeFileInfo>{updated_file});
	active_project.current_revision = get<int>(root, "project_revision", active_project.current_revision);
	write_file(FilePath(project_id, updated_file.id), source_data);

	if (episode_id == ActiveEpisodeId()) {
		SanaeEpisodeBinding binding;
		binding.project_id = updated_episode.project_id;
		binding.episode_id = updated_episode.id;
		binding.source_file_id = updated_episode.current_source_file_id;
		binding.base_finalized_revision_id = updated_episode.current_finalized_revision_id;
		binding.project_revision = active_project.current_revision;
		context->translationProject->SetSanaeBinding(std::move(binding));
		context->translationProject->LoadSource(FilePath(project_id, updated_file.id));
		pending_finalize_key.clear();
		SaveDrafts();
		ResetRecoveryBinding();
	}
	SaveSnapshot();
	RebuildMemory();
	RebuildRepeatCache();
	AnnounceChanged(SanaeProjectChange::Cache);
	return updated_episode;
}

SanaeEpisodeInfo SanaeProjectManager::DeleteEpisode(std::string const& episode_id) {
	auto episode = FindEpisode(episode_id);
	if (!episode || episode->IsDeleted())
		throw std::invalid_argument("Active episode is not present in the local project snapshot");
	if (pending_delete_episode_id != episode_id) {
		pending_delete_episode_id = episode_id;
		pending_delete_key = new_uuid();
	}
	auto response = make_api().Delete("/api/v1/episodes/" + episode_id, pending_delete_key);
	auto root = parse_json_object(response.body);
	auto episode_object = object_at(root, "episode");
	if (!episode_object) throw std::runtime_error("Invalid delete-episode response");
	auto deleted_episode = parse_episode(*episode_object);
	bool was_open = episode_id == ActiveEpisodeId();
	merge_by_id(episodes, std::vector<SanaeEpisodeInfo>{deleted_episode});
	active_project.current_revision = get<int>(root, "project_revision", active_project.current_revision);
	pending_delete_episode_id.clear();
	pending_delete_key.clear();
	if (was_open) CloseEpisode();
	SaveSnapshot();
	RebuildMemory();
	RebuildRepeatCache();
	AnnounceChanged(SanaeProjectChange::Cache);
	return deleted_episode;
}

SanaeSemanticDiff SanaeProjectManager::CompareFinalizedRevisions(
	std::string const& before_revision_id, std::string const& after_revision_id)
{
	auto find_revision = [&](std::string const& id) -> SanaeFinalizedRevisionInfo const * {
		auto found = std::find_if(finalized_revisions.begin(), finalized_revisions.end(),
			[&](auto const& value) { return value.id == id; });
		return found == finalized_revisions.end() ? nullptr : &*found;
	};
	auto before_revision = find_revision(before_revision_id);
	auto after_revision = find_revision(after_revision_id);
	if (!before_revision || !after_revision)
		throw std::invalid_argument("Finalized revision is not present in the local project snapshot");
	if (before_revision->episode_id != after_revision->episode_id)
		throw std::invalid_argument("Only revisions of the same episode can be compared");
	auto before_file = FindFile(before_revision->compact_rusub_file_id);
	auto after_file = FindFile(after_revision->compact_rusub_file_id);
	if (!before_file || !after_file) throw std::runtime_error("Finalized RUSUB metadata is unavailable");
	(void)ReadServerFile(*before_file);
	(void)ReadServerFile(*after_file);
	auto before = load_ass(context, FilePath(before_file->project_id, before_file->id));
	auto after = load_ass(context, FilePath(after_file->project_id, after_file->id));
	return SanaeCompareSemanticSubtitles(semantic_lines(*before), semantic_lines(*after));
}

void SanaeProjectManager::ValidateBatchEnsub(agi::fs::path const& ensub_path) const {
	if (!agi::fs::FileExists(ensub_path))
		throw std::runtime_error("ENSUB file does not exist: " + ensub_path.string());
	if (agi::fs::Size(ensub_path) > 16 * 1024 * 1024)
		throw std::runtime_error("ENSUB exceeds the server 16 MiB limit");
	auto source_data = read_file(ensub_path);
	if (source_data.find('\0') != std::string::npos)
		throw std::runtime_error("ENSUB contains a NUL byte");
	if (!valid_utf8(source_data))
		throw std::runtime_error("ENSUB must be UTF-8");
	auto lowered = lower_ascii(source_data);
	auto events_section = lowered.find("[events]");
	bool event_format = false;
	if (events_section != std::string::npos) {
		auto line_start = lowered.find('\n', events_section);
		while (line_start != std::string::npos && ++line_start < lowered.size()) {
			auto line_end = lowered.find('\n', line_start);
			auto line = std::string_view(lowered).substr(line_start,
				(line_end == std::string::npos ? lowered.size() : line_end) - line_start);
			while (!line.empty() && (line.front() == ' ' || line.front() == '\t' || line.front() == '\r'))
				line.remove_prefix(1);
			if (!line.empty() && line.front() == '[') break;
			if (line.starts_with("format:")) { event_format = true; break; }
			line_start = line_end;
		}
	}
	if (lowered.find("[script info]") == std::string::npos
		|| events_section == std::string::npos || !event_format)
		throw std::runtime_error("ENSUB must contain [Script Info], [Events] and an event Format line");
	(void)load_ass(context, ensub_path);
}

SanaeCompactStats SanaeProjectManager::PreviewBatchRusub(agi::fs::path const& rusub_path) const {
	if (!agi::fs::FileExists(rusub_path))
		throw std::runtime_error("RUSUB file does not exist: " + rusub_path.string());
	auto rusub = load_ass(context, rusub_path);
	SanaeCompactStats stats;
	(void)BuildSanaeCompactRusub(*rusub, &stats);
	return stats;
}

SanaeCompactStats SanaeProjectManager::FinalizeEpisodeFromFile(std::string const& episode_id,
	agi::fs::path const& rusub_path, std::string const& idempotency_key)
{
	if (idempotency_key.empty())
		throw std::invalid_argument("Batch Finalize requires an idempotency key");
	if (episode_id == ActiveEpisodeId())
		throw std::runtime_error("The episode is attached to the current ASS; finalize it through Final Review");
	auto episode = FindEpisode(episode_id);
	if (!episode || episode->IsDeleted())
		throw std::invalid_argument("Active episode is not present in the local Sanae snapshot");
	if (episode->project_id != active_project.id)
		throw std::runtime_error("Batch episode does not belong to the active project");
	if (episode->current_source_file_id.empty())
		throw std::runtime_error("Episode has no current ENSUB");

	auto rusub = load_ass(context, rusub_path);
	SanaeCompactStats stats;
	auto compact = BuildSanaeCompactRusub(*rusub, &stats);
	auto pending_path = CacheRoot(active_project.id) / "imports" / "pending"
		/ (episode_id + ".compact.ass");
	agi::fs::CreateDirectory(pending_path.parent_path());
	SubtitleFormat::GetWriter(pending_path)->WriteFile(&compact, pending_path,
		context->project->Timecodes(), "UTF-8");
	auto compact_data = read_file(pending_path);
	if (compact_data.size() > 16 * 1024 * 1024)
		throw std::runtime_error("Compact RUSUB exceeds the server 16 MiB limit");

	json::Object metadata;
	metadata["base_project_revision"] = active_project.current_revision;
	metadata["source_file_id"] = episode->current_source_file_id;
	if (episode->current_finalized_revision_id.empty())
		metadata["base_finalized_revision_id"] = json::Null();
	else metadata["base_finalized_revision_id"] = episode->current_finalized_revision_id;
	json::Array term_ops;
	json::Array ignore_ops;
	metadata["terminology_ops"] = std::move(term_ops);
	metadata["ignore_ops"] = std::move(ignore_ops);
	auto metadata_json = write_json(metadata);
	if (metadata_json.size() > 1024 * 1024)
		throw std::runtime_error("Finalize metadata exceeds the server 1 MiB limit");

	auto response = make_api().PostMultipart("/api/v1/episodes/" + episode_id + "/finalize",
		metadata_json, "compact_rusub", "compact-rusub.ass", compact_data, idempotency_key);
	auto root = parse_json_object(response.body);
	auto episode_object = object_at(root, "episode");
	auto file_object = object_at(root, "compact_rusub_file");
	auto revision_object = object_at(root, "finalized_revision");
	if (!episode_object || !file_object || !revision_object)
		throw std::runtime_error("Invalid batch Finalize response");
	auto updated_episode = parse_episode(*episode_object);
	auto file = parse_file(*file_object);
	auto revision = parse_finalized(*revision_object);
	merge_by_id(episodes, std::vector<SanaeEpisodeInfo>{updated_episode});
	merge_by_id(files, std::vector<SanaeEpisodeFileInfo>{file});
	merge_by_id(finalized_revisions, std::vector<SanaeFinalizedRevisionInfo>{revision});
	active_project.current_revision = get<int>(root, "project_revision", active_project.current_revision);
	write_file(FilePath(active_project.id, file.id), compact_data);
	SaveSnapshot();
	return stats;
}

void SanaeProjectManager::FinishBatchImport() {
	SaveSnapshot();
	RebuildMemory();
	RebuildRepeatCache();
	AnnounceChanged(SanaeProjectChange::Cache);
}

void SanaeProjectManager::AttachEpisode(std::string const& episode_id) {
	if (HasOpenEpisode() && ActiveEpisodeId() != episode_id) CheckRecovery(false);
	auto episode = FindEpisode(episode_id);
	if (!episode || episode->IsDeleted())
		throw std::invalid_argument("Active episode is not present in the local Sanae snapshot");
	if (episode->current_source_file_id.empty()) throw std::runtime_error("Episode has no current ENSUB");
	auto source_path = FilePath(episode->project_id, episode->current_source_file_id);
	if (!agi::fs::FileExists(source_path)) DownloadMissingFiles();
	if (!agi::fs::FileExists(source_path)) throw std::runtime_error("Current ENSUB is not available in the local cache");

	SanaeEpisodeBinding binding;
	binding.project_id = episode->project_id;
	binding.episode_id = episode->id;
	binding.source_file_id = episode->current_source_file_id;
	binding.base_finalized_revision_id = episode->current_finalized_revision_id;
	binding.project_revision = active_project.current_revision;
	context->translationProject->SetSanaeBinding(std::move(binding));
	context->translationProject->LoadSource(source_path);
	LoadDrafts();
	RebuildMemory();
	RebuildRepeatCache();
	ResetRecoveryBinding();
	AnnounceChanged(SanaeProjectChange::Binding);
}

void SanaeProjectManager::CloseEpisode() {
	if (HasOpenEpisode()) CheckRecovery(false);
	context->translationProject->ClearSanaeBinding();
	recovery_state.ClearBinding();
	recovery_baseline_binding_key.clear();
	recovery_baseline_loading = false;
	terminology_drafts.clear();
	ignore_drafts.clear();
	pending_finalize_key.clear();
	line_repeats.clear();
	AnnounceChanged(SanaeProjectChange::Binding);
}

bool SanaeProjectManager::HasOpenEpisode() const {
	auto episode = ActiveEpisode();
	return context->translationProject->HasSanaeBinding() && episode && !episode->IsDeleted();
}

std::string SanaeProjectManager::ActiveEpisodeId() const {
	return context->translationProject->HasSanaeBinding()
		? context->translationProject->GetSanaeBinding().episode_id : std::string();
}

SanaeEpisodeInfo const *SanaeProjectManager::ActiveEpisode() const {
	return FindEpisode(ActiveEpisodeId());
}

void SanaeProjectManager::OnSubtitleOpened(agi::fs::path) {
	if (!context->translationProject->HasSanaeBinding()) {
		recovery_state.ClearBinding();
		recovery_baseline_binding_key.clear();
		line_repeats.clear();
		AnnounceChanged(SanaeProjectChange::Binding);
		return;
	}
	auto const& binding = context->translationProject->GetSanaeBinding();
	LoadSnapshot(binding.project_id);
	auto episode = FindEpisode(binding.episode_id);
	if (!episode || episode->IsDeleted()) {
		CloseEpisode();
		return;
	}
	LoadDrafts();
	auto source_path = FilePath(binding.project_id, binding.source_file_id);
	if (agi::fs::FileExists(source_path)) context->translationProject->LoadSource(source_path);
	RebuildMemory();
	RebuildRepeatCache();
	ResetRecoveryBinding();
	AnnounceChanged(SanaeProjectChange::Binding);
}

void SanaeProjectManager::OnTranslationProjectChanged(AssDialogue const *line) {
	// Line-specific TranslationProject notifications are review-status changes;
	// they do not alter ENSUB alignment and must stay cheap during translation.
	if (HasOpenEpisode() && !line) RebuildRepeatCache();
}

void SanaeProjectManager::OnAssCommit(int, AssDialogue const *) {
	recovery_state.NoteDocumentChange();
	// Retain a key only while retrying the byte-for-byte same Finalize. Any
	// production ASS mutation must start a new idempotent operation.
	if (pending_finalize_key.empty()) return;
	pending_finalize_key.clear();
	try { SaveDrafts(); }
	catch (...) { }
}

void SanaeProjectManager::RebuildMemory() {
	memory.clear();
	exact_memory.clear();
	exact_russian_memory.clear();
	if (active_project.id.empty()) return;
	std::string current_episode = ActiveEpisodeId();
	auto current = FindEpisode(current_episode);
	auto append_entries = [&](std::vector<MemoryEntry> const& entries) {
		for (auto const& entry : entries) {
			exact_memory[entry.normalized_source].push_back(memory.size());
			if (!entry.normalized_russian.empty())
				exact_russian_memory[entry.normalized_russian].push_back(memory.size());
			memory.push_back(entry);
		}
	};

	std::vector<SanaeEpisodeInfo const *> ordered_episodes;
	ordered_episodes.reserve(episodes.size());
	for (auto const& episode : episodes)
		if (!episode.IsDeleted()) ordered_episodes.push_back(&episode);
	std::sort(ordered_episodes.begin(), ordered_episodes.end(), [](auto left, auto right) {
		return std::tie(left->sort_order, left->episode_code) < std::tie(right->sort_order, right->episode_code);
	});
	for (auto episode_ptr : ordered_episodes) {
		auto const& episode = *episode_ptr;
		if (episode.id == current_episode || episode.current_finalized_revision_id.empty()) continue;
		if (current && std::tie(episode.sort_order, episode.episode_code)
			>= std::tie(current->sort_order, current->episode_code)) continue;
		auto revision = std::find_if(finalized_revisions.begin(), finalized_revisions.end(), [&](auto const& value) {
			return value.id == episode.current_finalized_revision_id;
		});
		if (revision == finalized_revisions.end()) continue;
		auto source_path = FilePath(active_project.id, revision->source_file_id);
		auto rusub_path = FilePath(active_project.id, revision->compact_rusub_file_id);
		if (!agi::fs::FileExists(source_path) || !agi::fs::FileExists(rusub_path)) continue;
		auto source_file = FindFile(revision->source_file_id);
		auto rusub_file = FindFile(revision->compact_rusub_file_id);
		auto cache_key = active_project.id + ":" + episode.id + ":" + revision->id + ":"
			+ (source_file ? source_file->sha256 : revision->source_file_id) + ":"
			+ (rusub_file ? rusub_file->sha256 : revision->compact_rusub_file_id);
		if (auto cached = parsed_memory_cache.find(cache_key); cached != parsed_memory_cache.end()) {
			append_entries(cached->second);
			continue;
		}

		try {
			auto source = load_ass(context, source_path);
			auto rusub = load_ass(context, rusub_path);
			std::vector<MemoryEntry> parsed_entries;
			std::unordered_map<std::uint64_t, std::vector<AssDialogue const *>> exact;
			for (auto const& line : rusub->Events)
				if (!line.Comment && !has_drawing(line) && !visible_text(line).empty())
					exact[timing_key(line.Start, line.End)].push_back(&line);

			for (auto const& line : source->Events) {
				if (line.Comment || has_drawing(line)) continue;
				auto source_text = visible_text(line);
				auto normalized = SanaeNormalizeRepeatSource(line.Text.get());
				if (normalized.empty()) continue;
				std::string russian;
				if (auto match = exact.find(timing_key(line.Start, line.End)); match != exact.end()
					&& !match->second.empty()) russian = visible_text(*match->second.front());
				if (russian.empty()) {
					int best_overlap = 0;
					for (auto const& target : rusub->Events) {
						int overlap = std::min<int>(line.End, target.End) - std::max<int>(line.Start, target.Start);
						auto target_text = visible_text(target);
						if (overlap > best_overlap && !target.Comment && !target_text.empty() && !has_drawing(target)) {
							best_overlap = overlap;
							russian = std::move(target_text);
						}
					}
				}
				auto normalized_russian = SanaeNormalizeSource(russian);
				auto search_source = SanaeNormalizeSearchText(source_text);
				auto search_russian = SanaeNormalizeSearchText(russian);
				parsed_entries.push_back({std::move(normalized), std::move(normalized_russian),
					std::move(search_source), std::move(search_russian),
					std::move(source_text), std::move(russian),
					episode.episode_code, static_cast<int>(line.Start), static_cast<int>(line.End)});
			}
			append_entries(parsed_entries);
			parsed_memory_cache.emplace(std::move(cache_key), std::move(parsed_entries));
		}
		catch (...) {
			// One broken local historical file must not disable the rest of the
			// project memory. A later Sync can restore it from the server.
		}
	}
}

void SanaeProjectManager::RebuildRepeatCache() {
	line_repeats.clear();
	if (!HasOpenEpisode() || !OPT_GET("Sanae/Project/Source Repeat/Enabled")->GetBool()) {
		AnnounceChanged(SanaeProjectChange::Repeats);
		return;
	}
	double threshold = std::clamp(OPT_GET("Sanae/Project/Source Repeat/Similar Threshold")->GetDouble(), 0.80, 1.0);
	std::unordered_map<unsigned char, std::vector<size_t>> buckets;
	std::unordered_map<std::string, std::vector<size_t>> fragment_buckets;
	for (size_t i = 0; i < memory.size(); ++i)
		if (!memory[i].normalized_source.empty()) {
			buckets[static_cast<unsigned char>(memory[i].normalized_source.front())].push_back(i);
			std::unordered_set<std::string> emitted;
			for (auto& key : repeat_fragment_keys(memory[i].normalized_source))
				if (emitted.insert(key).second) fragment_buckets[std::move(key)].push_back(i);
		}
	std::unordered_map<std::string, SanaeRepeatMatch> calculated;

	for (auto const& line : context->ass->Events) {
		auto source = context->translationProject->SourceText(&line, " ");
		if (source.empty()) source = context->translationProject->SourceDisplayTextCached(&line);
		auto normalized = SanaeNormalizeRepeatSource(source);
		if (normalized.empty()) continue;
		auto ready = calculated.find(normalized);
		if (ready != calculated.end()) {
			if (ready->second.kind != SanaeRepeatKind::None) line_repeats.emplace(line.Id, ready->second);
			continue;
		}

		SanaeRepeatMatch result;
		if (auto exact = exact_memory.find(normalized); exact != exact_memory.end() && !exact->second.empty()) {
			auto const& found = memory[exact->second.back()];
			result = {SanaeRepeatKind::Exact, 1.0, found.source, found.russian,
				found.episode_code, found.start, found.end};
		}
		else {
			std::unordered_set<size_t> fragment_candidates;
			for (auto const& key : repeat_fragment_keys(normalized)) {
				auto bucket = fragment_buckets.find(key);
				if (bucket == fragment_buckets.end()) continue;
				fragment_candidates.insert(bucket->second.begin(), bucket->second.end());
			}
			for (auto it = memory.rbegin(); it != memory.rend(); ++it) {
				auto index = static_cast<size_t>(std::distance(it, memory.rend()) - 1);
				if (!fragment_candidates.count(index)) continue;
				if (!SanaeSourceFragmentMatch(source, it->source)) continue;
				result = {SanaeRepeatKind::Fragment, 1.0, it->source, it->russian,
					it->episode_code, it->start, it->end};
				break;
			}
		}
		if (result.kind == SanaeRepeatKind::None && threshold < 1.0 && informative_for_similar(normalized)) {
			double best = threshold;
			size_t best_index = std::numeric_limits<size_t>::max();
			auto bucket = buckets.find(static_cast<unsigned char>(normalized.front()));
			if (bucket != buckets.end()) {
				for (size_t index : bucket->second) {
					double score = SanaeSourceSimilarity(normalized, memory[index].normalized_source, threshold);
					if (score >= best) { best = score; best_index = index; }
				}
			}
			if (best_index != std::numeric_limits<size_t>::max()) {
				auto const& found = memory[best_index];
				result = {SanaeRepeatKind::Similar, best, found.source, found.russian,
					found.episode_code, found.start, found.end};
			}
		}
		calculated.emplace(normalized, result);
		if (result.kind != SanaeRepeatKind::None) line_repeats.emplace(line.Id, std::move(result));
	}
	AnnounceChanged(SanaeProjectChange::Repeats);
}

SanaeRepeatMatch const *SanaeProjectManager::RepeatFor(AssDialogue const *line) const {
	if (!line || !HasOpenEpisode()) return nullptr;
	auto found = line_repeats.find(line->Id);
	return found == line_repeats.end() ? nullptr : &found->second;
}

std::vector<SanaeRepeatMatch> SanaeProjectManager::RepeatsFor(AssDialogue const *line) const {
	std::vector<SanaeRepeatMatch> result;
	if (!line || !HasOpenEpisode()) return result;
	auto source = context->translationProject->SourceText(line, " ");
	if (source.empty()) source = context->translationProject->SourceDisplayTextCached(line);
	auto normalized = SanaeNormalizeRepeatSource(source);
	if (normalized.empty()) return result;
	if (auto exact = exact_memory.find(normalized); exact != exact_memory.end()) {
		result.reserve(exact->second.size());
		for (auto index : exact->second) {
			if (index >= memory.size()) continue;
			auto const& found = memory[index];
			result.push_back({SanaeRepeatKind::Exact, 1.0, found.source, found.russian,
				found.episode_code, found.start, found.end});
		}
		return result;
	}
	if (auto fallback = RepeatFor(line); fallback
		&& (fallback->kind == SanaeRepeatKind::Fragment || fallback->kind == SanaeRepeatKind::Similar))
		result.push_back(*fallback);
	return result;
}

std::vector<SanaeRepeatMatch> SanaeProjectManager::SearchMemory(SanaeSearchOptions const& options) const {
	std::vector<SanaeRepeatMatch> result;
	auto normalized = SanaeNormalizeSearchText(options.query);
	if (normalized.empty()) return result;
	auto accepts_episode = [&](MemoryEntry const& entry) {
		return options.episode_code.empty() || entry.episode_code == options.episode_code;
	};
	std::unordered_set<std::size_t> emitted;
	auto emit = [&](std::size_t index, SanaeRepeatKind kind, double score = 1.0) {
		if (index >= memory.size() || !accepts_episode(memory[index]) || !emitted.insert(index).second) return;
		auto const& entry = memory[index];
		result.push_back({kind, score, entry.source, entry.russian,
			entry.episode_code, entry.start, entry.end});
	};

	// Full-line exact queries are indexed and avoid a corpus scan.
	auto source_key = SanaeNormalizeSource(options.query);
	if (options.scope != SanaeSearchScope::Russian) {
		if (auto found = exact_memory.find(source_key); found != exact_memory.end())
			for (auto index : found->second) emit(index, SanaeRepeatKind::Exact);
	}
	if (options.scope != SanaeSearchScope::English) {
		if (auto found = exact_russian_memory.find(source_key); found != exact_russian_memory.end())
			for (auto index : found->second) emit(index, SanaeRepeatKind::Exact);
	}
	auto query_tokens = SanaeSearchTokens(options.query);
	auto fuzzy_matches = [&](std::string const& text) {
		if (!options.fuzzy_word_forms || query_tokens.empty()) return false;
		auto tokens = SanaeSearchTokens(text);
		return std::all_of(query_tokens.begin(), query_tokens.end(), [&](auto const& query_token) {
			return std::any_of(tokens.begin(), tokens.end(), [&](auto const& token) {
				return SanaeSearchTokenMatches(query_token, token);
			});
		});
	};
	for (std::size_t index = 0; index < memory.size() && result.size() < options.limit; ++index) {
		auto const& entry = memory[index];
		if (!accepts_episode(entry) || emitted.count(index)) continue;
		bool english_exact = false, russian_exact = false;
		if (options.scope != SanaeSearchScope::Russian)
			english_exact = entry.search_source.find(normalized) != std::string::npos;
		if (options.scope != SanaeSearchScope::English)
			russian_exact = entry.search_russian.find(normalized) != std::string::npos;
		if (english_exact || russian_exact) {
			emit(index, SanaeRepeatKind::Exact);
			continue;
		}
		bool fuzzy = (options.scope != SanaeSearchScope::Russian && fuzzy_matches(entry.source))
			|| (options.scope != SanaeSearchScope::English && fuzzy_matches(entry.russian));
		if (fuzzy) emit(index, SanaeRepeatKind::Similar);
	}

	// The synchronized memory intentionally excludes the open episode from
	// repeat history. Project search, however, is expected to search that open
	// working ASS too, so add it dynamically without polluting repeat indexes.
	if (result.size() < options.limit && HasOpenEpisode()) {
		auto current = ActiveEpisode();
		if (current && (options.episode_code.empty() || options.episode_code == current->episode_code)) {
			for (auto const& line : context->ass->Events) {
				if (result.size() >= options.limit || line.Comment || has_drawing(line)) continue;
				auto source = context->translationProject->SourceDisplayTextCached(&line);
				auto russian = visible_text(line);
				bool english_exact = options.scope != SanaeSearchScope::Russian
					&& SanaeNormalizeSearchText(source).find(normalized) != std::string::npos;
				bool russian_exact = options.scope != SanaeSearchScope::English
					&& SanaeNormalizeSearchText(russian).find(normalized) != std::string::npos;
				SanaeRepeatKind kind = SanaeRepeatKind::None;
				if (english_exact || russian_exact) kind = SanaeRepeatKind::Exact;
				else if ((options.scope != SanaeSearchScope::Russian && fuzzy_matches(source))
					|| (options.scope != SanaeSearchScope::English && fuzzy_matches(russian)))
					kind = SanaeRepeatKind::Similar;
				if (kind != SanaeRepeatKind::None)
					result.push_back({kind, 1.0, std::move(source), std::move(russian),
						current->episode_code, static_cast<int>(line.Start), static_cast<int>(line.End)});
			}
		}
	}
	return result;
}

std::vector<SanaeTerminologyCandidate> SanaeProjectManager::GenerateCandidates() const {
	struct Count {
		std::string display;
		int occurrences = 0;
		int previous_occurrences = 0;
		bool capitalized = false;
		std::unordered_set<std::string> episode_codes;
		std::vector<SanaeTerminologyCandidate::Context> contexts;
	};
	std::unordered_map<std::string, Count> counts;
	// This is only a safety net for installations without an English Hunspell
	// dictionary. The normal path below asks Aegisub's own English dictionary,
	// including its user dictionary, and rejects ordinary known single words.
	static std::unordered_set<std::string> const stopwords = {
		"a", "after", "all", "an", "and", "are", "as", "at", "be", "been", "but", "by", "can",
		"can't", "come", "could", "day", "did", "do", "does", "for", "from", "get", "got", "had",
		"has", "have", "he", "her", "here", "him", "his", "i", "if", "in", "is", "it", "its", "just",
		"know", "like", "me", "my", "no", "not", "of", "on", "one", "or", "our", "she", "should",
		"so", "someone", "something", "that", "the", "their", "them", "there", "they", "this", "time",
		"to", "was", "we", "well", "were", "what", "when", "where", "who", "why", "will", "with",
		"would", "you", "your", "yes", "yeah", "okay", "ok", "thanks", "thank"
	};
	auto useful_phrase = [&](std::vector<std::string> const& words, size_t begin, size_t length) {
		for (size_t offset = 0; offset < length; ++offset)
			if (!stopwords.count(SanaeNormalizeSource(words[begin + offset]))) return true;
		return false;
	};
	auto phrase_at = [](std::vector<std::string> const& words, size_t begin, size_t length) {
		std::string result;
		for (size_t offset = 0; offset < length; ++offset) {
			if (!result.empty()) result.push_back(' ');
			result += words[begin + offset];
		}
		return result;
	};
	auto name_like_capitalization = [](std::vector<std::string> const& words, size_t begin, size_t length) {
		for (size_t offset = 0; offset < length; ++offset) {
			auto const& word = words[begin + offset];
			if (word.empty() || !std::isupper(static_cast<unsigned char>(word.front()))) continue;
			// Capitalization of only the first token can just be sentence start.
			if (begin + offset > 0 || offset > 0) return true;
		}
		return false;
	};
	auto collect = [&](std::string const& source, std::string const& russian,
		std::string const& episode_code, int start, int end, bool current_episode) {
		auto words = english_words(source);
		std::unordered_set<std::string> context_added;
		for (size_t i = 0; i < words.size(); ++i) {
			for (size_t length = 1; length <= 4 && i + length <= words.size(); ++length) {
				if (!useful_phrase(words, i, length)) continue;
				auto display = phrase_at(words, i, length);
				auto normalized = SanaeNormalizeSource(display);
				auto& count = counts[normalized];
				if (count.display.empty() || (name_like_capitalization(words, i, length)
					&& !std::isupper(static_cast<unsigned char>(count.display.front()))))
					count.display = display;
				if (current_episode) ++count.occurrences;
				else ++count.previous_occurrences;
				count.episode_codes.insert(episode_code);
				count.capitalized = count.capitalized || name_like_capitalization(words, i, length);
				if (context_added.insert(normalized).second && count.contexts.size() < 8)
					count.contexts.push_back({episode_code, start, end, source, russian, current_episode});
			}
		}
	};
	for (auto const& entry : memory)
		collect(entry.source, entry.russian, entry.episode_code, entry.start, entry.end, false);
	for (auto const& line : context->ass->Events) {
		auto source = context->translationProject->SourceDisplayTextCached(&line);
		auto episode = ActiveEpisode();
		collect(source, visible_text(line), episode ? episode->episode_code : std::string("current"),
			static_cast<int>(line.Start), static_cast<int>(line.End), true);
	}

	std::unordered_set<std::string> blocked;
	for (auto const& term : terminology) if (!term.deleted)
		blocked.insert(term.english_normalized.empty() ? SanaeNormalizeSource(term.english) : term.english_normalized);
	for (auto const& term : terminology_drafts) blocked.insert(SanaeNormalizeSource(term.english));
	for (auto const& ignored : ignored_candidates)
		if (!ignored.deleted && (ignored.scope == "project" || ignored.episode_id == ActiveEpisodeId()))
			blocked.insert(ignored.normalized_text.empty() ? SanaeNormalizeSource(ignored.text) : ignored.normalized_text);
	for (auto const& ignored : ignore_drafts) blocked.insert(SanaeNormalizeSource(ignored.text));

	std::unique_ptr<agi::SpellChecker> dictionary = SpellCheckerFactory::GetSpellChecker();
	std::string english_dictionary;
	if (dictionary) {
		auto languages = dictionary->GetLanguageList();
		auto prefer = [&](std::string const& code) {
			return std::find(languages.begin(), languages.end(), code) != languages.end();
		};
		if (prefer("en_US")) english_dictionary = "en_US";
		else if (prefer("en_GB")) english_dictionary = "en_GB";
		else {
			auto found = std::find_if(languages.begin(), languages.end(), [](auto const& code) {
				return lower_ascii(code).rfind("en", 0) == 0;
			});
			if (found != languages.end()) english_dictionary = *found;
		}
		if (english_dictionary.empty()) dictionary.reset();
		else if (lower_ascii(OPT_GET("Tool/Spell Checker/Language")->GetString())
			!= lower_ascii(english_dictionary))
			dictionary = SpellCheckerFactory::GetSpellChecker(english_dictionary);
	}
	std::unordered_map<std::string, bool> dictionary_cache;
	auto known_english_word = [&](std::string word) {
		if (!dictionary) return false;
		auto key = lower_ascii(word);
		if (auto found = dictionary_cache.find(key); found != dictionary_cache.end()) return found->second;
		bool known = dictionary->CheckWord(word);
		if (!known && word != key) known = dictionary->CheckWord(key);
		dictionary_cache.emplace(std::move(key), known);
		return known;
	};

	std::vector<SanaeTerminologyCandidate> raw;
	for (auto const& [normalized, count] : counts) {
		if (blocked.count(normalized) || count.display.size() < 3 || !count.occurrences) continue;
		auto words = english_words(count.display);
		if (words.empty()) continue;
		bool phrase = words.size() > 1;
		int total = count.occurrences + count.previous_occurrences;
		bool missing = false;
		bool unknown_component = false;
		if (dictionary) {
			for (auto const& word : words)
				if (!known_english_word(word)) { unknown_component = true; break; }
			missing = !phrase && unknown_component;
		}
		else if (!phrase) {
			missing = !stopwords.count(normalized);
			unknown_component = missing;
		}

		std::string reason;
		if (!phrase) {
			// The main noise rule: a normal English dictionary word is not a
			// terminology proposal. User-dictionary words are accepted by Hunspell
			// too, so they disappear here as requested.
			if (dictionary && !missing) continue;
			if (!dictionary && stopwords.count(normalized)) continue;
			if (total < 2 && !count.capitalized) continue;
			reason = "unknown_word";
		}
		else {
			bool repeated_across_project = count.episode_codes.size() >= 2 && total >= 3;
			bool strong_name_phrase = count.capitalized && total >= 2;
			bool phrase_with_unknown = unknown_component && total >= 2;
			if (!strong_name_phrase && !phrase_with_unknown && !repeated_across_project) continue;
			reason = strong_name_phrase ? "name_phrase"
				: (phrase_with_unknown ? "unknown_phrase" : "repeated_phrase");
		}

		int score = count.occurrences * 2 + count.previous_occurrences
			+ (count.capitalized ? 3 : 0) + (missing ? 5 : 0)
			+ (unknown_component && phrase ? 3 : 0)
			+ (count.episode_codes.size() >= 2 ? 3 : 0) + (phrase ? 1 : 0);
		SanaeTerminologyCandidate candidate;
		candidate.english = count.display;
		candidate.score = score;
		candidate.occurrences = count.occurrences;
		candidate.previous_occurrences = count.previous_occurrences;
		candidate.project_episode_count = static_cast<int>(count.episode_codes.size());
		candidate.capitalized = count.capitalized;
		candidate.in_previous_episodes = count.previous_occurrences > 0;
		candidate.missing_from_dictionary = missing;
		candidate.reason = std::move(reason);
		candidate.contexts = count.contexts;
		raw.push_back(std::move(candidate));
	}

	// Prefer maximal phrases when a shorter n-gram occurs in exactly the same
	// places. This removes floods such as "Old / The Old / Old Country / ..."
	// without hiding independently-used names which also occur on their own.
	auto token_sequence_contains = [](std::string const& longer, std::string const& shorter) {
		auto a = SanaeSearchTokens(longer), b = SanaeSearchTokens(shorter);
		if (b.empty() || b.size() >= a.size()) return false;
		for (std::size_t i = 0; i + b.size() <= a.size(); ++i)
			if (std::equal(b.begin(), b.end(), a.begin() + static_cast<std::ptrdiff_t>(i))) return true;
		return false;
	};
	std::sort(raw.begin(), raw.end(), [](auto const& a, auto const& b) {
		auto aw = SanaeSearchTokens(a.english).size(), bw = SanaeSearchTokens(b.english).size();
		return std::tie(aw, a.score, a.occurrences, a.english)
			> std::tie(bw, b.score, b.occurrences, b.english);
	});
	std::vector<SanaeTerminologyCandidate> result;
	for (auto& candidate : raw) {
		bool redundant = std::any_of(result.begin(), result.end(), [&](auto const& longer) {
			return candidate.occurrences == longer.occurrences
				&& candidate.previous_occurrences == longer.previous_occurrences
				&& candidate.project_episode_count == longer.project_episode_count
				&& token_sequence_contains(longer.english, candidate.english);
		});
		if (!redundant) result.push_back(std::move(candidate));
	}
	std::sort(result.begin(), result.end(), [](auto const& a, auto const& b) {
		return std::tie(a.score, a.occurrences, a.previous_occurrences, a.english)
			> std::tie(b.score, b.occurrences, b.previous_occurrences, b.english);
	});
	if (result.size() > 200) result.resize(200);
	return result;
}

std::vector<SanaeReviewIssue> SanaeProjectManager::TerminologyConsistencyIssues() const {
	std::vector<SanaeTerminologyDraft> active_terms;
	active_terms.reserve(terminology.size() + terminology_drafts.size());
	for (auto const& term : terminology) {
		auto draft = std::find_if(terminology_drafts.begin(), terminology_drafts.end(),
			[&](auto const& value) { return value.term_id == term.id && !term.id.empty(); });
		if (draft != terminology_drafts.end()) {
			if (draft->operation == "delete") continue;
			if (draft->operation == "update") {
				active_terms.push_back(*draft);
				continue;
			}
			if (draft->operation == "restore") {
				active_terms.push_back({term.english, term.russian, term.note});
				continue;
			}
		}
		if (!term.deleted) active_terms.push_back({term.english, term.russian, term.note});
	}
	for (auto const& draft : terminology_drafts)
		if (draft.operation == "create") active_terms.push_back(draft);
	std::vector<SanaeReviewIssue> issues;
	for (auto const& term : active_terms) {
		if (term.english.empty() || term.russian.empty()) continue;
		auto accepted = SanaeNormalizeSearchText(term.russian);
		auto accepted_tokens = SanaeSearchTokens(term.russian);
		auto variant = [&](std::string const& translated) {
			if (contains_normalized(translated, term.russian)) return accepted;
			if (accepted_tokens.size() != 1) return std::string();
			auto tokens = SanaeSearchTokens(translated);
			double best = 0.72;
			std::string result;
			for (auto const& token : tokens) {
				auto score = SanaeSourceSimilarity(accepted_tokens.front(), token, best);
				if (score >= best) { best = score; result = token; }
			}
			return result;
		};
		std::map<std::string, int> current_counts, project_counts;
		for (auto const& entry : memory) {
			if (!contains_normalized(entry.source, term.english)) continue;
			auto value = variant(entry.russian);
			if (!value.empty()) ++project_counts[value];
		}
		struct CurrentUse { AssDialogue *line; std::string actual; };
		std::vector<CurrentUse> current_uses;
		for (auto& line : context->ass->Events) {
			auto source = context->translationProject->SourceDisplayTextCached(&line);
			if (!contains_normalized(source, term.english)) continue;
			auto actual = variant(visible_text(line));
			if (!actual.empty()) {
				++current_counts[actual];
				++project_counts[actual];
			}
			current_uses.push_back({&line, std::move(actual)});
		}
		auto distribution = [&](std::map<std::string, int> const& counts) {
			std::ostringstream text;
			bool first = true;
			for (auto const& [value, count] : counts) {
				if (!first) text << ", ";
				first = false;
				text << value << " ×" << count;
			}
			return text.str();
		};
		for (auto const& use : current_uses) {
			if (use.actual == accepted) continue;
			std::ostringstream detail;
			detail << "Accepted: " << term.russian << ". In this line: "
				<< (use.actual.empty() ? "not detected" : use.actual) << ".";
			auto current_distribution = distribution(current_counts);
			auto project_distribution = distribution(project_counts);
			if (!current_distribution.empty()) detail << " Current episode: " << current_distribution << ".";
			if (!project_distribution.empty()) detail << " Project: " << project_distribution << ".";
			issues.push_back({term.english + " → " + term.russian, detail.str(), use.line,
				use.actual, use.actual.empty() ? std::string() : term.russian});
		}
	}
	return issues;
}

std::vector<SanaeReviewIssue> SanaeProjectManager::InternalConsistencyIssues() const {
	std::unordered_map<std::string, std::map<std::string, std::vector<AssDialogue *>>> groups;
	struct WordUse {
		std::vector<std::uint32_t> letters;
		std::string display;
		std::vector<AssDialogue *> lines;
	};
	std::map<std::string, WordUse> word_uses;
	for (auto& line : context->ass->Events) {
		auto source = SanaeNormalizeSource(context->translationProject->SourceDisplayTextCached(&line));
		auto russian = SanaeNormalizeSource(visible_text(line));
		if (!source.empty() && !russian.empty()) groups[source][russian].push_back(&line);
		for (auto& word : russian_words(visible_text(line))) {
			auto& use = word_uses[word.display];
			if (use.display.empty()) {
				use.letters = std::move(word.letters);
				use.display = std::move(word.display);
			}
			use.lines.push_back(&line);
		}
	}
	std::vector<SanaeReviewIssue> issues;
	for (auto const& [source, variants] : groups) {
		if (variants.size() < 2 || !informative_for_internal_consistency(source)) continue;
		for (auto const& [russian, lines] : variants)
			for (auto line : lines)
				issues.push_back({"Different translations of the same ENSUB source", source + " → " + russian, line});
	}
	// Conservative typo hint: only one-off, internal one-character variants
	// of a word seen at least five times are reported. Matching first and last
	// letters avoids treating ordinary Russian inflections as spelling errors.
	for (auto const& [key, minor] : word_uses) {
		(void)key;
		if (minor.lines.size() != 1) continue;
		WordUse const *best = nullptr;
		for (auto const& [candidate_key, major] : word_uses) {
			(void)candidate_key;
			if (major.lines.size() < 5 || !edit_distance_one(minor.letters, major.letters)) continue;
			if (!best || major.lines.size() > best->lines.size()) best = &major;
		}
		if (!best) continue;
		issues.push_back({"Possible inconsistent spelling",
			minor.display + " ×1 / " + best->display + " ×" + std::to_string(best->lines.size()),
			minor.lines.front()});
	}
	return issues;
}

std::vector<SanaeReviewIssue> SanaeProjectManager::SourceRepeatIssues() const {
	std::vector<SanaeReviewIssue> issues;
	for (auto& line : context->ass->Events) {
		auto repeat = RepeatFor(&line);
		if (!repeat) continue;
		std::ostringstream detail;
		detail << repeat->episode_code << " · " << agi::Time(repeat->start).GetAssFormatted(true)
			<< ": " << repeat->source;
		if (!repeat->russian.empty()) detail << " → " << repeat->russian;
		char const *title = repeat->kind == SanaeRepeatKind::Exact ? "Exact ENSUB repeat"
			: repeat->kind == SanaeRepeatKind::Fragment ? "ENSUB fragment repeat" : "Similar ENSUB repeat";
		issues.push_back({title, detail.str(), &line});
	}
	return issues;
}

void SanaeProjectManager::ResetFinalizeKey() {
	pending_finalize_key.clear();
}

void SanaeProjectManager::QueueTerminology(SanaeTerminologyDraft draft) {
	if (SanaeNormalizeSource(draft.english).empty() || draft.russian.empty())
		throw std::invalid_argument("Terminology requires English and Russian text");
	draft.operation = "create";
	draft.term_id.clear();
	draft.base_version = 0;
	auto normalized = SanaeNormalizeSource(draft.english);
	for (auto const& term : terminology)
		if (!term.deleted && (term.english_normalized.empty()
			? SanaeNormalizeSource(term.english) : term.english_normalized) == normalized)
			throw std::invalid_argument("This English term already exists in project terminology");
	for (auto& existing : terminology_drafts) {
		if (existing.operation == "create" && SanaeNormalizeSource(existing.english) == normalized) {
			existing = std::move(draft);
			ResetFinalizeKey(); SaveDrafts(); AnnounceChanged(SanaeProjectChange::Draft); return;
		}
	}
	terminology_drafts.push_back(std::move(draft));
	ResetFinalizeKey(); SaveDrafts(); AnnounceChanged(SanaeProjectChange::Draft);
}

void SanaeProjectManager::QueueTerminologyUpdate(std::string const& term_id, int base_version,
	std::string english, std::string russian, std::string note)
{
	auto term = std::find_if(terminology.begin(), terminology.end(),
		[&](auto const& value) { return value.id == term_id; });
	if (term == terminology.end()) throw std::invalid_argument("Terminology entry is not present in the local project snapshot");
	if (base_version < 1) throw std::invalid_argument("Terminology update requires a valid base version");
	if (SanaeNormalizeSource(english).empty() || russian.empty())
		throw std::invalid_argument("Terminology requires English and Russian text");
	auto normalized = SanaeNormalizeSource(english);
	for (auto const& value : terminology) {
		if (value.id == term_id || value.deleted) continue;
		auto existing = value.english_normalized.empty()
			? SanaeNormalizeSource(value.english) : value.english_normalized;
		if (existing == normalized)
			throw std::invalid_argument("Another project term already uses this English text");
	}
	SanaeTerminologyDraft draft;
	draft.english = std::move(english);
	draft.russian = std::move(russian);
	draft.note = std::move(note);
	draft.operation = "update";
	draft.term_id = term_id;
	draft.base_version = base_version;
	for (auto& existing : terminology_drafts) {
		if (!existing.term_id.empty() && existing.term_id == term_id) {
			existing = std::move(draft);
			ResetFinalizeKey(); SaveDrafts(); AnnounceChanged(SanaeProjectChange::Draft); return;
		}
	}
	terminology_drafts.push_back(std::move(draft));
	ResetFinalizeKey(); SaveDrafts(); AnnounceChanged(SanaeProjectChange::Draft);
}

void SanaeProjectManager::QueueTerminologyDelete(std::string const& term_id, int base_version) {
	auto term = std::find_if(terminology.begin(), terminology.end(),
		[&](auto const& value) { return value.id == term_id; });
	if (term == terminology.end()) throw std::invalid_argument("Terminology entry is not present in the local project snapshot");
	if (base_version < 1) throw std::invalid_argument("Terminology delete requires a valid base version");
	SanaeTerminologyDraft draft;
	draft.english = term->english; draft.russian = term->russian; draft.note = term->note;
	draft.operation = "delete"; draft.term_id = term_id; draft.base_version = base_version;
	for (auto& existing : terminology_drafts) {
		if (!existing.term_id.empty() && existing.term_id == term_id) {
			existing = std::move(draft);
			ResetFinalizeKey(); SaveDrafts(); AnnounceChanged(SanaeProjectChange::Draft); return;
		}
	}
	terminology_drafts.push_back(std::move(draft));
	ResetFinalizeKey(); SaveDrafts(); AnnounceChanged(SanaeProjectChange::Draft);
}

void SanaeProjectManager::QueueTerminologyRestore(std::string const& term_id, int base_version) {
	auto term = std::find_if(terminology.begin(), terminology.end(),
		[&](auto const& value) { return value.id == term_id; });
	if (term == terminology.end()) throw std::invalid_argument("Terminology entry is not present in the local project snapshot");
	if (base_version < 1) throw std::invalid_argument("Terminology restore requires a valid base version");
	SanaeTerminologyDraft draft;
	draft.english = term->english; draft.russian = term->russian; draft.note = term->note;
	draft.operation = "restore"; draft.term_id = term_id; draft.base_version = base_version;
	for (auto& existing : terminology_drafts) {
		if (!existing.term_id.empty() && existing.term_id == term_id) {
			existing = std::move(draft);
			ResetFinalizeKey(); SaveDrafts(); AnnounceChanged(SanaeProjectChange::Draft); return;
		}
	}
	terminology_drafts.push_back(std::move(draft));
	ResetFinalizeKey(); SaveDrafts(); AnnounceChanged(SanaeProjectChange::Draft);
}

void SanaeProjectManager::RemoveTerminologyDraft(std::size_t index) {
	if (index >= terminology_drafts.size()) return;
	terminology_drafts.erase(terminology_drafts.begin() + index);
	ResetFinalizeKey(); SaveDrafts(); AnnounceChanged(SanaeProjectChange::Draft);
}

void SanaeProjectManager::QueueIgnore(SanaeIgnoreDraft draft) {
	if (draft.scope != "project" && draft.scope != "episode")
		throw std::invalid_argument("Ignore scope must be project or episode");
	if (SanaeNormalizeSource(draft.text).empty()) throw std::invalid_argument("Ignore text cannot be empty");
	for (auto const& existing : ignore_drafts)
		if (existing.scope == draft.scope && SanaeNormalizeSource(existing.text) == SanaeNormalizeSource(draft.text)) return;
	ignore_drafts.push_back(std::move(draft));
	ResetFinalizeKey(); SaveDrafts(); AnnounceChanged(SanaeProjectChange::Draft);
}

void SanaeProjectManager::RemoveIgnoreDraft(std::size_t index) {
	if (index >= ignore_drafts.size()) return;
	ignore_drafts.erase(ignore_drafts.begin() + index);
	ResetFinalizeKey(); SaveDrafts(); AnnounceChanged(SanaeProjectChange::Draft);
}

void SanaeProjectManager::LoadDrafts() {
	terminology_drafts.clear(); ignore_drafts.clear(); pending_finalize_key.clear();
	if (!HasOpenEpisode() || active_project.id.empty()) return;
	auto path = DraftPath(active_project.id, ActiveEpisodeId());
	if (!agi::fs::FileExists(path)) return;
	try {
		auto root = parse_json_object(read_file(path));
		pending_finalize_key = get<std::string>(root, "pending_finalize_key");
		if (auto values = array_at(root, "terminology")) for (auto const& value : *values) {
			auto const& item = static_cast<json::Object const&>(value);
			SanaeTerminologyDraft draft;
			draft.english = get<std::string>(item, "english");
			draft.russian = get<std::string>(item, "russian");
			draft.note = get<std::string>(item, "note");
			draft.operation = get<std::string>(item, "operation", "create");
			draft.term_id = get<std::string>(item, "term_id");
			draft.base_version = get<int>(item, "base_version", 0);
			terminology_drafts.push_back(std::move(draft));
		}
		if (auto values = array_at(root, "ignores")) for (auto const& value : *values) {
			auto const& item = static_cast<json::Object const&>(value);
			ignore_drafts.push_back({get<std::string>(item, "scope", "project"),
				get<std::string>(item, "text"), get<std::string>(item, "language", "en")});
		}
	}
	catch (...) { terminology_drafts.clear(); ignore_drafts.clear(); pending_finalize_key.clear(); }
}

void SanaeProjectManager::SaveDrafts() const {
	if (!HasOpenEpisode() || active_project.id.empty()) return;
	json::Object root;
	root["version"] = 1;
	root["pending_finalize_key"] = pending_finalize_key;
	json::Array terms;
	for (auto const& value : terminology_drafts) {
		json::Object item; item["english"] = value.english; item["russian"] = value.russian; item["note"] = value.note;
		item["operation"] = value.operation; item["term_id"] = value.term_id;
		item["base_version"] = value.base_version;
		terms.emplace_back(std::move(item));
	}
	root["terminology"] = std::move(terms);
	json::Array ignores;
	for (auto const& value : ignore_drafts) {
		json::Object item; item["scope"] = value.scope; item["text"] = value.text; item["language"] = value.language;
		ignores.emplace_back(std::move(item));
	}
	root["ignores"] = std::move(ignores);
	write_file(DraftPath(active_project.id, ActiveEpisodeId()), write_json(root));
}

SanaeCompactStats SanaeProjectManager::Finalize() {
	if (!HasOpenEpisode()) throw std::runtime_error("No Sanae episode is open");
	last_finalize_warning.clear();
	auto const binding = context->translationProject->GetSanaeBinding();
	bool terminology_changed = !terminology_drafts.empty();
	if (terminology_drafts.size() > 1000) throw std::runtime_error("Finalize contains more than 1000 terminology operations");

	SanaeCompactStats stats;
	auto compact = BuildSanaeCompactRusub(*context->ass, &stats);
	auto pending_path = CacheRoot(active_project.id) / "pending" / (binding.episode_id + ".compact.ass");
	agi::fs::CreateDirectory(pending_path.parent_path());
	SubtitleFormat::GetWriter(pending_path)->WriteFile(&compact, pending_path,
		context->project->Timecodes(), "UTF-8");
	auto compact_data = read_file(pending_path);
	if (compact_data.size() > 16 * 1024 * 1024)
		throw std::runtime_error("Compact RUSUB exceeds the server 16 MiB limit");

	json::Object metadata;
	metadata["base_project_revision"] = binding.project_revision;
	metadata["source_file_id"] = binding.source_file_id;
	if (binding.base_finalized_revision_id.empty()) metadata["base_finalized_revision_id"] = json::Null();
	else metadata["base_finalized_revision_id"] = binding.base_finalized_revision_id;
	json::Array term_ops;
	for (auto const& draft : terminology_drafts) {
		json::Object operation;
		operation["op"] = draft.operation;
		if (draft.operation == "create") {
			operation["english"] = draft.english;
			operation["russian"] = draft.russian;
			if (draft.note.empty()) operation["note"] = json::Null(); else operation["note"] = draft.note;
		}
		else {
			operation["term_id"] = draft.term_id;
			operation["base_version"] = draft.base_version;
			if (draft.operation == "update") {
				operation["english"] = draft.english;
				operation["russian"] = draft.russian;
				if (draft.note.empty()) operation["note"] = json::Null(); else operation["note"] = draft.note;
			}
		}
		term_ops.emplace_back(std::move(operation));
	}
	metadata["terminology_ops"] = std::move(term_ops);
	json::Array ignore_ops;
	for (auto const& draft : ignore_drafts) {
		json::Object operation;
		operation["op"] = "create"; operation["scope"] = draft.scope;
		operation["text"] = draft.text; operation["language"] = draft.language;
		ignore_ops.emplace_back(std::move(operation));
	}
	metadata["ignore_ops"] = std::move(ignore_ops);
	auto metadata_json = write_json(metadata);
	if (metadata_json.size() > 1024 * 1024)
		throw std::runtime_error("Finalize metadata exceeds the server 1 MiB limit");
	if (pending_finalize_key.empty()) pending_finalize_key = new_uuid();
	SaveDrafts();
	auto response = make_api().PostMultipart("/api/v1/episodes/" + binding.episode_id + "/finalize",
		metadata_json, "compact_rusub", "compact-rusub.ass", compact_data, pending_finalize_key);
	auto root = parse_json_object(response.body);
	auto episode_object = object_at(root, "episode");
	auto file_object = object_at(root, "compact_rusub_file");
	auto revision_object = object_at(root, "finalized_revision");
	if (!episode_object || !file_object || !revision_object)
		throw std::runtime_error("Invalid Finalize response");
	auto episode = parse_episode(*episode_object);
	auto file = parse_file(*file_object);
	auto revision = parse_finalized(*revision_object);
	merge_by_id(episodes, std::vector<SanaeEpisodeInfo>{episode});
	merge_by_id(files, std::vector<SanaeEpisodeFileInfo>{file});
	merge_by_id(finalized_revisions, std::vector<SanaeFinalizedRevisionInfo>{revision});
	if (auto results = array_at(root, "terminology_results")) for (auto const& value : *results) {
		auto const& item = static_cast<json::Object const&>(value);
		if (auto term = object_at(item, "term")) merge_by_id(terminology, std::vector<SanaeTerminologyEntry>{parse_term(*term)});
	}
	if (auto results = array_at(root, "ignore_results")) for (auto const& value : *results) {
		auto const& item = static_cast<json::Object const&>(value);
		if (auto ignored = object_at(item, "ignore")) merge_by_id(ignored_candidates, std::vector<SanaeIgnoredCandidate>{parse_ignore(*ignored)});
	}
	active_project.current_revision = get<int>(root, "project_revision", active_project.current_revision);
	terminology_drafts.clear(); ignore_drafts.clear(); pending_finalize_key.clear();
	auto preserve_warning = [&](char const *step, auto action) {
		try { action(); }
		catch (std::exception const& error) {
			if (!last_finalize_warning.empty()) last_finalize_warning += "\n";
			last_finalize_warning += std::string(step) + ": " + error.what();
		}
	};
	preserve_warning("compact cache", [&] { write_file(FilePath(active_project.id, file.id), compact_data); });
	preserve_warning("translation sidecar", [&] {
		context->translationProject->UpdateSanaeFinalizeState(active_project.current_revision, revision.id);
	});
	preserve_warning("review draft", [&] { SaveDrafts(); });
	if (terminology_changed)
		preserve_warning("terminology history", [&] { RefreshTerminologyHistory(); });
	preserve_warning("project snapshot", [&] { SaveSnapshot(); });
	preserve_warning("project memory", [&] { RebuildMemory(); RebuildRepeatCache(); });
	// Finalize cleanup is server-owned. Refresh the independent recovery list;
	// do not emit a Project Sync or delete snapshots from the client.
	recovery_baseline_binding_key.clear();
	RefreshRecoveryBaselineAsync();
	AnnounceChanged(SanaeProjectChange::Cache);
	return stats;
}
