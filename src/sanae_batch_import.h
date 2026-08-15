// Copyright (c) 2026, Aegisub Sanae contributors

#pragma once

#include <libaegisub/fs.h>

#include <string>
#include <string_view>
#include <vector>

enum class SanaeBatchRowState {
	Pending,
	Running,
	Succeeded,
	Failed,
	Skipped
};

struct SanaeBatchImportRow {
	std::string episode_code;
	double sort_order = 0.0;
	agi::fs::path ensub_path;
	agi::fs::path rusub_path;
	bool duplicate_ensub = false;
	bool duplicate_rusub = false;
	bool included = true;
	std::string existing_episode_id;
	/// ask, skip, use or replace. It is a user decision for an already-existing
	/// server episode and is persisted with the resumable import job.
	std::string existing_source_action = "ask";
	std::string create_idempotency_key;
	std::string replace_idempotency_key;
	std::string finalize_idempotency_key;
	SanaeBatchRowState state = SanaeBatchRowState::Pending;
	std::string status;
};

struct SanaeBatchImportJob {
	std::string project_id;
	agi::fs::path ensub_directory;
	agi::fs::path rusub_directory;
	bool skip_finalized = true;
	bool continue_after_error = true;
	bool sync_after_import = true;
	std::vector<SanaeBatchImportRow> rows;
};

/// Extract an episode code from a common fansub filename. Returns an empty
/// string for files which need manual mapping.
std::string SanaeBatchExtractEpisodeCode(agi::fs::path const& path);

/// Canonical comparison key. Numeric forms such as 01 and 1 compare equal.
std::string SanaeBatchCanonicalEpisodeCode(std::string_view code);

double SanaeBatchEpisodeSortOrder(std::string_view code, double fallback);

/// Pair already-enumerated ENSUB and RUSUB paths. This is separated from the
/// directory scan so matching behavior stays unit-testable.
std::vector<SanaeBatchImportRow> SanaeBatchPairFiles(
	std::vector<agi::fs::path> const& ensub_files,
	std::vector<agi::fs::path> const& rusub_files);

std::vector<SanaeBatchImportRow> SanaeBatchScanFolders(
	agi::fs::path const& ensub_directory,
	agi::fs::path const& rusub_directory);

std::string SanaeBatchNewIdempotencyKey();
std::string SanaeBatchSha256File(agi::fs::path const& path);

void SanaeBatchSaveJob(agi::fs::path const& path, SanaeBatchImportJob const& job);
bool SanaeBatchLoadJob(agi::fs::path const& path, SanaeBatchImportJob& job);
