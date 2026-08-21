// sanae_local_line_id.h — Local sidecar identity for ReviewIssue persistence
// Phase 3 of SANAE_REVAMP_PLAN.md

#pragma once

#include <string>
#include <vector>

class AssDialogue;

namespace sanae {

struct LocalLineIdEntry {
    std::string id;
    int start_cs = 0;
    int end_cs = 0;
    std::string source_hash;
    int dialogue_id = -1;
    bool orphaned = false;
};

struct CurrentLineInfo {
    AssDialogue *dialogue = nullptr; // non-owning; pure registry logic never dereferences it
    int start_cs = 0;
    int end_cs = 0;
    std::string source_hash;
    int dialogue_id = -1;
};

class LocalLineIdRegistry {
public:
    // Pure/session-id API used by the registry and unit tests.
    std::string AssignForLine(int dialogue_id, int start_cs, int end_cs,
                              const std::string& source_hash);
    std::string LookupByDialogueId(int dialogue_id) const;

    // AssDialogue adapters (application target only).
    std::string AssignForLine(AssDialogue *line);
    std::string LookupByDialogue(AssDialogue *line) const;

    void Realign(const std::vector<CurrentLineInfo>& current_lines);
    const std::vector<LocalLineIdEntry>& Entries() const { return entries_; }
    void LoadEntries(std::vector<LocalLineIdEntry> entries, int next_id);
    int NextId() const { return next_id_; }
    std::vector<LocalLineIdEntry> OrphanedEntries() const;
    void Rebind(const std::string& local_line_id, const CurrentLineInfo& new_line);

private:
    std::vector<LocalLineIdEntry> entries_;
    int next_id_ = 1;
    std::string format_id(int n) const;
};

// AssDialogue adapter helper (application target only).
std::string compute_source_hash(AssDialogue *line);

} // namespace sanae
