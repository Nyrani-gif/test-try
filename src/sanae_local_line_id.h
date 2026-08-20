// sanae_local_line_id.h — Local sidecar identity for ReviewIssue persistence
// Phase 3 of SANAE_REVAMP_PLAN.md
//
// local_line_id is a stable, sidecar-maintained identity that binds a
// ReviewIssue to an AssDialogue line WITHOUT modifying the production ASS.
//
// Re-alignment on reload uses a deterministic multi-stage algorithm:
//   1. Exact identity match (direct AssDialogue::Id if still valid)
//   2. Exact (start, end, source_hash) — highest confidence
//   3. source_hash + contextual neighbors — for retimed/reordered
//   4. Unique source_hash fallback — exactly one unmatched pair
//   5. Duplicate handling — deterministic by timing proximity only if unambiguous
//   6. Ambiguous case → orphan, require explicit rebind
//
// NEVER attach a persisted ReviewIssue to the wrong dialogue to avoid an orphan.

#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>

class AssDialogue;
class AssFile;

namespace sanae {

struct LocalLineIdEntry {
    std::string id;              // e.g. "li_000001"
    int start_cs = 0;           // centiseconds at time of assignment
    int end_cs = 0;
    std::string source_hash;    // hash of normalized visible EN text
    int dialogue_id = -1;       // AssDialogue::Id (process-local, for session)
    bool orphaned = false;      // true if no match found on reload
};

struct CurrentLineInfo {
    AssDialogue *dialogue;
    int start_cs;
    int end_cs;
    std::string source_hash;
    int dialogue_id;
};

class LocalLineIdRegistry {
public:
    // Assign a new local_line_id for a line that doesn't have one yet.
    // Returns the new ID string.
    std::string AssignForLine(AssDialogue *line);

    // Look up the local_line_id for a given AssDialogue in the current session.
    // Returns empty string if not assigned.
    std::string LookupByDialogue(AssDialogue *line) const;

    // Re-align all entries after ASS reload.
    // Uses the multi-stage algorithm:
    //   1. Exact dialogue_id (if process-local IDs happen to match)
    //   2. Exact (start, end, source_hash)
    //   3. source_hash + neighbor context
    //   4. Unique source_hash fallback
    //   5. Duplicate → orphan if ambiguous
    //
    // `current_lines` = all lines in the newly loaded ASS file.
    // After this call, entries that couldn't be matched have `orphaned = true`.
    void Realign(const std::vector<CurrentLineInfo>& current_lines);

    // Get all entries (for sidecar serialization).
    const std::vector<LocalLineIdEntry>& Entries() const { return entries_; }

    // Load from sidecar JSON (deserialized externally).
    void LoadEntries(std::vector<LocalLineIdEntry> entries, int next_id);

    // Get next ID counter (for sidecar serialization).
    int NextId() const { return next_id_; }

    // Get orphaned entries (for UI rebind prompts).
    std::vector<LocalLineIdEntry> OrphanedEntries() const;

    // Rebind an orphaned entry to a new line.
    void Rebind(const std::string& local_line_id, const CurrentLineInfo& new_line);

private:
    std::vector<LocalLineIdEntry> entries_;
    int next_id_ = 1;

    std::string format_id(int n) const;
};

// Compute the source_hash for a line (used by AssignForLine and Realign).
// This is sha256 of SanaeNormalizeSource(visible_text)[:16] — NOT used as
// the identity itself, only as a matching key for re-alignment.
std::string compute_source_hash(AssDialogue *line);

} // namespace sanae
