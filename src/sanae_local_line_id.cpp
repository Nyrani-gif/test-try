// sanae_local_line_id.cpp — implementation
// Phase 3 of SANAE_REVAMP_PLAN.md

#include "sanae_local_line_id.h"

#include "ass_dialogue.h"
#include "sanae_baseline_fingerprint.h"
#include "sanae_text.h"

#include <algorithm>
#include <sstream>
#include <iomanip>
#include <unordered_set>

namespace sanae {

std::string LocalLineIdRegistry::format_id(int n) const {
    std::ostringstream ss;
    ss << "li_" << std::setfill('0') << std::setw(6) << n;
    return ss.str();
}

std::string compute_source_hash(AssDialogue *line) {
    if (!line) return "";
    auto visible = line->GetStrippedText();
    auto normalized = SanaeNormalizeSource(visible);
    return SanaeSha256(normalized).substr(0, 16);
}

std::string LocalLineIdRegistry::AssignForLine(AssDialogue *line) {
    if (!line) return "";

    // Check if this line already has an ID (by dialogue_id).
    for (auto const& e : entries_) {
        if (e.dialogue_id == line->Id && !e.orphaned)
            return e.id;
    }

    // Assign new.
    LocalLineIdEntry entry;
    entry.id = format_id(next_id_++);
    entry.start_cs = to_centiseconds(line->Start);
    entry.end_cs = to_centiseconds(line->End);
    entry.source_hash = compute_source_hash(line);
    entry.dialogue_id = line->Id;
    entry.orphaned = false;
    entries_.push_back(std::move(entry));
    return entries_.back().id;
}

std::string LocalLineIdRegistry::LookupByDialogue(AssDialogue *line) const {
    if (!line) return "";
    for (auto const& e : entries_) {
        if (e.dialogue_id == line->Id && !e.orphaned)
            return e.id;
    }
    return "";
}

void LocalLineIdRegistry::Realign(const std::vector<CurrentLineInfo>& current_lines) {
    // Track which current lines are matched.
    std::vector<bool> matched_current(current_lines.size(), false);

    // Reset dialogue_id and orphaned state for all entries.
    for (auto& e : entries_) {
        e.dialogue_id = -1;
        e.orphaned = true;  // will be cleared on match
    }

    // === Stage 1: Exact (start, end, source_hash) ===
    for (size_t i = 0; i < current_lines.size(); ++i) {
        if (matched_current[i]) continue;
        auto const& cl = current_lines[i];
        for (auto& e : entries_) {
            if (!e.orphaned) continue;
            if (e.start_cs == cl.start_cs
                && e.end_cs == cl.end_cs
                && e.source_hash == cl.source_hash) {
                e.dialogue_id = cl.dialogue_id;
                e.orphaned = false;
                matched_current[i] = true;
                break;
            }
        }
    }

    // === Stage 2: source_hash + contextual neighbors ===
    // For each unmatched entry, check if its source_hash matches exactly one
    // unmatched current line, AND the neighbors (entries before/after in the
    // sidecar that ARE matched) have matching neighbors in the current file.
    //
    // Simplified V1: if source_hash matches exactly one unmatched current line,
    // AND the entry's previous/next matched neighbors also match in position
    // relative to the candidate, rebind.
    //
    // For V1 we implement a simpler version: unique source_hash fallback (Stage 3).
    // Full neighbor-context matching is V1.5.

    // === Stage 3: Unique source_hash fallback ===
    // For each unmatched entry, if exactly one unmatched current line shares
    // the same source_hash, rebind them.
    for (auto& e : entries_) {
        if (!e.orphaned) continue;
        int match_count = 0;
        int match_idx = -1;
        for (size_t i = 0; i < current_lines.size(); ++i) {
            if (matched_current[i]) continue;
            if (current_lines[i].source_hash == e.source_hash) {
                ++match_count;
                match_idx = static_cast<int>(i);
            }
        }
        if (match_count == 1) {
            e.dialogue_id = current_lines[match_idx].dialogue_id;
            e.start_cs = current_lines[match_idx].start_cs;
            e.end_cs = current_lines[match_idx].end_cs;
            e.orphaned = false;
            matched_current[match_idx] = true;
        }
    }

    // === Stage 4: Duplicate handling ===
    // For entries with duplicate source_hashes where multiple candidates exist,
    // try timing proximity: if an unmatched entry's (start,end) is closest to
    // exactly one unmatched candidate (by absolute centisecond distance),
    // AND the distance is < 5000 cs (50 seconds), rebind.
    for (auto& e : entries_) {
        if (!e.orphaned) continue;
        int best_idx = -1;
        int best_distance = -1;
        int candidate_count = 0;
        for (size_t i = 0; i < current_lines.size(); ++i) {
            if (matched_current[i]) continue;
            if (current_lines[i].source_hash != e.source_hash) continue;
            int dist = std::abs(current_lines[i].start_cs - e.start_cs)
                     + std::abs(current_lines[i].end_cs - e.end_cs);
            if (best_distance < 0 || dist < best_distance) {
                best_distance = dist;
                best_idx = static_cast<int>(i);
            }
            ++candidate_count;
        }
        // Only rebind if exactly one candidate AND distance is reasonable.
        if (candidate_count == 1 && best_distance >= 0 && best_distance < 5000) {
            e.dialogue_id = current_lines[best_idx].dialogue_id;
            e.start_cs = current_lines[best_idx].start_cs;
            e.end_cs = current_lines[best_idx].end_cs;
            e.orphaned = false;
            matched_current[best_idx] = true;
        }
        // Otherwise: remains orphaned. Do NOT silently choose.
    }

    // === Stage 5: Ambiguous case ===
    // All remaining orphaned entries stay orphaned. UI prompts rebind.
}

std::vector<LocalLineIdEntry> LocalLineIdRegistry::OrphanedEntries() const {
    std::vector<LocalLineIdEntry> result;
    for (auto const& e : entries_) {
        if (e.orphaned) result.push_back(e);
    }
    return result;
}

void LocalLineIdRegistry::Rebind(const std::string& local_line_id,
                                  const CurrentLineInfo& new_line) {
    for (auto& e : entries_) {
        if (e.id == local_line_id) {
            e.dialogue_id = new_line.dialogue_id;
            e.start_cs = new_line.start_cs;
            e.end_cs = new_line.end_cs;
            e.source_hash = new_line.source_hash;
            e.orphaned = false;
            return;
        }
    }
}

void LocalLineIdRegistry::LoadEntries(std::vector<LocalLineIdEntry> entries, int next_id) {
    entries_ = std::move(entries);
    next_id_ = next_id;
}

} // namespace sanae
