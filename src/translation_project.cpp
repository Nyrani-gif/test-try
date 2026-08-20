// Copyright (c) 2026, Aegisub++ contributors
//
// Permission to use, copy, modify, and distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.

#include "translation_project.h"

#include "ass_dialogue.h"
#include "ass_file.h"
#include "charset_detect.h"
#include "compat.h"
#include "format.h"
#include "include/aegisub/context.h"
#include "options.h"
#include "project.h"
#include "sanae_profiling.h"
#include "sanae_review_issue.h"
#include "sanae_local_line_id.h"
#include "subs_controller.h"
#include "subtitle_format.h"

#include <libaegisub/cajun/elements.h>
#include <libaegisub/cajun/reader.h>
#include <libaegisub/cajun/writer.h>
#include <libaegisub/character_count.h>
#include <libaegisub/fs.h>
#include <libaegisub/io.h>
#include <libaegisub/string.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace {
constexpr int sidecar_version = 4;
constexpr size_t history_limit = 50;

std::uint64_t timing_key(int start, int end) {
        return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(start)) << 32)
                | static_cast<std::uint32_t>(end);
}

std::int64_t now_seconds() {
        return std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string status_to_string(ReviewStatus status) {
        switch (status) {
                case ReviewStatus::Draft: return "draft";
                case ReviewStatus::NeedsContext: return "needs_context";
                case ReviewStatus::OCRDoubt: return "ocr_doubt";
                case ReviewStatus::MeaningChecked: return "meaning_checked";
                case ReviewStatus::Polished: return "polished";
                case ReviewStatus::Typeset: return "typeset";
                case ReviewStatus::QCPassed: return "qc_passed";
                case ReviewStatus::Final: return "final";
                default: return "untranslated";
        }
}

ReviewStatus status_from_string(std::string const& value) {
        if (value == "draft") return ReviewStatus::Draft;
        if (value == "needs_context" || value == "needs_edit") return ReviewStatus::NeedsContext;
        if (value == "ocr_doubt") return ReviewStatus::OCRDoubt;
        if (value == "meaning_checked" || value == "approved") return ReviewStatus::MeaningChecked;
        if (value == "polished") return ReviewStatus::Polished;
        if (value == "typeset") return ReviewStatus::Typeset;
        if (value == "qc_passed") return ReviewStatus::QCPassed;
        if (value == "final") return ReviewStatus::Final;
        return ReviewStatus::Untranslated;
}

template<typename T>
T get(json::Object const& object, char const *key, T fallback) {
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

std::string relative_path(agi::fs::path const& path, agi::fs::path const& base) {
        if (path.empty()) return {};
        agi::fs::path relative(path.lexically_relative(base));
        return relative.empty() ? path.generic_string() : relative.generic_string();
}

bool contains_text(std::string const& haystack, std::string const& needle, bool match_case) {
        if (needle.empty()) return false;
        auto source = to_wx(haystack);
        auto query = to_wx(needle);
        if (!match_case) {
                source.MakeLower();
                query.MakeLower();
        }
        return source.Find(query) != wxNOT_FOUND;
}

std::string readable_text(AssDialogue const& line) {
        std::string text = line.GetStrippedText();
        for (size_t pos = 0; (pos = text.find("\\N", pos)) != std::string::npos; ) {
                text.replace(pos, 2, " ");
                ++pos;
        }
        if (!text.empty()) return text;

        auto blocks = line.ParseTags();
        bool drawing = std::any_of(blocks.begin(), blocks.end(), [](auto const& block) {
                return block->GetType() == AssBlockType::DRAWING;
        });
        return drawing ? from_wx(_("[ASS drawing]")) : from_wx(_("[formatting only]"));
}

bool is_drawing(AssDialogue const& line) {
        auto blocks = line.ParseTags();
        return std::any_of(blocks.begin(), blocks.end(), [](auto const& block) {
                return block->GetType() == AssBlockType::DRAWING;
        });
}

int max_visible_line_length(std::string text) {
        size_t start = 0;
        int maximum = 0;
        while (start <= text.size()) {
                auto pos = text.find("\\N", start);
                auto segment = text.substr(start, pos == std::string::npos ? pos : pos - start);
                maximum = std::max(maximum, static_cast<int>(agi::CharacterCount(segment, agi::IGNORE_BLOCKS)));
                if (pos == std::string::npos) break;
                start = pos + 2;
        }
        return maximum;
}
}

TranslationProject::TranslationProject(agi::Context *c)
: context(c)
, source(std::make_unique<AssFile>())
{
        BindConnection(context->subsController->AddFileOpenListener(&TranslationProject::OnSubtitleOpened, this));
        BindConnection(context->subsController->AddFileSaveListener(&TranslationProject::OnSubtitleSaved, this));
        BindConnection(context->ass->AddCommitListener(&TranslationProject::OnAssCommit, this));
        BindConnection(OPT_SUB("Subtitle/Character Counter/CPS Error Threshold", &TranslationProject::OnQCSettingsChanged, this));
        BindConnection(OPT_SUB("Subtitle/Character Counter/CPS Warning Threshold", &TranslationProject::OnQCSettingsChanged, this));
        BindConnection(OPT_SUB("Subtitle/Character Limit", &TranslationProject::OnQCSettingsChanged, this));
        RebuildUnits();
}

TranslationProject::~TranslationProject() {
        try { Save(); }
        catch (...) { }
}

void TranslationProject::OnSubtitleOpened(agi::fs::path path) {
        loading = true;
        subtitle_path = std::move(path);
        sidecar_path.clear();
        source_path.clear();
        source = std::make_unique<AssFile>();
        units.clear();
        last_text.clear();
        InvalidateQC();
        RebuildSourceIndex();
        folders.clear();
        RebuildFolderIndex();
        next_folder_id = 1;
        group_by_folders = false;
        filter = ReviewFilter::All;
        sanae_binding = {};
        dirty = false;

        if (!subtitle_path.empty()) {
                sidecar_path = subtitle_path.parent_path() /
                        (subtitle_path.filename().string() + ".aegisub.json");
                LoadSidecar();
        }
        else {
                RebuildUnits();
        }

        loading = false;
        AnnounceChanged(TranslationProjectChange::View, nullptr);
}

void TranslationProject::OnSubtitleSaved() {
        if (subtitle_path != context->subsController->Filename()) {
                subtitle_path = context->subsController->Filename();
                sidecar_path = subtitle_path.parent_path() /
                        (subtitle_path.filename().string() + ".aegisub.json");
        }
        Save();
}

void TranslationProject::RememberText(const AssDialogue& line) {
        auto const& current = line.Text.get();
        auto previous = last_text.find(line.Id);
        if (previous == last_text.end()) {
                last_text.emplace(line.Id, current);
                return;
        }
        if (previous->second == current) return;

        auto unit = units.find(line.Id);
        if (unit != units.end() && !previous->second.empty()) {
                unit->second.history.push_back({now_seconds(), previous->second});
                if (unit->second.history.size() > history_limit)
                        unit->second.history.erase(unit->second.history.begin());
                if (unit->second.status == ReviewStatus::Untranslated)
                        unit->second.status = ReviewStatus::Draft;
                dirty = true;
        }
        previous->second = current;
}

void TranslationProject::OnAssCommit(int type, const AssDialogue *changed) {
        if (loading) return;

        if (type == AssFile::COMMIT_NEW || type & AssFile::COMMIT_ORDER
                || type & AssFile::COMMIT_DIAG_ADDREM || type & AssFile::COMMIT_STYLES)
                InvalidateQC();
        else if (type & (AssFile::COMMIT_DIAG_TEXT | AssFile::COMMIT_DIAG_TIME | AssFile::COMMIT_DIAG_META))
                InvalidateQC(changed);

        if (type & AssFile::COMMIT_DIAG_TEXT) {
                if (changed)
                        RememberText(*changed);
                else
                        for (auto const& line : context->ass->Events) RememberText(line);
        }

        if (type == AssFile::COMMIT_NEW || type & AssFile::COMMIT_ORDER || type & AssFile::COMMIT_DIAG_ADDREM) {
                RebuildUnits();
                dirty = true;
        }
        if (type & AssFile::COMMIT_DIAG_TIME)
                dirty = true;

        // Do not serialize every translation unit and history entry on each
        // keystroke/timing drag. The sidecar is flushed with the subtitle file,
        // by autosave, on explicit metadata changes, and at project destruction.
        if (type == AssFile::COMMIT_NEW || type & AssFile::COMMIT_ORDER || type & AssFile::COMMIT_DIAG_ADDREM)
                AnnounceChanged(TranslationProjectChange::View, nullptr);
        else if (type & AssFile::COMMIT_STYLES)
                AnnounceChanged(TranslationProjectChange::QC, nullptr);
}

void TranslationProject::RebuildUnits(std::vector<Unit> persisted) {
        sanae::ScopedTimer t("sanae/profile/translation_project", "RebuildUnits");
        std::unordered_map<int, Unit> old = std::move(units);
        units.clear();
        units.reserve(context->ass->Events.size());
        last_text.clear();
        last_text.reserve(context->ass->Events.size());
        RebuildTargetIndex();

        std::unordered_map<int, Unit *> persisted_by_row;
        persisted_by_row.reserve(persisted.size());
        for (auto& unit : persisted)
                persisted_by_row.emplace(unit.target_row, &unit);

        for (auto const& line : context->ass->Events) {
                Unit unit;
                unit.target_id = line.Id;
                unit.target_row = line.Row;
                unit.target_start = line.Start;
                unit.target_end = line.End;

                if (auto it = old.find(line.Id); it != old.end()) {
                        unit.source_rows = std::move(it->second.source_rows);
                        unit.status = it->second.status;
                        unit.folder_id = it->second.folder_id;
                        unit.history = std::move(it->second.history);
                }
                else if (auto match = persisted_by_row.find(line.Row); match != persisted_by_row.end()) {
                        auto const& candidate = *match->second;
                        if (std::abs(candidate.target_start - static_cast<int>(line.Start)) <= 1000
                                && std::abs(candidate.target_end - static_cast<int>(line.End)) <= 1000) {
                                unit.source_rows = candidate.source_rows;
                                unit.status = candidate.status;
                                unit.folder_id = candidate.folder_id;
                                unit.history = candidate.history;
                        }
                }

                if (unit.source_rows.empty() && HasSource()) AlignUnit(unit, line);
                RefreshSourceTextCache(unit);
                units.emplace(line.Id, std::move(unit));
                last_text[line.Id] = line.Text.get();
        }
        RebuildFolderViewCache();
}

void TranslationProject::AlignUnit(Unit& unit, const AssDialogue& target) const {
        if (!HasSource()) return;

        int target_start = target.Start;
        int target_end = target.End;

        // Signs are commonly made from many layers sharing exactly the same timing.
        // Mapping every overlapping source line to every target line makes vector
        // drawings flood the Source column. For equal-time clusters, preserve the
        // relative line order instead and make a one-to-one association.
        auto exact = source_exact_rows.find(timing_key(target_start, target_end));
        if (exact != source_exact_rows.end() && !exact->second.empty()) {
                auto const& exact_source_rows = exact->second;
                auto target_group = target_exact_lines.find(timing_key(target_start, target_end));
                static const std::vector<AssDialogue const *> empty_cluster;
                auto const& target_cluster = target_group == target_exact_lines.end() ? empty_cluster : target_group->second;

                auto target_it = std::find(target_cluster.begin(), target_cluster.end(), &target);
                size_t target_rank = target_it == target_cluster.end() ? 0 :
                        static_cast<size_t>(std::distance(target_cluster.begin(), target_it));
                size_t mapped = target_cluster.empty() ? 0 :
                        std::min(exact_source_rows.size() - 1,
                                target_rank * exact_source_rows.size() / target_cluster.size());
                unit.source_rows.push_back(exact_source_rows[mapped]);
                return;
        }

        int nearest_row = -1;
        int nearest_distance = std::numeric_limits<int>::max();
        auto first = std::lower_bound(source_by_start.begin(), source_by_start.end(),
                target_start - maximum_source_duration,
                [](SourceTiming const& candidate, int value) { return candidate.start < value; });
        for (auto candidate = first; candidate != source_by_start.end() && candidate->start < target_end; ++candidate) {
                int candidate_start = candidate->start;
                int candidate_end = candidate->end;
                int overlap = std::min(target_end, candidate_end) - std::max(target_start, candidate_start);
                int shorter_duration = std::min(target_end - target_start, candidate_end - candidate_start);
                if (overlap > 0 && shorter_duration > 0 && overlap * 100 >= shorter_duration * 65)
                        unit.source_rows.push_back(candidate->row);
        }

        int target_midpoint = target_start + (target_end - target_start) / 2;
        auto nearest = std::lower_bound(source_by_midpoint.begin(), source_by_midpoint.end(), target_midpoint,
                [](SourceTiming const& candidate, int value) { return candidate.midpoint < value; });
        auto consider = [&](SourceTiming const& candidate) {
                int distance = std::abs(target_midpoint - candidate.midpoint);
                if (distance < nearest_distance) {
                        nearest_distance = distance;
                        nearest_row = candidate.row;
                }
        };
        if (nearest != source_by_midpoint.end()) consider(*nearest);
        if (nearest != source_by_midpoint.begin()) consider(*std::prev(nearest));
        if (unit.source_rows.empty() && nearest_row >= 0 && nearest_distance <= 750)
                unit.source_rows.push_back(nearest_row);
}

void TranslationProject::RebuildSourceIndex() {
        source_row_index.clear();
        source_by_start.clear();
        source_by_midpoint.clear();
        source_exact_rows.clear();
        maximum_source_duration = 0;
        if (!source) return;

        source_row_index.reserve(source->Events.size());
        source_by_start.reserve(source->Events.size());
        source_by_midpoint.reserve(source->Events.size());
        int row = 0;
        for (auto const& line : source->Events) {
                int start = line.Start;
                int end = line.End;
                SourceTiming timing{&line, row, start, end, start + (end - start) / 2};
                source_row_index.push_back(&line);
                source_by_start.push_back(timing);
                source_by_midpoint.push_back(timing);
                source_exact_rows[timing_key(start, end)].push_back(row);
                maximum_source_duration = std::max(maximum_source_duration, std::max(0, end - start));
                ++row;
        }
        std::sort(source_by_start.begin(), source_by_start.end(), [](auto const& a, auto const& b) {
                return a.start < b.start;
        });
        std::sort(source_by_midpoint.begin(), source_by_midpoint.end(), [](auto const& a, auto const& b) {
                return a.midpoint < b.midpoint;
        });
}

void TranslationProject::RebuildTargetIndex() {
        target_exact_lines.clear();
        target_exact_lines.reserve(context->ass->Events.size());
        for (auto const& line : context->ass->Events)
                target_exact_lines[timing_key(line.Start, line.End)].push_back(&line);
}

void TranslationProject::RefreshSourceTextCache(Unit& unit) const {
        unit.source_text.clear();
        unit.source_display_text.clear();
        for (int row : unit.source_rows) {
                if (row < 0 || static_cast<size_t>(row) >= source_row_index.size()) continue;
                auto const& source_line = *source_row_index[row];
                if (!unit.source_text.empty()) unit.source_text += " / ";
                if (!unit.source_display_text.empty()) unit.source_display_text += " / ";
                unit.source_text += source_line.Text.get();
                unit.source_display_text += readable_text(source_line);
        }
}

void TranslationProject::RebuildFolderIndex() {
        folder_index.clear();
        folder_index.reserve(folders.size());
        for (size_t index = 0; index < folders.size(); ++index)
                folder_index.emplace(folders[index].id, index);
}

void TranslationProject::RebuildFolderViewCache() {
        first_folder_line.clear();
        first_direct_folder_line.clear();
        folder_member_count.clear();
        first_folder_line.reserve(folders.size());
        first_direct_folder_line.reserve(folders.size());
        folder_member_count.reserve(folders.size());

        for (auto const& line : context->ass->Events) {
                auto unit = units.find(line.Id);
                if (unit == units.end() || unit->second.folder_id == 0) continue;
                int direct = unit->second.folder_id;
                if (!FolderById(direct)) continue;
                bool matches = MatchesFilter(&line);
                if (matches)
                        first_direct_folder_line.emplace(direct, const_cast<AssDialogue *>(&line));

                int current = direct;
                for (size_t guard = 0; current > 0 && guard <= folders.size(); ++guard) {
                        auto folder = FolderById(current);
                        if (!folder) break;
                        if (matches)
                                first_folder_line.emplace(current, const_cast<AssDialogue *>(&line));
                        ++folder_member_count[current];
                        current = folder->parent_id;
                }
        }
}

void TranslationProject::InvalidateQC(const AssDialogue *line) {
        if (!line) {
                qc_cache.clear();
                cps_cache.clear();
                return;
        }
        qc_cache.erase(line->Id);
        cps_cache.erase(line->Id);
        auto current = context->ass->iterator_to(*const_cast<AssDialogue *>(line));
        if (current != context->ass->Events.end() && ++current != context->ass->Events.end())
                qc_cache.erase(current->Id);
}

void TranslationProject::OnQCSettingsChanged() {
        InvalidateQC();
        AnnounceChanged(TranslationProjectChange::QC, nullptr);
}

void TranslationProject::LoadSourceFile(agi::fs::path const& path) {
        std::string encoding = CharSetDetect::GetEncoding(path);
        AssFile loaded;
        SubtitleFormat::GetReader(path, encoding.c_str())->ReadFile(
                &loaded, path, context->project->Timecodes(), encoding.c_str());
        source->swap(loaded);
        source_path = path;
        RebuildSourceIndex();
}

void TranslationProject::LoadSource(agi::fs::path const& path) {
        LoadSourceFile(path);
        std::unordered_map<int, AssDialogue *> target_by_id;
        target_by_id.reserve(context->ass->Events.size());
        for (auto& line : context->ass->Events)
                target_by_id.emplace(line.Id, &line);
        for (auto& [id, unit] : units) {
                unit.source_rows.clear();
                if (auto line = target_by_id.find(id); line != target_by_id.end())
                        AlignUnit(unit, *line->second);
                RefreshSourceTextCache(unit);
        }
        dirty = true;
        Save();
        AnnounceChanged(TranslationProjectChange::Content, nullptr);
}

void TranslationProject::ClearSource() {
        source = std::make_unique<AssFile>();
        source_path.clear();
        RebuildSourceIndex();
        for (auto& [id, unit] : units) {
                unit.source_rows.clear();
                unit.source_text.clear();
                unit.source_display_text.clear();
        }
        dirty = true;
        Save();
        AnnounceChanged(TranslationProjectChange::Content, nullptr);
}

bool TranslationProject::HasSource() const {
        return !source_path.empty() && source && !source->Events.empty();
}

std::string TranslationProject::SourceText(const AssDialogue *line, std::string const& separator) const {
        if (!line || !HasSource()) return {};
        auto unit = units.find(line->Id);
        if (unit == units.end()) return {};
        if (separator == " / ") return unit->second.source_text;

        std::string result;
        for (int row : unit->second.source_rows) {
                if (row < 0 || static_cast<size_t>(row) >= source_row_index.size()) continue;
                auto const& source_line = *source_row_index[row];
                if (!result.empty()) result += separator;
                result += source_line.Text.get();
        }
        return result;
}

std::string TranslationProject::SourceDisplayText(const AssDialogue *line, std::string const& separator) const {
        if (!line || !HasSource()) return {};
        auto unit = units.find(line->Id);
        if (unit == units.end()) return {};
        if (separator == " / ") return unit->second.source_display_text;

        std::string result;
        for (int row : unit->second.source_rows) {
                if (row < 0 || static_cast<size_t>(row) >= source_row_index.size()) continue;
                auto const& source_line = *source_row_index[row];
                if (!result.empty()) result += separator;
                result += readable_text(source_line);
        }
        return result;
}

std::string const& TranslationProject::SourceDisplayTextCached(const AssDialogue *line) const {
        static const std::string empty;
        if (!line || !HasSource()) return empty;
        auto unit = units.find(line->Id);
        return unit == units.end() ? empty : unit->second.source_display_text;
}

ReviewStatus TranslationProject::Status(const AssDialogue *line) const {
        if (!line) return ReviewStatus::Untranslated;
        auto unit = units.find(line->Id);
        return unit == units.end() ? ReviewStatus::Untranslated : unit->second.status;
}

void TranslationProject::SetStatus(const AssDialogue *line, ReviewStatus status) {
        if (!line) return;
        auto unit = units.find(line->Id);
        if (unit == units.end() || unit->second.status == status) return;
        unit->second.status = status;
        dirty = true;
        Save();
        if (filter != ReviewFilter::All) RebuildFolderViewCache();
        AnnounceChanged(filter == ReviewFilter::All ? TranslationProjectChange::Content : TranslationProjectChange::View, line);
}

void TranslationProject::SetStatus(std::vector<AssDialogue *> const& lines, ReviewStatus status) {
        bool changed = false;
        for (auto line : lines) {
                if (!line) continue;
                auto unit = units.find(line->Id);
                if (unit == units.end() || unit->second.status == status) continue;
                unit->second.status = status;
                changed = true;
        }
        if (!changed) return;
        dirty = true;
        Save();
        if (filter != ReviewFilter::All) RebuildFolderViewCache();
        AnnounceChanged(filter == ReviewFilter::All ? TranslationProjectChange::Content : TranslationProjectChange::View, nullptr);
}

void TranslationProject::AdvanceStatus(std::vector<AssDialogue *> const& lines) {
        bool changed = false;
        for (auto line : lines) {
                if (!line) continue;
                auto unit = units.find(line->Id);
                if (unit == units.end() || unit->second.status == ReviewStatus::Final) continue;
                unit->second.status = static_cast<ReviewStatus>(static_cast<int>(unit->second.status) + 1);
                changed = true;
        }
        if (!changed) return;
        dirty = true;
        Save();
        if (filter != ReviewFilter::All) RebuildFolderViewCache();
        AnnounceChanged(filter == ReviewFilter::All ? TranslationProjectChange::Content : TranslationProjectChange::View, nullptr);
}

const char *TranslationProject::StatusName(ReviewStatus status) {
        switch (status) {
                case ReviewStatus::Draft: return "Draft";
                case ReviewStatus::NeedsContext: return "Needs context";
                case ReviewStatus::OCRDoubt: return "OCR doubt";
                case ReviewStatus::MeaningChecked: return "Meaning checked";
                case ReviewStatus::Polished: return "Polished";
                case ReviewStatus::Typeset: return "Typeset";
                case ReviewStatus::QCPassed: return "QC passed";
                case ReviewStatus::Final: return "Final";
                default: return "Untranslated";
        }
}

const char *TranslationProject::StatusSymbol(ReviewStatus status) {
        switch (status) {
                case ReviewStatus::Draft: return "D";
                case ReviewStatus::NeedsContext: return "?";
                case ReviewStatus::OCRDoubt: return "O";
                case ReviewStatus::MeaningChecked: return "M";
                case ReviewStatus::Polished: return "P";
                case ReviewStatus::Typeset: return "T";
                case ReviewStatus::QCPassed: return "Q";
                case ReviewStatus::Final: return "★";
                default: return "○";
        }
}

void TranslationProject::SetFilter(ReviewFilter value) {
        if (filter == value) return;
        filter = value;
        RebuildFolderViewCache();
        AnnounceChanged(TranslationProjectChange::View, nullptr);
}

bool TranslationProject::MatchesFilter(const AssDialogue *line) const {
        if (filter == ReviewFilter::All) return true;
        return static_cast<int>(filter) - 1 == static_cast<int>(Status(line));
}

bool TranslationProject::ShouldDisplay(const AssDialogue *line) const {
        if (!MatchesFilter(line)) return false;
        auto folder = Folder(line);
        if (!folder) return true;
        int collapsed = CollapsedAncestor(folder->id);
        return collapsed == 0 || FirstFolderLine(collapsed) == line;
}

SubtitleFolder const *TranslationProject::Folder(const AssDialogue *line) const {
        if (!line) return nullptr;
        auto unit = units.find(line->Id);
        if (unit == units.end() || unit->second.folder_id == 0) return nullptr;
        return FolderById(unit->second.folder_id);
}

SubtitleFolder const *TranslationProject::FolderById(int folder_id) const {
        auto folder = folder_index.find(folder_id);
        return folder == folder_index.end() ? nullptr : &folders[folder->second];
}

bool TranslationProject::FolderContains(int ancestor_id, int folder_id) const {
        if (ancestor_id <= 0 || folder_id <= 0) return false;
        int current = folder_id;
        for (size_t guard = 0; current > 0 && guard <= folders.size(); ++guard) {
                if (current == ancestor_id) return true;
                auto folder = FolderById(current);
                current = folder ? folder->parent_id : 0;
        }
        return false;
}

int TranslationProject::CollapsedAncestor(int folder_id) const {
        int result = 0;
        int current = folder_id;
        for (size_t guard = 0; current > 0 && guard <= folders.size(); ++guard) {
                auto folder = FolderById(current);
                if (!folder) break;
                if (folder->collapsed) result = folder->id;
                current = folder->parent_id;
        }
        return result;
}

AssDialogue *TranslationProject::FirstFolderLine(int folder_id, bool direct_only) const {
        auto const& cache = direct_only ? first_direct_folder_line : first_folder_line;
        auto line = cache.find(folder_id);
        return line == cache.end() ? nullptr : line->second;
}

SubtitleFolder const *TranslationProject::DisplayFolder(const AssDialogue *line) const {
        auto direct = Folder(line);
        if (!direct) return nullptr;
        int collapsed = CollapsedAncestor(direct->id);
        return collapsed ? FolderById(collapsed) : direct;
}

bool TranslationProject::IsFolderHeader(const AssDialogue *line, int folder_id) const {
        if (!line || folder_id <= 0) return false;
        auto folder = FolderById(folder_id);
        if (!folder) return false;
        return FirstFolderLine(folder_id, !folder->collapsed) == line;
}

int TranslationProject::FolderDepth(int folder_id) const {
        int depth = 0;
        auto folder = FolderById(folder_id);
        for (size_t guard = 0; folder && folder->parent_id > 0 && guard <= folders.size(); ++guard) {
                ++depth;
                folder = FolderById(folder->parent_id);
        }
        return depth;
}

std::string TranslationProject::FolderPath(int folder_id) const {
        std::vector<std::string> parts;
        auto folder = FolderById(folder_id);
        for (size_t guard = 0; folder && guard <= folders.size(); ++guard) {
                parts.push_back(folder->name);
                folder = FolderById(folder->parent_id);
        }
        std::reverse(parts.begin(), parts.end());
        std::string result;
        for (auto const& part : parts) {
                if (!result.empty()) result += " / ";
                result += part;
        }
        return result;
}

bool TranslationProject::CanMoveFolder(int folder_id, int parent_id) const {
        if (!FolderById(folder_id)) return false;
        if (parent_id == 0) return true;
        if (!FolderById(parent_id) || folder_id == parent_id) return false;
        return !FolderContains(folder_id, parent_id);
}

int TranslationProject::CreateFolder(std::string name, std::string colour, std::vector<AssDialogue *> const& lines, int parent_id) {
        if (name.empty()) name = from_wx(_("Folder"));
        if (colour.size() != 7 || colour.front() != '#') colour = "#DDEEFF";
        if (parent_id != 0 && !FolderById(parent_id)) parent_id = 0;
        int id = next_folder_id++;
        folders.push_back({id, parent_id, std::move(name), std::move(colour), false});
        RebuildFolderIndex();
        for (auto line : lines) {
                if (line) {
                        auto unit = units.find(line->Id);
                        if (unit != units.end()) unit->second.folder_id = id;
                }
        }
        RebuildFolderViewCache();
        dirty = true;
        Save();
        AnnounceChanged(TranslationProjectChange::View, nullptr);
        return id;
}

void TranslationProject::AssignFolder(std::vector<AssDialogue *> const& lines, int folder_id) {
        if (folder_id != 0 && std::none_of(folders.begin(), folders.end(), [&](SubtitleFolder const& folder) {
                return folder.id == folder_id;
        })) return;

        bool changed = false;
        for (auto line : lines) {
                if (!line) continue;
                auto unit = units.find(line->Id);
                if (unit == units.end() || unit->second.folder_id == folder_id) continue;
                unit->second.folder_id = folder_id;
                changed = true;
        }
        if (!changed) return;
        RebuildFolderViewCache();
        dirty = true;
        Save();
        AnnounceChanged(TranslationProjectChange::View, nullptr);
}

void TranslationProject::ClearFolder(std::vector<AssDialogue *> const& lines) {
        AssignFolder(lines, 0);
}

void TranslationProject::ToggleFolderCollapsed(int folder_id) {
        auto folder = std::find_if(folders.begin(), folders.end(), [&](SubtitleFolder const& candidate) {
                return candidate.id == folder_id;
        });
        if (folder == folders.end()) return;
        folder->collapsed = !folder->collapsed;
        dirty = true;
        Save();
        AnnounceChanged(TranslationProjectChange::View, nullptr);
}

void TranslationProject::MoveFolder(int folder_id, int parent_id) {
        if (!CanMoveFolder(folder_id, parent_id)) return;
        auto folder = std::find_if(folders.begin(), folders.end(), [&](SubtitleFolder const& candidate) {
                return candidate.id == folder_id;
        });
        if (folder == folders.end() || folder->parent_id == parent_id) return;
        folder->parent_id = parent_id;
        RebuildFolderViewCache();
        dirty = true;
        Save();
        AnnounceChanged(TranslationProjectChange::View, nullptr);
}

void TranslationProject::DeleteFolder(int folder_id) {
        auto deleted = FolderById(folder_id);
        if (!deleted) return;
        int new_parent = deleted->parent_id;
        for (auto& folder : folders)
                if (folder.parent_id == folder_id) folder.parent_id = new_parent;
        for (auto& [id, unit] : units)
                if (unit.folder_id == folder_id) unit.folder_id = new_parent;

        folders.erase(std::remove_if(folders.begin(), folders.end(), [&](SubtitleFolder const& folder) {
                return folder.id == folder_id;
        }), folders.end());
        RebuildFolderIndex();
        RebuildFolderViewCache();
        dirty = true;
        Save();
        AnnounceChanged(TranslationProjectChange::View, nullptr);
}

std::vector<AssDialogue *> TranslationProject::FolderMembers(int folder_id, bool include_children) const {
        std::vector<AssDialogue *> result;
        for (auto const& line : context->ass->Events) {
                auto unit = units.find(line.Id);
                if (unit != units.end() && (unit->second.folder_id == folder_id
                        || (include_children && FolderContains(folder_id, unit->second.folder_id))))
                        result.push_back(const_cast<AssDialogue *>(&line));
        }
        return result;
}

int TranslationProject::FolderMemberCount(int folder_id) const {
        auto count = folder_member_count.find(folder_id);
        return count == folder_member_count.end() ? 0 : count->second;
}

void TranslationProject::SetGroupByFolders(bool enabled) {
        if (group_by_folders == enabled) return;
        group_by_folders = enabled;
        dirty = true;
        Save();
        AnnounceChanged(TranslationProjectChange::View, nullptr);
}

std::vector<AssDialogue *> TranslationProject::DisplayLines() const {
        std::vector<AssDialogue *> result;
        result.reserve(context->ass->Events.size());
        if (!group_by_folders) {
                for (auto& line : context->ass->Events)
                        if (ShouldDisplay(&line)) result.push_back(&line);
                return result;
        }

        std::unordered_map<int, std::vector<AssDialogue *>> direct_lines;
        std::unordered_map<int, std::vector<int>> child_folders;
        direct_lines.reserve(folders.size());
        child_folders.reserve(folders.size());
        for (auto const& folder : folders)
                child_folders[folder.parent_id].push_back(folder.id);
        for (auto& line : context->ass->Events) {
                if (!MatchesFilter(&line)) continue;
                if (auto direct = Folder(&line))
                        direct_lines[direct->id].push_back(&line);
        }

        std::function<void(int)> append_folder = [&](int folder_id) {
                auto folder = FolderById(folder_id);
                if (!folder) return;
                if (folder->collapsed) {
                        if (auto first = FirstFolderLine(folder_id)) result.push_back(first);
                        return;
                }

                if (auto lines = direct_lines.find(folder_id); lines != direct_lines.end())
                        result.insert(result.end(), lines->second.begin(), lines->second.end());
                if (auto children = child_folders.find(folder_id); children != child_folders.end())
                        for (int child : children->second) append_folder(child);
        };

        if (auto roots = child_folders.find(0); roots != child_folders.end())
                for (int root : roots->second) append_folder(root);
        for (auto& line : context->ass->Events)
                if (MatchesFilter(&line) && !Folder(&line)) result.push_back(&line);
        return result;
}

AssDialogue *TranslationProject::AdjacentDisplayLine(const AssDialogue *line, int direction) const {
        if (direction == 0) return const_cast<AssDialogue *>(line);
        auto rows = DisplayLines();
        if (rows.empty()) return nullptr;
        auto current = std::find(rows.begin(), rows.end(), line);
        if (current == rows.end()) return direction > 0 ? rows.front() : rows.back();
        if (direction > 0) return ++current == rows.end() ? nullptr : *current;
        return current == rows.begin() ? nullptr : *--current;
}

std::vector<QCIssue> const& TranslationProject::CheckLine(const AssDialogue *line) const {
        static const std::vector<QCIssue> empty;
        if (!line) return empty;
        if (auto cached = qc_cache.find(line->Id); cached != qc_cache.end())
                return cached->second;

        std::vector<QCIssue> issues;
        issues.reserve(5);
        bool drawing = is_drawing(*line);

        int duration = line->End - line->Start;
        if (duration <= 0)
                issues.push_back({QCIssue::Severity::Error, "duration", from_wx(_("End time must be after start time"))});
        else if (!drawing) {
                int cps = CharactersPerSecond(line, agi::IGNORE_BLOCKS | agi::IGNORE_WHITESPACE);
                int error = OPT_GET("Subtitle/Character Counter/CPS Error Threshold")->GetInt();
                int warning = OPT_GET("Subtitle/Character Counter/CPS Warning Threshold")->GetInt();
                if (cps > error)
                        issues.push_back({QCIssue::Severity::Error, "cps", from_wx(agi::wxformat(_("Reading speed is %d CPS"), cps))});
                else if (cps > warning)
                        issues.push_back({QCIssue::Severity::Warning, "cps", from_wx(agi::wxformat(_("Reading speed is %d CPS"), cps))});
        }

        if (!drawing) {
                int line_limit = OPT_GET("Subtitle/Character Limit")->GetInt();
                int visible_length = max_visible_line_length(line->Text.get());
                if (line_limit > 0 && visible_length > line_limit)
                        issues.push_back({QCIssue::Severity::Warning, "length", from_wx(agi::wxformat(_("Visible line has %d characters"), visible_length))});
        }

        auto const& text = line->Text.get();
        if (!drawing && text.find("  ") != std::string::npos)
                issues.push_back({QCIssue::Severity::Warning, "spaces", from_wx(_("Text contains repeated spaces"))});
        if (std::count(text.begin(), text.end(), '{') != std::count(text.begin(), text.end(), '}'))
                issues.push_back({QCIssue::Severity::Error, "tags", from_wx(_("Unbalanced ASS override braces"))});
        if (!context->ass->GetStyle(line->Style))
                issues.push_back({QCIssue::Severity::Error, "style", from_wx(_("Style does not exist"))});

        auto current = context->ass->iterator_to(*const_cast<AssDialogue *>(line));
        if (current != context->ass->Events.begin() && current != context->ass->Events.end()) {
                auto previous = std::prev(current);
                if (previous->End > line->Start)
                        issues.push_back({QCIssue::Severity::Warning, "overlap", from_wx(_("Line overlaps the preceding subtitle"))});
        }
        return qc_cache.emplace(line->Id, std::move(issues)).first->second;
}

int TranslationProject::CharactersPerSecond(const AssDialogue *line, int ignore_flags) const {
        if (!line) return -1;
        int duration = line->End - line->Start;
        if (duration <= 0) return -1;
        auto& values = cps_cache[line->Id];
        if (auto value = values.find(ignore_flags); value != values.end())
                return value->second;
        int cps = agi::CharacterCount(line->Text.get(), ignore_flags) * 1000 / duration;
        values.emplace(ignore_flags, cps);
        return cps;
}

std::vector<TextRevision> const& TranslationProject::History(const AssDialogue *line) const {
        static const std::vector<TextRevision> empty;
        if (!line) return empty;
        auto unit = units.find(line->Id);
        return unit == units.end() ? empty : unit->second.history;
}

AssDialogue *TranslationProject::FindNext(std::string const& query, const AssDialogue *after, bool match_case) const {
        if (query.empty() || context->ass->Events.empty()) return nullptr;
        int start = after ? after->Row + 1 : 0;
        int count = static_cast<int>(context->ass->Events.size());
        auto line = context->ass->Events.begin();
        std::advance(line, start % count);
        for (int offset = 0; offset < count; ++offset) {
                if (contains_text(readable_text(*line), query, match_case)
                        || contains_text(SourceDisplayTextCached(&*line), query, match_case))
                        return &*line;
                if (++line == context->ass->Events.end()) line = context->ass->Events.begin();
        }
        return nullptr;
}

void TranslationProject::LoadSidecar() {
        std::vector<Unit> persisted;
        if (!agi::fs::Exists(sidecar_path)) {
                RebuildUnits();
                return;
        }

        try {
                auto stream = agi::io::Open(sidecar_path);
                json::UnknownElement root;
                json::Reader::Read(root, *stream);
                auto const& object = static_cast<json::Object const&>(root);
                int version = get<int>(object, "version", 0);
                if (version < 1 || version > sidecar_version) {
                        RebuildUnits();
                        return;
                }
                auto source_name = get<std::string>(object, "source", {});
                if (!source_name.empty()) {
                        agi::fs::path candidate(source_name);
                        if (candidate.is_relative()) candidate = sidecar_path.parent_path() / candidate;
                        if (agi::fs::Exists(candidate)) LoadSourceFile(agi::fs::path(candidate.lexically_normal()));
                }

                auto folders_it = object.find("folders");
                if (folders_it != object.end()) {
                        for (auto const& entry : static_cast<json::Array const&>(folders_it->second)) {
                                auto const& folder_object = static_cast<json::Object const&>(entry);
                                SubtitleFolder folder;
                                folder.id = get<int>(folder_object, "id", 0);
                                folder.parent_id = get<int>(folder_object, "parent_id", 0);
                                folder.name = get<std::string>(folder_object, "name", {});
                                folder.colour = get<std::string>(folder_object, "colour", "#DDEEFF");
                                folder.collapsed = get<bool>(folder_object, "collapsed", false);
                                if (folder.id > 0 && !folder.name.empty()) {
                                        next_folder_id = std::max(next_folder_id, folder.id + 1);
                                        folders.push_back(std::move(folder));
                                }
                        }
                }
                RebuildFolderIndex();
                group_by_folders = get<bool>(object, "group_by_folders", false);
                if (auto sanae = object.find("sanae"); sanae != object.end()) {
                        auto const& binding = static_cast<json::Object const&>(sanae->second);
                        sanae_binding.project_id = get<std::string>(binding, "project_id", {});
                        sanae_binding.episode_id = get<std::string>(binding, "episode_id", {});
                        sanae_binding.source_file_id = get<std::string>(binding, "source_file_id", {});
                        sanae_binding.base_finalized_revision_id = get<std::string>(binding, "base_finalized_revision_id", {});
                        sanae_binding.project_revision = get<int>(binding, "project_revision", 0);
                        if (sanae_binding.project_id.empty() || sanae_binding.episode_id.empty()
                                || sanae_binding.source_file_id.empty())
                                sanae_binding = {};
                }
                for (auto& folder : folders) {
                        if (folder.parent_id == folder.id || !FolderById(folder.parent_id))
                                folder.parent_id = 0;
                }
                for (auto& folder : folders)
                        if (!CanMoveFolder(folder.id, folder.parent_id)) folder.parent_id = 0;

                auto units_it = object.find("units");
                if (units_it != object.end()) {
                        for (auto const& entry : static_cast<json::Array const&>(units_it->second)) {
                                auto const& unit_object = static_cast<json::Object const&>(entry);
                                Unit unit;
                                unit.target_row = get<int>(unit_object, "target_row", -1);
                                unit.target_start = get<int>(unit_object, "target_start", 0);
                                unit.target_end = get<int>(unit_object, "target_end", 0);
                                unit.status = status_from_string(get<std::string>(unit_object, "status", "unchecked"));
                                unit.folder_id = get<int>(unit_object, "folder_id", 0);

                                if (auto it = unit_object.find("source_rows"); it != unit_object.end())
                                        for (auto const& row : static_cast<json::Array const&>(it->second))
                                                unit.source_rows.push_back(static_cast<int>(row));

                                if (auto it = unit_object.find("history"); it != unit_object.end()) {
                                        for (auto const& revision : static_cast<json::Array const&>(it->second)) {
                                                auto const& revision_object = static_cast<json::Object const&>(revision);
                                                unit.history.push_back({
                                                        get<std::int64_t>(revision_object, "changed_at", 0),
                                                        get<std::string>(revision_object, "text", {})});
                                        }
                                }
                                persisted.push_back(std::move(unit));
                        }
                }

                // Phase 3: Load ReviewIssues from sidecar.
                review_issues_.clear();
                if (auto issues_it = object.find("review_issues"); issues_it != object.end()) {
                        for (auto const& entry : static_cast<json::Array const&>(issues_it->second)) {
                                auto const& io = static_cast<json::Object const&>(entry);
                                sanae::SanaeReviewIssue issue;
                                issue.id = get<std::string>(io, "id", {});
                                issue.local_line_id = get<std::string>(io, "local_line_id", {});
                                issue.kind = static_cast<sanae::ReviewIssueKind>(get<int>(io, "kind", 0));
                                issue.severity = static_cast<sanae::ReviewIssueSeverity>(get<int>(io, "severity", 1));
                                issue.state = sanae::SanaeReviewIssue::StateFromString(get<std::string>(io, "state", "open"));
                                issue.body = get<std::string>(io, "body", {});
                                issue.resolution_note = get<std::string>(io, "resolution_note", {});
                                issue.version = get<int>(io, "version", 1);
                                issue.baseline_text_hash = get<std::string>(io, "baseline_text_hash", {});
                                issue.baseline_timing_hash = get<std::string>(io, "baseline_timing_hash", {});
                                issue.created_by_device_id = get<std::string>(io, "created_by_device_id", {});
                                issue.created_at = get<std::string>(io, "created_at", {});
                                issue.updated_at = get<std::string>(io, "updated_at", {});
                                issue.resolved_at = get<std::string>(io, "resolved_at", {});
                                issue.resolved_by_device_id = get<std::string>(io, "resolved_by_device_id", {});
                                issue.deleted_at = get<std::string>(io, "deleted_at", {});
                                // Load comments (immutable).
                                if (auto comments_it = io.find("comments"); comments_it != io.end()) {
                                        for (auto const& ce : static_cast<json::Array const&>(comments_it->second)) {
                                                auto const& co = static_cast<json::Object const&>(ce);
                                                issue.comments.push_back({
                                                        get<std::string>(co, "id", {}),
                                                        get<std::string>(co, "issue_id", {}),
                                                        get<std::string>(co, "body", {}),
                                                        get<std::string>(co, "created_by_device_id", {}),
                                                        get<std::string>(co, "created_at", {})});
                                        }
                                }
                                if (!issue.id.empty()) review_issues_.push_back(std::move(issue));
                        }
                }

                // Phase 3: Load local_line_id mapping from sidecar.
                local_line_entries_.clear();
                local_line_next_id_ = 1;
                if (auto lli_it = object.find("local_line_ids"); lli_it != object.end()) {
                        for (auto const& entry : static_cast<json::Array const&>(lli_it->second)) {
                                auto const& lo = static_cast<json::Object const&>(entry);
                                sanae::LocalLineIdEntry e;
                                e.id = get<std::string>(lo, "id", {});
                                e.start_cs = get<int>(lo, "start", 0);
                                e.end_cs = get<int>(lo, "end", 0);
                                e.source_hash = get<std::string>(lo, "source_hash", {});
                                e.dialogue_id = get<int>(lo, "dialogue_id", -1);
                                e.orphaned = get<bool>(lo, "orphaned", false);
                                if (!e.id.empty()) local_line_entries_.push_back(std::move(e));
                        }
                }
                local_line_next_id_ = get<int>(object, "next_local_line_id", 1);
        }
        catch (...) {
                // A broken sidecar must never prevent the subtitle file from opening.
                persisted.clear();
                source = std::make_unique<AssFile>();
                source_path.clear();
                RebuildSourceIndex();
                folders.clear();
                RebuildFolderIndex();
                next_folder_id = 1;
                group_by_folders = false;
                sanae_binding = {};
                review_issues_.clear();
                local_line_entries_.clear();
                local_line_next_id_ = 1;
        }
        RebuildUnits(std::move(persisted));
}

void TranslationProject::WriteSidecar(agi::fs::path const& path) const {
        json::Object root;
        root["version"] = sidecar_version;
        root["source"] = relative_path(source_path, path.parent_path());
        root["group_by_folders"] = group_by_folders;
        if (HasSanaeBinding()) {
                json::Object binding;
                binding["project_id"] = sanae_binding.project_id;
                binding["episode_id"] = sanae_binding.episode_id;
                binding["source_file_id"] = sanae_binding.source_file_id;
                binding["base_finalized_revision_id"] = sanae_binding.base_finalized_revision_id;
                binding["project_revision"] = sanae_binding.project_revision;
                root["sanae"] = std::move(binding);
        }

        json::Array serialized_folders;
        for (auto const& folder : folders) {
                json::Object item;
                item["id"] = folder.id;
                item["parent_id"] = folder.parent_id;
                item["name"] = folder.name;
                item["colour"] = folder.colour;
                item["collapsed"] = folder.collapsed;
                serialized_folders.emplace_back(std::move(item));
        }
        root["folders"] = std::move(serialized_folders);

        json::Array serialized_units;
        for (auto const& line : context->ass->Events) {
                auto it = units.find(line.Id);
                if (it == units.end()) continue;
                auto const& unit = it->second;

                json::Object item;
                item["target_row"] = line.Row;
                item["target_start"] = static_cast<int>(line.Start);
                item["target_end"] = static_cast<int>(line.End);
                item["status"] = status_to_string(unit.status);
                item["folder_id"] = unit.folder_id;

                json::Array source_rows;
                for (int row : unit.source_rows) source_rows.emplace_back(row);
                item["source_rows"] = std::move(source_rows);

                json::Array history;
                for (auto const& revision : unit.history) {
                        json::Object serialized_revision;
                        serialized_revision["changed_at"] = revision.changed_at;
                        serialized_revision["text"] = revision.text;
                        history.emplace_back(std::move(serialized_revision));
                }
                item["history"] = std::move(history);
                serialized_units.emplace_back(std::move(item));
        }
        root["units"] = std::move(serialized_units);

        // Phase 3: Serialize ReviewIssues.
        json::Array serialized_issues;
        for (auto const& issue : review_issues_) {
                json::Object io;
                io["id"] = issue.id;
                io["local_line_id"] = issue.local_line_id;
                io["kind"] = static_cast<int>(issue.kind);
                io["severity"] = static_cast<int>(issue.severity);
                io["state"] = sanae::SanaeReviewIssue::StateToString(issue.state);
                io["body"] = issue.body;
                io["resolution_note"] = issue.resolution_note;
                io["version"] = issue.version;
                io["baseline_text_hash"] = issue.baseline_text_hash;
                io["baseline_timing_hash"] = issue.baseline_timing_hash;
                io["created_by_device_id"] = issue.created_by_device_id;
                io["created_at"] = issue.created_at;
                io["updated_at"] = issue.updated_at;
                io["resolved_at"] = issue.resolved_at;
                io["resolved_by_device_id"] = issue.resolved_by_device_id;
                io["deleted_at"] = issue.deleted_at;
                json::Array serialized_comments;
                for (auto const& c : issue.comments) {
                        json::Object co;
                        co["id"] = c.id;
                        co["issue_id"] = c.issue_id;
                        co["body"] = c.body;
                        co["created_by_device_id"] = c.created_by_device_id;
                        co["created_at"] = c.created_at;
                        serialized_comments.emplace_back(std::move(co));
                }
                io["comments"] = std::move(serialized_comments);
                serialized_issues.emplace_back(std::move(io));
        }
        root["review_issues"] = std::move(serialized_issues);

        // Phase 3: Serialize local_line_id mapping.
        json::Array serialized_llids;
        for (auto const& e : local_line_entries_) {
                json::Object lo;
                lo["id"] = e.id;
                lo["start"] = e.start_cs;
                lo["end"] = e.end_cs;
                lo["source_hash"] = e.source_hash;
                lo["dialogue_id"] = e.dialogue_id;
                lo["orphaned"] = e.orphaned;
                serialized_llids.emplace_back(std::move(lo));
        }
        root["local_line_ids"] = std::move(serialized_llids);
        root["next_local_line_id"] = local_line_next_id_;

        agi::JsonWriter::Write(root, agi::io::Save(path).Get());
}

void TranslationProject::Save() {
        if (loading || !dirty || sidecar_path.empty()) return;
        WriteSidecar(sidecar_path);
        dirty = false;
}

void TranslationProject::SaveAutosave(agi::fs::path const& autosaved_ass_path) const {
        if (loading || autosaved_ass_path.empty()) return;
        auto autosave_sidecar = agi::fs::path(autosaved_ass_path.string() + ".aegisub.json");
        WriteSidecar(autosave_sidecar);
}

void TranslationProject::SetSanaeBinding(SanaeEpisodeBinding binding) {
        if (binding.project_id.empty() || binding.episode_id.empty() || binding.source_file_id.empty())
                throw std::invalid_argument("Sanae binding requires project, episode and source file IDs");
        sanae_binding = std::move(binding);
        dirty = true;
        Save();
        AnnounceChanged(TranslationProjectChange::Content, nullptr);
}

void TranslationProject::ClearSanaeBinding() {
        if (!HasSanaeBinding()) return;
        sanae_binding = {};
        dirty = true;
        Save();
        AnnounceChanged(TranslationProjectChange::Content, nullptr);
}

void TranslationProject::UpdateSanaeFinalizeState(int project_revision, std::string finalized_revision_id) {
        if (!HasSanaeBinding()) return;
        sanae_binding.project_revision = project_revision;
        sanae_binding.base_finalized_revision_id = std::move(finalized_revision_id);
        dirty = true;
        Save();
}

void TranslationProject::AddReviewIssue(sanae::SanaeReviewIssue issue) {
        review_issues_.push_back(std::move(issue));
        dirty = true;
        Save();
}

void TranslationProject::UpdateReviewIssue(std::string const& id, sanae::SanaeReviewIssue updated) {
        for (auto& issue : review_issues_) {
                if (issue.id == id) {
                        issue = std::move(updated);
                        dirty = true;
                        Save();
                        return;
                }
        }
}

void TranslationProject::RemoveReviewIssue(std::string const& id) {
        review_issues_.erase(
                std::remove_if(review_issues_.begin(), review_issues_.end(),
                        [&](sanae::SanaeReviewIssue const& i) { return i.id == id; }),
                review_issues_.end());
        dirty = true;
        Save();
}
