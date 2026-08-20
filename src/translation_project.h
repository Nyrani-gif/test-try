// Copyright (c) 2026, Aegisub++ contributors
//
// Permission to use, copy, modify, and distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.

#pragma once

#include <libaegisub/fs.h>
#include <libaegisub/signal.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class AssDialogue;
class AssFile;

namespace sanae {
    struct SanaeReviewIssue;
    struct SanaeComment;
    struct LocalLineIdEntry;
    class LocalLineIdRegistry;
}
namespace agi { struct Context; }

enum class ReviewStatus {
        Untranslated,
        Draft,
        NeedsContext,
        OCRDoubt,
        MeaningChecked,
        Polished,
        Typeset,
        QCPassed,
        Final,
        Count
};

enum class ReviewFilter {
        All,
        Untranslated,
        Draft,
        NeedsContext,
        OCRDoubt,
        MeaningChecked,
        Polished,
        Typeset,
        QCPassed,
        Final
};

struct TextRevision {
        std::int64_t changed_at = 0;
        std::string text;
};

struct QCIssue {
        enum class Severity { Warning, Error };
        Severity severity = Severity::Warning;
        std::string code;
        std::string message;
};

enum class TranslationProjectChange {
        /// The visible row list/order may have changed and must be rebuilt.
        View,
        /// Sidecar data displayed by existing rows changed.
        Content,
        /// QC settings or cached results changed without changing the row list.
        QC
};

struct SubtitleFolder {
        int id = 0;
        /// Zero means a root folder. Folder nesting exists only in the sidecar.
        int parent_id = 0;
        std::string name;
        /// RGB colour stored as #RRGGBB. Folder metadata never enters the ASS.
        std::string colour = "#DDEEFF";
        bool collapsed = false;
};

/// Server episode identity stored only in the translation sidecar. Production
/// ASS files never receive Sanae project metadata.
struct SanaeEpisodeBinding {
        std::string project_id;
        std::string episode_id;
        std::string source_file_id;
        std::string base_finalized_revision_id;
        int project_revision = 0;
};

/// Project-local translation metadata which is deliberately kept outside ASS.
///
/// The sidecar stores source/translation associations, review statuses and
/// text-only history. Runtime associations use AssDialogue::Id, while the
/// on-disk representation uses row and timing hints so it can be reconstructed
/// after reloading the ASS file (dialogue IDs are process-local).
class TranslationProject final : private agi::signal::ConnectionScope {
        struct Unit {
                int target_id = 0;
                int target_row = -1;
                int target_start = 0;
                int target_end = 0;
                std::vector<int> source_rows;
                std::string source_text;
                std::string source_display_text;
                ReviewStatus status = ReviewStatus::Untranslated;
                int folder_id = 0;
                std::vector<TextRevision> history;
        };

        agi::Context *context = nullptr;
        std::unique_ptr<AssFile> source;
        agi::fs::path subtitle_path;
        agi::fs::path source_path;
        agi::fs::path sidecar_path;
        std::unordered_map<int, Unit> units;
        std::unordered_map<int, std::string> last_text;
        mutable std::unordered_map<int, std::vector<QCIssue>> qc_cache;
        mutable std::unordered_map<int, std::unordered_map<int, int>> cps_cache;
        std::vector<AssDialogue const *> source_row_index;
        struct SourceTiming {
                AssDialogue const *line = nullptr;
                int row = -1;
                int start = 0;
                int end = 0;
                int midpoint = 0;
        };
        std::vector<SourceTiming> source_by_start;
        std::vector<SourceTiming> source_by_midpoint;
        std::unordered_map<std::uint64_t, std::vector<int>> source_exact_rows;
        std::unordered_map<std::uint64_t, std::vector<AssDialogue const *>> target_exact_lines;
        int maximum_source_duration = 0;
        std::vector<SubtitleFolder> folders;
        std::unordered_map<int, std::size_t> folder_index;
        std::unordered_map<int, AssDialogue *> first_folder_line;
        std::unordered_map<int, AssDialogue *> first_direct_folder_line;
        std::unordered_map<int, int> folder_member_count;
        int next_folder_id = 1;
        bool group_by_folders = false;
        ReviewFilter filter = ReviewFilter::All;
        SanaeEpisodeBinding sanae_binding;
        bool loading = false;
        bool dirty = false;

        // Phase 3: ReviewIssue + local_line_id sidecar persistence.
        std::vector<sanae::SanaeReviewIssue> review_issues_;
        std::vector<sanae::LocalLineIdEntry> local_line_entries_;
        int local_line_next_id_ = 1;

        agi::signal::Signal<TranslationProjectChange, const AssDialogue *> AnnounceChanged;

        void OnSubtitleOpened(agi::fs::path path);
        void OnSubtitleSaved();
        void OnAssCommit(int type, const AssDialogue *changed);
        void RebuildUnits(std::vector<Unit> persisted = {});
        void AlignUnit(Unit& unit, const AssDialogue& target) const;
        void LoadSidecar();
        void LoadSourceFile(agi::fs::path const& path);
        void RememberText(const AssDialogue& line);
        void RebuildSourceIndex();
        void RebuildTargetIndex();
        void RefreshSourceTextCache(Unit& unit) const;
        void RebuildFolderIndex();
        void RebuildFolderViewCache();
        void InvalidateQC(const AssDialogue *line = nullptr);
        void OnQCSettingsChanged();
        void WriteSidecar(agi::fs::path const& path) const;
        bool MatchesFilter(const AssDialogue *line) const;
        bool FolderContains(int ancestor_id, int folder_id) const;
        int CollapsedAncestor(int folder_id) const;
        AssDialogue *FirstFolderLine(int folder_id, bool direct_only = false) const;

public:
        explicit TranslationProject(agi::Context *context);
        ~TranslationProject();

        void LoadSource(agi::fs::path const& path);
        void ClearSource();
        bool HasSource() const;
        agi::fs::path const& SourcePath() const { return source_path; }
        agi::fs::path const& SidecarPath() const { return sidecar_path; }
        std::string SourceText(const AssDialogue *line, std::string const& separator = " / ") const;
        std::string SourceDisplayText(const AssDialogue *line, std::string const& separator = " / ") const;
        std::string const& SourceDisplayTextCached(const AssDialogue *line) const;

        ReviewStatus Status(const AssDialogue *line) const;
        void SetStatus(const AssDialogue *line, ReviewStatus status);
        void SetStatus(std::vector<AssDialogue *> const& lines, ReviewStatus status);
        /// Move all selected lines forward one stage and persist only once.
        void AdvanceStatus(std::vector<AssDialogue *> const& lines);
        static const char *StatusName(ReviewStatus status);
        static const char *StatusSymbol(ReviewStatus status);

        ReviewFilter Filter() const { return filter; }
        void SetFilter(ReviewFilter value);
        bool ShouldDisplay(const AssDialogue *line) const;

        std::vector<SubtitleFolder> const& Folders() const { return folders; }
        SubtitleFolder const *Folder(const AssDialogue *line) const;
        SubtitleFolder const *FolderById(int folder_id) const;
        /// Folder whose header/colour should be shown for this grid row. A
        /// collapsed ancestor takes precedence over the line's direct folder.
        SubtitleFolder const *DisplayFolder(const AssDialogue *line) const;
        bool IsFolderHeader(const AssDialogue *line, int folder_id) const;
        int FolderDepth(int folder_id) const;
        std::string FolderPath(int folder_id) const;
        bool CanMoveFolder(int folder_id, int parent_id) const;
        int CreateFolder(std::string name, std::string colour, std::vector<AssDialogue *> const& lines, int parent_id = 0);
        void AssignFolder(std::vector<AssDialogue *> const& lines, int folder_id);
        void ClearFolder(std::vector<AssDialogue *> const& lines);
        void ToggleFolderCollapsed(int folder_id);
        void MoveFolder(int folder_id, int parent_id);
        void DeleteFolder(int folder_id);
        std::vector<AssDialogue *> FolderMembers(int folder_id, bool include_children = true) const;
        int FolderMemberCount(int folder_id) const;
        bool GroupByFolders() const { return group_by_folders; }
        void SetGroupByFolders(bool enabled);
        /// Build the grid view without changing the order stored in ASS.
        std::vector<AssDialogue *> DisplayLines() const;
        AssDialogue *AdjacentDisplayLine(const AssDialogue *line, int direction) const;

        std::vector<QCIssue> const& CheckLine(const AssDialogue *line) const;
        int CharactersPerSecond(const AssDialogue *line, int ignore_flags) const;
        std::vector<TextRevision> const& History(const AssDialogue *line) const;
        AssDialogue *FindNext(std::string const& query, const AssDialogue *after, bool match_case = false) const;

        bool HasSanaeBinding() const {
                return !sanae_binding.project_id.empty() && !sanae_binding.episode_id.empty()
                        && !sanae_binding.source_file_id.empty();
        }
        SanaeEpisodeBinding const& GetSanaeBinding() const { return sanae_binding; }
        void SetSanaeBinding(SanaeEpisodeBinding binding);
        void ClearSanaeBinding();
        void UpdateSanaeFinalizeState(int project_revision, std::string finalized_revision_id);

        // Phase 3: ReviewIssue + local_line_id accessors.
        std::vector<sanae::SanaeReviewIssue> const& ReviewIssues() const { return review_issues_; }
        void AddReviewIssue(sanae::SanaeReviewIssue issue);
        void UpdateReviewIssue(std::string const& id, sanae::SanaeReviewIssue updated);
        void RemoveReviewIssue(std::string const& id);
        std::vector<sanae::LocalLineIdEntry> const& LocalLineEntries() const { return local_line_entries_; }
        int LocalLineNextId() const { return local_line_next_id_; }

        void Save();
        /// Write a sidecar next to an Aegisub ASS autosave snapshot.
        void SaveAutosave(agi::fs::path const& autosaved_ass_path) const;

        DEFINE_SIGNAL_ADDERS(AnnounceChanged, AddChangeListener)
};
