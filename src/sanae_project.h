// Copyright (c) 2026, Aegisub Sanae contributors

#pragma once

#include "sanae_compact_rusub.h"
#include "sanae_recovery.h"
#include "sanae_subtitle_diff.h"
#include "sanae_text.h"

#include <libaegisub/fs.h>
#include <libaegisub/signal.h>

#include <wx/timer.h>

#include <atomic>
#include <cstddef>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class AssDialogue;
class AssFile;
class wxString;
namespace agi { struct Context; }

struct SanaeSeasonInfo {
	std::string id;
	int year = 0;
	std::string code;
	std::string display_name;
	double sort_order = 0.0;
};

struct SanaeDeviceInfo {
	std::string id;
	std::string display_name;
	std::string device_name;
};

struct SanaeProjectInfo {
	std::string id;
	std::string season_id;
	std::string slug;
	std::string name;
	std::string status;
	int current_revision = 0;
};

struct SanaeEpisodeInfo {
	std::string id;
	std::string project_id;
	std::string episode_code;
	double sort_order = 0.0;
	std::string status;
	std::string current_source_file_id;
	std::string current_finalized_revision_id;
	std::string created_at;
	std::string finalized_at;
	std::string deleted_at;

	bool IsDeleted() const { return !deleted_at.empty(); }
};

struct SanaeEpisodeFileInfo {
	std::string id;
	std::string project_id;
	std::string episode_id;
	std::string kind;
	int revision_number = 0;
	std::string sha256;
	std::size_t size_bytes = 0;
	std::string created_at;
};

struct SanaeFinalizedRevisionInfo {
	std::string id;
	std::string project_id;
	std::string episode_id;
	int revision_number = 0;
	std::string source_file_id;
	std::string compact_rusub_file_id;
	int project_revision = 0;
	std::string created_at;
};

struct SanaeEpisodeDetails {
	SanaeEpisodeInfo episode;
	std::vector<SanaeEpisodeFileInfo> files;
	std::vector<SanaeFinalizedRevisionInfo> finalized_revisions;
	bool local_cache_only = false;
};

struct SanaeTerminologyEntry {
	std::string id;
	std::string english;
	std::string english_normalized;
	std::string russian;
	std::string note;
	int version = 0;
	bool deleted = false;
};

struct SanaeTerminologyHistoryEntry {
	std::string id;
	std::string term_id;
	int term_version = 0;
	std::string english;
	std::string russian;
	std::string note;
	std::string episode_id;
	std::string changed_at;
	int project_revision = 0;
	std::string change_type;
};

struct SanaeIgnoredCandidate {
	std::string id;
	std::string episode_id;
	std::string scope;
	std::string text;
	std::string normalized_text;
	std::string language;
	bool deleted = false;
};

enum class SanaeRepeatKind { None, Similar, Span, Fragment, Exact };

struct SanaeRepeatMatch {
	SanaeRepeatKind kind = SanaeRepeatKind::None;
	double similarity = 0.0;
	std::string source;
	std::string russian;
	std::string episode_code;
	int start = 0;
	int end = 0;
	double context_similarity = 0.0;
	int current_span_lines = 1;
	int source_span_lines = 1;
	int current_span_start = 0;
	int current_span_end = 0;
	std::string current_span_source;
	std::string current_span_russian;
};

struct SanaeTerminologyDraft {
	std::string english;
	std::string russian;
	std::string note;
	std::string operation = "create";
	std::string term_id;
	int base_version = 0;
};

struct SanaeIgnoreDraft {
	std::string scope = "project";
	std::string text;
	std::string language = "en";
};

struct SanaeTerminologyCandidate {
	struct Context {
		std::string episode_code;
		int start = 0;
		int end = 0;
		std::string source;
		std::string russian;
		bool current_episode = false;
	};

	std::string english;
	int score = 0;
	int occurrences = 0;
	int previous_occurrences = 0;
	int project_episode_count = 0;
	bool capitalized = false;
	bool in_previous_episodes = false;
	bool missing_from_dictionary = false;
	std::string reason;
	std::vector<Context> contexts;
};

struct SanaeReviewIssue {
	std::string title;
	std::string detail;
	AssDialogue *line = nullptr;
	std::string replacement_from;
	std::string replacement_to;
};

enum class SanaeSearchScope { English, Russian, All };

struct SanaeSearchOptions {
	std::string query;
	SanaeSearchScope scope = SanaeSearchScope::All;
	bool fuzzy_word_forms = false;
	std::string episode_code;
	std::size_t limit = 500;
};

enum class SanaeProjectChange { Cache, Binding, Repeats, Draft };

/// Per-window state for Sanae Project mode. Ordinary Aegisub code only asks
/// this object for cached state and never needs to know about the server.
class SanaeProjectManager final : private agi::signal::ConnectionScope {
	struct MemoryEntry {
		std::string normalized_source;
		std::string normalized_russian;
		std::string search_source;
		std::string search_russian;
		std::string source;
		std::string russian;
		std::string episode_code;
		int start = 0;
		int end = 0;
	};
	struct MemorySpan {
		std::string normalized_source;
		std::string search_source;
		std::string search_russian;
		std::string source;
		std::string russian;
		std::string episode_code;
		int start = 0;
		int end = 0;
		std::size_t first_memory = 0;
		std::size_t last_memory = 0;
		int line_count = 0;
	};

	agi::Context *context = nullptr;
	std::vector<SanaeSeasonInfo> seasons;
	std::vector<SanaeProjectInfo> projects;
	SanaeProjectInfo active_project;
	std::vector<SanaeEpisodeInfo> episodes;
	std::vector<SanaeEpisodeFileInfo> files;
	std::vector<SanaeFinalizedRevisionInfo> finalized_revisions;
	std::vector<SanaeTerminologyEntry> terminology;
	std::vector<SanaeTerminologyHistoryEntry> terminology_history;
	std::vector<SanaeIgnoredCandidate> ignored_candidates;
	std::vector<SanaeTerminologyDraft> terminology_drafts;
	std::vector<SanaeIgnoreDraft> ignore_drafts;
	std::vector<MemoryEntry> memory;
	/// Immutable server file ids/SHA pairs map to parsed normalized corpus
	/// entries. Repeated Sync calls reuse unchanged episodes in-process.
	std::unordered_map<std::string, std::vector<MemoryEntry>> parsed_memory_cache;
	std::unordered_map<std::string, std::vector<std::size_t>> exact_memory;
	std::unordered_map<std::string, std::vector<std::size_t>> exact_russian_memory;
	/// Repeat retrieval indexes are rebuilt only when immutable project memory
	/// changes, not every time the current episode repeat cache is refreshed.
	std::unordered_map<std::string, std::vector<std::size_t>> repeat_fragment_memory;
	std::unordered_map<std::string, std::vector<std::size_t>> repeat_token_memory;
	std::vector<MemorySpan> memory_spans;
	std::unordered_map<std::string, std::vector<std::size_t>> exact_span_memory;
	std::unordered_map<std::string, std::vector<std::size_t>> repeat_fragment_span_memory;
	std::unordered_map<std::string, std::vector<std::size_t>> repeat_token_span_memory;
	std::unordered_map<int, SanaeRepeatMatch> line_repeats;
	std::string pending_enroll_request;
	std::string pending_enroll_key;
	std::string pending_season_request;
	std::string pending_season_key;
	std::string pending_project_request;
	std::string pending_project_key;
	std::string pending_episode_request;
	std::string pending_episode_key;
	std::string pending_source_request;
	std::string pending_source_key;
	std::string pending_delete_episode_id;
	std::string pending_delete_key;
	std::string pending_recovery_delete_id;
	std::string pending_recovery_delete_key;
	std::string pending_finalize_key;
	std::string last_finalize_warning;
	bool cache_loaded = false;
	bool terminology_history_complete = false;

	wxTimer recovery_timer;
	std::shared_ptr<std::atomic<bool>> recovery_alive;
	SanaeRecoveryState recovery_state;
	std::unordered_map<std::string, std::vector<SanaeRecoverySnapshotInfo>> recovery_snapshots;
	std::string recovery_device_id;
	std::string recovery_baseline_binding_key;
	std::string recovery_last_success_at;
	std::string recovery_last_error;
	bool recovery_baseline_loading = false;
	bool recovery_manual_after_baseline = false;
	bool recovery_too_large_warned = false;

	agi::signal::Signal<SanaeProjectChange> AnnounceChanged;

	void OnSubtitleOpened(agi::fs::path path);
	void OnTranslationProjectChanged(AssDialogue const *line);
	void OnAssCommit(int type, AssDialogue const *line);
	agi::fs::path DirectoryCachePath() const;
	agi::fs::path CacheRoot(std::string const& project_id) const;
	agi::fs::path SnapshotPath(std::string const& project_id) const;
	agi::fs::path DraftPath(std::string const& project_id, std::string const& episode_id) const;
	agi::fs::path FilePath(std::string const& project_id, std::string const& file_id) const;
	void LoadSnapshot(std::string const& project_id);
	void LoadDirectoryCache();
	void SaveDirectoryCache() const;
	void SaveSnapshot() const;
	void LoadDrafts();
	void SaveDrafts() const;
	void DownloadMissingFiles();
	void RefreshTerminologyHistory();
	void RebuildMemory();
	void RebuildRepeatCache();
	void ResetFinalizeKey();
	void ConfigureRecoveryTimer();
	void ResetRecoveryBinding();
	void RefreshRecoveryBaselineAsync();
	void DetectRecoveryDifferenceAsync(SanaeRecoveryBinding binding,
		std::vector<SanaeRecoverySnapshotInfo> snapshots);
	void CheckRecovery(bool manual);
	void QueueRecoveryUpload(SanaeRecoveryUpload upload,
		std::shared_ptr<AssFile const> immutable_ass, bool manual);
	void ShowRecoveryStatus(wxString const& text) const;
	std::string ReadServerFile(SanaeEpisodeFileInfo const& file);
	SanaeEpisodeInfo ReplaceEpisodeSourceRequest(std::string const& episode_id,
		agi::fs::path const& ensub_path, std::string const& source_data,
		std::string const& idempotency_key);
	SanaeEpisodeInfo CreateEpisodeRequest(std::string const& project_id,
		std::string const& episode_code, double sort_order,
		agi::fs::path const& ensub_path, std::string const& source_data,
		std::string const& idempotency_key,
		bool attach_to_current_ass);

public:
	explicit SanaeProjectManager(agi::Context *context);
	~SanaeProjectManager();

	bool IsEnrolled() const;
	std::string ServerBaseUrl() const;
	SanaeDeviceInfo CheckConnection() const;
	void Enroll(std::string const& display_name, std::string const& device_name,
		std::string const& invitation_key);
	void ForgetEnrollment();
	void RefreshDirectory();
	SanaeSeasonInfo CreateSeason(int year, std::string const& code,
		std::string const& display_name, double sort_order);
	SanaeProjectInfo CreateProject(std::string const& season_id,
		std::string const& slug, std::string const& name);
	void SyncProject(std::string const& project_id);
	SanaeEpisodeInfo CreateEpisode(std::string const& project_id, std::string const& episode_code,
		double sort_order, agi::fs::path const& ensub_path);
	SanaeEpisodeInfo CreateEpisodeDetached(std::string const& project_id,
		std::string const& episode_code, double sort_order,
		agi::fs::path const& ensub_path, std::string const& idempotency_key);
	void ValidateBatchEnsub(agi::fs::path const& ensub_path) const;
	SanaeCompactStats PreviewBatchRusub(agi::fs::path const& rusub_path) const;
	SanaeCompactStats FinalizeEpisodeFromFile(std::string const& episode_id,
		agi::fs::path const& rusub_path, std::string const& idempotency_key);
	SanaeEpisodeDetails GetEpisodeDetails(std::string const& episode_id);
	std::string ReadEpisodeFile(std::string const& file_id);
	SanaeSemanticDiff CompareEpisodeSource(std::string const& episode_id,
		agi::fs::path const& ensub_path);
	SanaeEpisodeInfo ReplaceEpisodeSource(std::string const& episode_id,
		agi::fs::path const& ensub_path);
	SanaeEpisodeInfo ReplaceEpisodeSourceWithKey(std::string const& episode_id,
		agi::fs::path const& ensub_path, std::string const& idempotency_key);
	SanaeEpisodeInfo DeleteEpisode(std::string const& episode_id);
	SanaeSemanticDiff CompareFinalizedRevisions(std::string const& before_revision_id,
		std::string const& after_revision_id);
	std::vector<SanaeRecoverySnapshotInfo> ListRecoverySnapshots(std::string const& episode_id);
	std::string ReadRecoverySnapshot(SanaeRecoverySnapshotInfo const& snapshot);
	SanaeSemanticDiff CompareRecoverySnapshot(SanaeRecoverySnapshotInfo const& snapshot);
	void DeleteRecoverySnapshot(std::string const& snapshot_id);
	void RequestRecoveryNow();
	void PrepareRecoveryOnShutdown();
	std::vector<SanaeRecoverySnapshotInfo> const& CachedRecoverySnapshots(
		std::string const& episode_id) const;
	std::string const& RecoveryLastSuccessAt() const { return recovery_last_success_at; }
	std::string const& RecoveryLastError() const { return recovery_last_error; }
	bool RecoveryUploading() const { return recovery_state.IsUploading(); }
	bool RecoveryPaused() const { return recovery_state.IsPaused(); }
	void FinishBatchImport();
	void AttachEpisode(std::string const& episode_id);
	void CloseEpisode();

	bool HasOpenEpisode() const;
	std::string const& ActiveProjectId() const { return active_project.id; }
	std::string ActiveEpisodeId() const;
	std::string ActiveProjectName() const { return active_project.name; }
	SanaeEpisodeInfo const *ActiveEpisode() const;
	SanaeEpisodeInfo const *FindEpisode(std::string const& episode_id) const;
	SanaeEpisodeFileInfo const *FindFile(std::string const& file_id) const;
	agi::fs::path BatchImportStatePath(std::string const& project_id) const;

	std::vector<SanaeSeasonInfo> const& Seasons() const { return seasons; }
	std::vector<SanaeProjectInfo> const& Projects() const { return projects; }
	std::vector<SanaeEpisodeInfo> const& Episodes() const { return episodes; }
	std::vector<SanaeTerminologyEntry> const& Terminology() const { return terminology; }
	std::vector<SanaeTerminologyHistoryEntry> const& TerminologyHistory() const { return terminology_history; }
	std::vector<SanaeTerminologyDraft> const& TerminologyDrafts() const { return terminology_drafts; }
	std::vector<SanaeIgnoreDraft> const& IgnoreDrafts() const { return ignore_drafts; }
	std::string const& LastFinalizeWarning() const { return last_finalize_warning; }

	SanaeRepeatMatch const *RepeatFor(AssDialogue const *line) const;
	std::vector<SanaeRepeatMatch> RepeatsFor(AssDialogue const *line) const;
	std::vector<SanaeRepeatMatch> SearchMemory(SanaeSearchOptions const& options) const;
	std::vector<SanaeRepeatMatch> SearchMemory(std::string const& query) const {
		SanaeSearchOptions options;
		options.query = query;
		return SearchMemory(options);
	}
	std::vector<SanaeTerminologyCandidate> GenerateCandidates() const;
	std::vector<SanaeReviewIssue> TerminologyConsistencyIssues() const;
	std::vector<SanaeReviewIssue> InternalConsistencyIssues() const;
	std::vector<SanaeReviewIssue> SourceRepeatIssues() const;

	void QueueTerminology(SanaeTerminologyDraft draft);
	void QueueTerminologyUpdate(std::string const& term_id, int base_version,
		std::string english, std::string russian, std::string note);
	void QueueTerminologyDelete(std::string const& term_id, int base_version);
	void QueueTerminologyRestore(std::string const& term_id, int base_version);
	void RemoveTerminologyDraft(std::size_t index);
	void QueueIgnore(SanaeIgnoreDraft draft);
	void RemoveIgnoreDraft(std::size_t index);
	SanaeCompactStats Finalize();

	DEFINE_SIGNAL_ADDERS(AnnounceChanged, AddChangeListener)
};
