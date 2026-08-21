// sanae_local_line_id.cpp — pure registry/re-alignment implementation

#include "sanae_local_line_id.h"

#include <algorithm>
#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <utility>

namespace sanae {

std::string LocalLineIdRegistry::format_id(int n) const {
    std::ostringstream ss;
    ss << "li_" << std::setfill('0') << std::setw(6) << n;
    return ss.str();
}

std::string LocalLineIdRegistry::AssignForLine(int dialogue_id, int start_cs,
                                                int end_cs, const std::string& source_hash) {
    for (auto const& e : entries_) {
        if (e.dialogue_id == dialogue_id && !e.orphaned)
            return e.id;
    }

    LocalLineIdEntry entry;
    entry.id = format_id(next_id_++);
    entry.start_cs = start_cs;
    entry.end_cs = end_cs;
    entry.source_hash = source_hash;
    entry.dialogue_id = dialogue_id;
    entry.orphaned = false;
    entries_.push_back(std::move(entry));
    return entries_.back().id;
}

std::string LocalLineIdRegistry::LookupByDialogueId(int dialogue_id) const {
    for (auto const& e : entries_) {
        if (e.dialogue_id == dialogue_id && !e.orphaned)
            return e.id;
    }
    return "";
}

void LocalLineIdRegistry::Realign(const std::vector<CurrentLineInfo>& current_lines) {
    std::vector<bool> matched_current(current_lines.size(), false);

    for (auto& e : entries_) {
        e.dialogue_id = -1;
        e.orphaned = true;
    }

    // Stage 1: exact (start, end, source_hash).
    for (size_t i = 0; i < current_lines.size(); ++i) {
        if (matched_current[i]) continue;
        auto const& cl = current_lines[i];
        for (auto& e : entries_) {
            if (!e.orphaned) continue;
            if (e.start_cs == cl.start_cs && e.end_cs == cl.end_cs
                && e.source_hash == cl.source_hash) {
                e.dialogue_id = cl.dialogue_id;
                e.orphaned = false;
                matched_current[i] = true;
                break;
            }
        }
    }

    // Stage 2/3 V1: unique source_hash fallback among unmatched lines.
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

    // Duplicate handling: only bind when exactly one same-source candidate
    // remains and its timing is within the established 5000 cs threshold.
    for (auto& e : entries_) {
        if (!e.orphaned) continue;
        int best_idx = -1;
        int best_distance = -1;
        int candidate_count = 0;
        for (size_t i = 0; i < current_lines.size(); ++i) {
            if (matched_current[i] || current_lines[i].source_hash != e.source_hash)
                continue;
            int dist = std::abs(current_lines[i].start_cs - e.start_cs)
                     + std::abs(current_lines[i].end_cs - e.end_cs);
            if (best_distance < 0 || dist < best_distance) {
                best_distance = dist;
                best_idx = static_cast<int>(i);
            }
            ++candidate_count;
        }
        if (candidate_count == 1 && best_distance >= 0 && best_distance < 5000) {
            e.dialogue_id = current_lines[best_idx].dialogue_id;
            e.start_cs = current_lines[best_idx].start_cs;
            e.end_cs = current_lines[best_idx].end_cs;
            e.orphaned = false;
            matched_current[best_idx] = true;
        }
    }
}

std::vector<LocalLineIdEntry> LocalLineIdRegistry::OrphanedEntries() const {
    std::vector<LocalLineIdEntry> result;
    for (auto const& e : entries_)
        if (e.orphaned) result.push_back(e);
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
