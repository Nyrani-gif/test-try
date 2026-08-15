// Copyright (c) 2026, Aegisub Sanae contributors

#include "sanae_batch_import.h"

#include <libaegisub/cajun/elements.h>
#include <libaegisub/cajun/reader.h>
#include <libaegisub/cajun/writer.h>
#include <libaegisub/io.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <limits>
#include <map>
#include <random>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <tuple>
#include <type_traits>

#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>

// windows.h maps CreateDirectory to CreateDirectoryW. Keep that macro from
// rewriting the qualified agi::fs::CreateDirectory calls below.
#undef CreateDirectory
#endif

namespace {
std::string lower_ascii(std::string value) {
	std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
		return static_cast<char>(c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c);
	});
	return value;
}

std::string trim_ascii(std::string_view value) {
	auto first = value.begin();
	auto last = value.end();
	while (first != last && std::isspace(static_cast<unsigned char>(*first))) ++first;
	while (first != last && std::isspace(static_cast<unsigned char>(*(last - 1)))) --last;
	return std::string(first, last);
}

bool numeric_code(std::string_view value) {
	if (value.empty()) return false;
	bool dot = false;
	for (char c : value) {
		if (c == '.' && !dot) dot = true;
		else if (!std::isdigit(static_cast<unsigned char>(c))) return false;
	}
	return value.front() != '.' && value.back() != '.';
}

std::string normalize_numeric(std::string value) {
	auto dot = value.find('.');
	auto integer_end = dot == std::string::npos ? value.size() : dot;
	auto first = value.find_first_not_of('0');
	if (first == std::string::npos || first >= integer_end) first = integer_end ? integer_end - 1 : 0;
	value.erase(0, first);
	if (dot != std::string::npos) {
		dot -= first;
		while (value.size() > dot + 1 && value.back() == '0') value.pop_back();
		if (value.back() == '.') value.pop_back();
	}
	return value;
}

std::vector<agi::fs::path> list_subtitle_files(agi::fs::path const& directory) {
	std::vector<agi::fs::path> result;
	if (directory.empty()) return result;
	if (!agi::fs::DirectoryExists(directory))
		throw std::runtime_error("Batch import directory does not exist: " + directory.string());

	for (auto const& name : agi::fs::DirectoryIterator(directory, "*")) {
		auto path = directory / agi::fs::path(name);
		auto extension = lower_ascii(path.extension().string());
		if (agi::fs::FileExists(path) && (extension == ".ass" || extension == ".ssa"))
			result.push_back(std::move(path));
	}
	std::sort(result.begin(), result.end(), [](auto const& left, auto const& right) {
		return lower_ascii(left.filename().string()) < lower_ascii(right.filename().string());
	});
	return result;
}

char const *state_name(SanaeBatchRowState state) {
	switch (state) {
		case SanaeBatchRowState::Pending: return "pending";
		case SanaeBatchRowState::Running: return "running";
		case SanaeBatchRowState::Succeeded: return "succeeded";
		case SanaeBatchRowState::Failed: return "failed";
		case SanaeBatchRowState::Skipped: return "skipped";
	}
	return "pending";
}

SanaeBatchRowState parse_state(std::string const& state) {
	if (state == "running") return SanaeBatchRowState::Running;
	if (state == "succeeded") return SanaeBatchRowState::Succeeded;
	if (state == "failed") return SanaeBatchRowState::Failed;
	if (state == "skipped") return SanaeBatchRowState::Skipped;
	return SanaeBatchRowState::Pending;
}

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

std::string write_json(json::Object const& value) {
	std::ostringstream output;
	agi::JsonWriter::Write(value, output);
	return output.str();
}

void write_file(agi::fs::path const& path, std::string const& data) {
	agi::fs::CreateDirectory(path.parent_path());
	agi::io::Save output(path, true);
	output.Get().write(data.data(), static_cast<std::streamsize>(data.size()));
}

std::string read_file(agi::fs::path const& path) {
	auto input = agi::io::Open(path, true);
	std::ostringstream data;
	data << input->rdbuf();
	return data.str();
}
}

std::string SanaeBatchExtractEpisodeCode(agi::fs::path const& path) {
	auto stem = lower_ascii(path.stem().string());
	std::smatch match;
	// The negative lookahead prevents a filename separator followed by a video
	// resolution (for example E03.1080p) from becoming episode code 03.1080.
	static std::regex const season_episode(R"((?:^|[^a-z0-9])s[0-9]+[ ._-]*e([0-9]+(?:\.[0-9]+)?)(?![0-9pi])(?:[^0-9]|$))");
	static std::regex const episode_marker(R"((?:^|[^a-z0-9])(?:episode|ep)[ ._-]*([0-9]+(?:\.[0-9]+)?)(?![0-9pi])(?:[^0-9]|$))");
	if (std::regex_search(stem, match, season_episode)) return match[1].str();
	if (std::regex_search(stem, match, episode_marker)) return match[1].str();
	static std::regex const special_episode(R"((?:^|[ ._-])(ova|oad|ona|sp)([0-9]{0,3})$)");
	if (std::regex_search(stem, match, special_episode)) {
		auto code = match[1].str() + match[2].str();
		std::transform(code.begin(), code.end(), code.begin(), [](unsigned char c) {
			return static_cast<char>(std::toupper(c));
		});
		return code;
	}

	struct Candidate {
		std::string value;
		bool bracketed = false;
		bool common_audio_layout = false;
	};
	std::vector<Candidate> candidates;
	auto bracketed_at = [&](std::size_t position) {
		int square = 0, round = 0;
		for (std::size_t index = 0; index < position; ++index) {
			if (stem[index] == '[') ++square;
			else if (stem[index] == ']' && square) --square;
			else if (stem[index] == '(') ++round;
			else if (stem[index] == ')' && round) --round;
		}
		return square > 0 || round > 0;
	};
	for (std::size_t i = 0; i < stem.size();) {
		if (!std::isdigit(static_cast<unsigned char>(stem[i]))) { ++i; continue; }
		auto begin = i;
		bool left_boundary = begin == 0
			|| !std::isalnum(static_cast<unsigned char>(stem[begin - 1]));
		while (i < stem.size() && std::isdigit(static_cast<unsigned char>(stem[i]))) ++i;
		auto integer_end = i;
		if (i < stem.size() && stem[i] == '.' && i + 1 < stem.size()
			&& std::isdigit(static_cast<unsigned char>(stem[i + 1]))) {
			++i;
			while (i < stem.size() && std::isdigit(static_cast<unsigned char>(stem[i]))) ++i;
		}
		auto value = stem.substr(begin, i - begin);
		bool dotted_resolution_suffix = false;
		if (integer_end < i && i < stem.size() && (stem[i] == 'p' || stem[i] == 'i')) {
			auto fraction = stem.substr(integer_end + 1, i - integer_end - 1);
			if (fraction == "480" || fraction == "720" || fraction == "1080"
				|| fraction == "1440" || fraction == "2160" || fraction == "4320") {
				value = stem.substr(begin, integer_end - begin);
				dotted_resolution_suffix = true;
			}
		}
		bool version_suffix = i + 1 < stem.size() && stem[i] == 'v'
			&& std::isdigit(static_cast<unsigned char>(stem[i + 1]));
		bool right_boundary = i == stem.size()
			|| !std::isalnum(static_cast<unsigned char>(stem[i]))
			|| version_suffix || dotted_resolution_suffix;
		bool resolution = i < stem.size() && (stem[i] == 'p' || stem[i] == 'i')
			&& (value == "480" || value == "720" || value == "1080"
				|| value == "1440" || value == "2160" || value == "4320");
		bool year = value.size() == 4 && value.find('.') == std::string::npos
			&& std::atoi(value.c_str()) >= 1900 && std::atoi(value.c_str()) <= 2099;
		bool audio_layout = value == "2.0" || value == "5.1" || value == "7.1";
		if (left_boundary && right_boundary && !resolution && !year)
			candidates.push_back({std::move(value), bracketed_at(begin), audio_layout});
	}
	if (candidates.empty()) {
		// Real working files often append the episode directly to a personal
		// basename (dara5.ass). Only accept a short trailing number and reject
		// common codec tokens; separated candidates above always take priority.
		static std::regex const attached_suffix(R"(([a-z])([0-9]{1,3}(?:\.[0-9]+)?)(?:v[0-9]+)?$)");
		if (std::regex_search(stem, match, attached_suffix)) {
			auto marker = match[1].str();
			auto value = match[2].str();
			if (!((marker == "x" || marker == "h") && (value == "264" || value == "265")))
				return value;
		}
		return {};
	}
	for (auto it = candidates.rbegin(); it != candidates.rend(); ++it)
		if (!it->bracketed && !it->common_audio_layout) return it->value;
	for (auto it = candidates.rbegin(); it != candidates.rend(); ++it)
		if (!it->common_audio_layout) return it->value;
	return candidates.back().value;
}

std::string SanaeBatchCanonicalEpisodeCode(std::string_view code) {
	auto value = trim_ascii(code);
	if (numeric_code(value)) return "n:" + normalize_numeric(value);
	value = lower_ascii(std::move(value));
	std::string result = "s:";
	bool space = false;
	for (unsigned char c : value) {
		if (c >= 0x80 || std::isalnum(c)) {
			if (space && result.size() > 2) result.push_back(' ');
			result.push_back(static_cast<char>(c));
			space = false;
		}
		else space = true;
	}
	return result.size() == 2 ? std::string() : result;
}

double SanaeBatchEpisodeSortOrder(std::string_view code, double fallback) {
	auto value = trim_ascii(code);
	if (!numeric_code(value)) return fallback;
	char *end = nullptr;
	double number = std::strtod(value.c_str(), &end);
	return end && *end == '\0' ? number : fallback;
}

std::vector<SanaeBatchImportRow> SanaeBatchPairFiles(
	std::vector<agi::fs::path> const& ensub_files,
	std::vector<agi::fs::path> const& rusub_files)
{
	std::map<std::string, SanaeBatchImportRow> paired;
	std::vector<SanaeBatchImportRow> unmapped;
	auto add = [&](agi::fs::path const& path, bool ensub) {
		auto code = SanaeBatchExtractEpisodeCode(path);
		auto canonical = SanaeBatchCanonicalEpisodeCode(code);
		if (canonical.empty()) {
			SanaeBatchImportRow row;
			if (ensub) row.ensub_path = path; else row.rusub_path = path;
			row.status = "Episode code could not be detected";
			unmapped.push_back(std::move(row));
			return;
		}
		auto& row = paired[canonical];
		if (row.episode_code.empty()) row.episode_code = code;
		auto& target = ensub ? row.ensub_path : row.rusub_path;
		auto& duplicate = ensub ? row.duplicate_ensub : row.duplicate_rusub;
		if (!target.empty()) duplicate = true;
		else target = path;
	};
	for (auto const& path : ensub_files) add(path, true);
	for (auto const& path : rusub_files) add(path, false);

	std::vector<SanaeBatchImportRow> result;
	result.reserve(paired.size() + unmapped.size());
	double fallback = 1.0;
	for (auto& [key, row] : paired) {
		(void)key;
		row.sort_order = SanaeBatchEpisodeSortOrder(row.episode_code, fallback++);
		result.push_back(std::move(row));
	}
	result.insert(result.end(), std::make_move_iterator(unmapped.begin()), std::make_move_iterator(unmapped.end()));
	std::stable_sort(result.begin(), result.end(), [](auto const& left, auto const& right) {
		if (left.episode_code.empty() != right.episode_code.empty()) return !left.episode_code.empty();
		return std::tie(left.sort_order, left.episode_code) < std::tie(right.sort_order, right.episode_code);
	});
	return result;
}

std::vector<SanaeBatchImportRow> SanaeBatchScanFolders(
	agi::fs::path const& ensub_directory,
	agi::fs::path const& rusub_directory)
{
	if (ensub_directory.empty() && rusub_directory.empty())
		throw std::invalid_argument("Choose at least one ENSUB or RUSUB directory");
	return SanaeBatchPairFiles(list_subtitle_files(ensub_directory), list_subtitle_files(rusub_directory));
}

std::string SanaeBatchNewIdempotencyKey() {
	std::array<unsigned char, 16> bytes{};
	std::random_device random;
	for (auto& byte : bytes) byte = static_cast<unsigned char>(random());
	bytes[6] = static_cast<unsigned char>((bytes[6] & 0x0f) | 0x40);
	bytes[8] = static_cast<unsigned char>((bytes[8] & 0x3f) | 0x80);
	std::ostringstream output;
	output << std::hex << std::setfill('0');
	for (std::size_t i = 0; i < bytes.size(); ++i) {
		if (i == 4 || i == 6 || i == 8 || i == 10) output << '-';
		output << std::setw(2) << static_cast<int>(bytes[i]);
	}
	return output.str();
}

std::string SanaeBatchSha256File(agi::fs::path const& path) {
#ifdef _WIN32
	auto data = read_file(path);
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
		|| BCryptHashData(hash, reinterpret_cast<PUCHAR>(data.data()), static_cast<ULONG>(data.size()), 0) < 0
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
	(void)path;
	return {};
#endif
}

void SanaeBatchSaveJob(agi::fs::path const& path, SanaeBatchImportJob const& job) {
	json::Object root;
	root["version"] = 1;
	root["project_id"] = job.project_id;
	root["ensub_directory"] = job.ensub_directory.string();
	root["rusub_directory"] = job.rusub_directory.string();
	root["skip_finalized"] = job.skip_finalized;
	root["continue_after_error"] = job.continue_after_error;
	root["sync_after_import"] = job.sync_after_import;
	json::Array rows;
	for (auto const& row : job.rows) {
		json::Object item;
		item["episode_code"] = row.episode_code;
		item["sort_order"] = row.sort_order;
		item["ensub_path"] = row.ensub_path.string();
		item["rusub_path"] = row.rusub_path.string();
		item["duplicate_ensub"] = row.duplicate_ensub;
		item["duplicate_rusub"] = row.duplicate_rusub;
		item["included"] = row.included;
		item["existing_episode_id"] = row.existing_episode_id;
		item["existing_source_action"] = row.existing_source_action;
		item["create_idempotency_key"] = row.create_idempotency_key;
		item["replace_idempotency_key"] = row.replace_idempotency_key;
		item["finalize_idempotency_key"] = row.finalize_idempotency_key;
		item["state"] = state_name(row.state);
		item["status"] = row.status;
		rows.emplace_back(std::move(item));
	}
	root["rows"] = std::move(rows);
	write_file(path, write_json(root));
}

bool SanaeBatchLoadJob(agi::fs::path const& path, SanaeBatchImportJob& job) {
	if (!agi::fs::FileExists(path)) return false;
	try {
		std::istringstream input(read_file(path));
		json::UnknownElement unknown;
		json::Reader::Read(unknown, input);
		auto const& root = static_cast<json::Object const&>(unknown);
		if (get<int>(root, "version") != 1) return false;
		SanaeBatchImportJob loaded;
		loaded.project_id = get<std::string>(root, "project_id");
		loaded.ensub_directory = agi::fs::path(get<std::string>(root, "ensub_directory"));
		loaded.rusub_directory = agi::fs::path(get<std::string>(root, "rusub_directory"));
		loaded.skip_finalized = get<bool>(root, "skip_finalized", true);
		loaded.continue_after_error = get<bool>(root, "continue_after_error", true);
		loaded.sync_after_import = get<bool>(root, "sync_after_import", true);
		auto rows = root.find("rows");
		if (rows != root.end()) for (auto const& value : static_cast<json::Array const&>(rows->second)) {
			auto const& item = static_cast<json::Object const&>(value);
			SanaeBatchImportRow row;
			row.episode_code = get<std::string>(item, "episode_code");
			row.sort_order = get<double>(item, "sort_order");
			row.ensub_path = agi::fs::path(get<std::string>(item, "ensub_path"));
			row.rusub_path = agi::fs::path(get<std::string>(item, "rusub_path"));
			row.duplicate_ensub = get<bool>(item, "duplicate_ensub");
			row.duplicate_rusub = get<bool>(item, "duplicate_rusub");
			row.included = get<bool>(item, "included", true);
			row.existing_episode_id = get<std::string>(item, "existing_episode_id");
			row.existing_source_action = get<std::string>(item, "existing_source_action", "ask");
			row.create_idempotency_key = get<std::string>(item, "create_idempotency_key");
			row.replace_idempotency_key = get<std::string>(item, "replace_idempotency_key");
			row.finalize_idempotency_key = get<std::string>(item, "finalize_idempotency_key");
			row.state = parse_state(get<std::string>(item, "state"));
			row.status = get<std::string>(item, "status");
			if (row.state == SanaeBatchRowState::Running) {
				row.state = SanaeBatchRowState::Failed;
				row.status = "Interrupted; safe to retry";
			}
			loaded.rows.push_back(std::move(row));
		}
		job = std::move(loaded);
		return true;
	}
	catch (...) { return false; }
}
