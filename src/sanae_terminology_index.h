// sanae_terminology_index.h — Aho-Corasick multi-pattern matcher for project terms
// Phase 2 of SANAE_REVAMP_PLAN.md §3.3
//
// Uses SanaeNormalizeSource() from sanae_text.h for NFKC + whitespace + case fold.
// V1 ranking (STRICT, no partial/fuzzy/morphology):
//   longer exact phrase > shorter exact phrase > exact single word
//   Top 3–5 results.
//
// Heavy/light split (§3.3.2):
//   On ActiveLineChanged: Search(normalized_en) → cache matches. "Heavy", once.
//   On each RU keystroke: UpdateUsage(matches, current_ru). "Light", microseconds.
//   NO heavy search on every keystroke. NO debounce needed.
//
// Usage state (§3.3.3 — no auto-misused in V1):
//   Exact RU substring present → CorrectlyUsed (✓)
//   Absent → NotUsed (NEUTRAL, NOT warning)
//   NO "Misused" state in V1.

#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace sanae {

enum class TerminologyMatchKind : uint8_t {
    ExactWord,
    ExactPhrase,
};

enum class TerminologyUsage : uint8_t {
    NotUsed,
    CorrectlyUsed,
};

struct TerminologyMatch {
    std::string term_id;
    std::string english;
    std::string english_normalized;
    std::string russian;
    std::string note;
    std::vector<std::string> aliases;

    TerminologyMatchKind kind = TerminologyMatchKind::ExactWord;
    TerminologyUsage usage = TerminologyUsage::NotUsed;

    int matched_start = 0;
    int matched_end = 0;
    int word_count = 1;
};

// Entry for the index. Built from SanaeTerminologyEntry + SanaeTerminologyDraft.
struct TerminologyIndexEntry {
    std::string term_id;
    std::string english;
    std::string english_normalized;  // MUST be non-empty (use SanaeNormalizeSource)
    std::string russian;
    std::string note;
    std::vector<std::string> aliases_normalized;
    bool is_phrase = false;
};

class SanaeTerminologyIndex {
public:
    void Rebuild(const std::vector<TerminologyIndexEntry>& entries);

    // Search normalized EN source text for all indexed terms.
    // Returns matches sorted by V1 ranking: longer phrase > shorter > single word.
    // Caller truncates to top N (3-5) for display.
    //
    // `normalized_en_text` MUST be output of SanaeNormalizeSource().
    std::vector<TerminologyMatch> Search(const std::string& normalized_en_text) const;

    // Light-pass usage check. Updates `usage` on each match in-place.
    // `current_ru_text` should be normalized for comparison (fold_case, no NFKC needed
    // for Russian — the integration boundary decides exact normalization).
    static void UpdateUsage(std::vector<TerminologyMatch>& matches,
                            const std::string& current_ru_text);

    bool Empty() const noexcept { return nodes_.empty() || entries_.empty(); }
    size_t Size() const noexcept { return entries_.size(); }

private:
    struct Node {
        std::unordered_map<char, int> children;
        int fail = 0;
        int entry_index = -1;
    };

    std::vector<Node> nodes_;
    std::vector<TerminologyIndexEntry> entries_;

    void build_failure_links();
};

} // namespace sanae
