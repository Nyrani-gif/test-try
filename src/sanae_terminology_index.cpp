// sanae_terminology_index.cpp — Aho-Corasick implementation
// Phase 2 of SANAE_REVAMP_PLAN.md §3.3
//
// Uses SanaeNormalizeSource() from sanae_text.h for normalization.
// The Aho-Corasick algorithm itself is normalization-agnostic — it matches
// patterns against text character by character. The normalization policy
// (NFKC + fold_case + whitespace collapse) is applied BEFORE feeding text
// to Search(), and the same normalization is used to build english_normalized.

#include "sanae_terminology_index.h"

#include "sanae_text.h"

#include <algorithm>
#include <cctype>
#include <unordered_map>

#include <unicode/uchar.h>
#include <unicode/utf8.h>

namespace sanae {

namespace {

// Unicode-safe word boundary check.
// A character is a boundary if it is NOT a letter or digit (Unicode-aware).
// This correctly handles:
//   - ASCII punctuation (space, .,!?;:-()"'/\)
//   - Unicode punctuation (« » „ " " — № · – etc.)
//   - Non-ASCII letters (Cyrillic, CJK, etc. are NOT boundaries)
//   - Continuation bytes of multi-byte UTF-8 sequences (NOT boundaries —
//     they are part of a codepoint that was already classified by its lead byte)
//
// The function takes a single byte from the normalized text. For multi-byte
// UTF-8 codepoints, only the LEAD byte is classified; continuation bytes
// (0x80-0xBF) return false (not a boundary) because they belong to the
// same codepoint as the preceding lead byte.
bool is_word_boundary(uint8_t byte) {
    // ASCII fast path: check common punctuation directly.
    if (byte < 0x80) {
        // ASCII: letter or digit = not boundary; everything else = boundary.
        return !(std::isalnum(byte) || byte == '_');
    }
    // Non-ASCII byte. Could be a UTF-8 lead byte or continuation byte.
    // Continuation bytes (0x80-0xBF) are part of a multi-byte codepoint;
    // the lead byte already determined the classification.
    if ((byte & 0xC0) == 0x80) {
        // Continuation byte — not a boundary (part of preceding codepoint).
        return false;
    }
    // Lead byte of a multi-byte UTF-8 sequence. Decode the codepoint and
    // check if it's alphanumeric. If not (e.g. Unicode punctuation), it's
    // a boundary.
    // We don't have the full codepoint here (need continuation bytes), but
    // we can use a simpler heuristic: Unicode letters/digits in common
    // scripts (Cyrillic, Latin Extended, CJK) have lead bytes in specific
    // ranges. Unicode punctuation is in different ranges.
    //
    // For correctness, we decode the full codepoint. But since
    // is_word_boundary is called per-byte and we already handled
    // continuation bytes above, this branch is only hit for lead bytes.
    // We return false here and let the caller's boundary check work on
    // the PREVIOUS character (which is the byte before the lead byte).
    //
    // Actually, the caller checks text[start-1] and text[end]. For a
    // multi-byte codepoint at position start-1, text[start-1] is the
    // LEAD byte. We need to classify the full codepoint.
    //
    // Simplest correct approach: non-ASCII letters are NOT boundaries.
    // Non-ASCII punctuation IS a boundary. We can't distinguish by lead
    // byte alone. So we return false (assume letter) for all non-ASCII
    // lead bytes, which means a term adjacent to a Unicode letter won't
    // be boundary-checked. This is SAFE (no false boundary) but may miss
    // some Unicode punctuation boundaries.
    //
    // For V1 this is acceptable: SanaeNormalizeSource already converts
    // many Unicode punctuation forms. The common case (English terms in
    // English source text) works correctly. Full Unicode boundary
    // detection would require decoding codepoints, which is V1.5.
    return false;
}

// Decode a full UTF-8 codepoint starting at `pos` in `text`.
// Returns the codepoint and advances pos past it.
// Used for more accurate boundary checking at the edges of matches.
UChar32 decode_codepoint_at(const std::string& text, int pos) {
    if (pos < 0 || pos >= static_cast<int>(text.size())) return 0;
    UChar32 cp = 0;
    int32_t offset = pos;
    U8_NEXT(text, offset, static_cast<int32_t>(text.size()), cp);
    return cp;
}

// Check if the character at position `pos` is a word boundary (non-alphanumeric).
// Decodes the full UTF-8 codepoint for Unicode correctness.
bool is_boundary_position(const std::string& text, int pos) {
    if (pos < 0 || pos >= static_cast<int>(text.size())) return true; // start/end of text
    UChar32 cp = decode_codepoint_at(text, pos);
    if (cp < 0) return true; // invalid UTF-8, treat as boundary
    // Unicode alphanumeric check: letters, digits, marks.
    return !u_isalnum(cp);
}

} // namespace

void SanaeTerminologyIndex::Rebuild(const std::vector<TerminologyIndexEntry>& entries) {
    nodes_.clear();
    entries_.clear();
    nodes_.emplace_back();  // root = node 0

    for (const auto& e : entries) {
        if (e.english_normalized.empty()) continue;
        int cur = 0;
        for (char c : e.english_normalized) {
            auto it = nodes_[cur].children.find(c);
            if (it == nodes_[cur].children.end()) {
                int next = static_cast<int>(nodes_.size());
                nodes_.emplace_back();
                nodes_[cur].children[c] = next;
                cur = next;
            } else {
                cur = it->second;
            }
        }
        if (nodes_[cur].entry_index == -1) {
            entries_.push_back(e);
            nodes_[cur].entry_index = static_cast<int>(entries_.size()) - 1;
        }
    }

    build_failure_links();
}

void SanaeTerminologyIndex::build_failure_links() {
    std::vector<int> queue;
    queue.reserve(nodes_.size());
    for (auto& kv : nodes_[0].children) {
        int child = kv.second;
        nodes_[child].fail = 0;
        queue.push_back(child);
    }
    size_t head = 0;
    while (head < queue.size()) {
        int u = queue[head++];
        for (auto& kv : nodes_[u].children) {
            char c = kv.first;
            int v = kv.second;
            int f = nodes_[u].fail;
            while (f != 0 && nodes_[f].children.find(c) == nodes_[f].children.end()) {
                f = nodes_[f].fail;
            }
            auto it = nodes_[f].children.find(c);
            if (it != nodes_[f].children.end() && it->second != v) {
                nodes_[v].fail = it->second;
            } else {
                nodes_[v].fail = 0;
            }
            queue.push_back(v);
        }
    }
}

std::vector<TerminologyMatch> SanaeTerminologyIndex::Search(
    const std::string& text) const {

    std::vector<TerminologyMatch> result;
    if (nodes_.empty() || entries_.empty()) return result;

    int cur = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        char c = text[i];
        while (cur != 0 && nodes_[cur].children.find(c) == nodes_[cur].children.end()) {
            cur = nodes_[cur].fail;
        }
        auto it = nodes_[cur].children.find(c);
        if (it != nodes_[cur].children.end()) {
            cur = it->second;
        }
        int node = cur;
        while (node != 0) {
            if (nodes_[node].entry_index != -1) {
                const TerminologyIndexEntry& e = entries_[nodes_[node].entry_index];
                int pattern_len = static_cast<int>(e.english_normalized.size());
                int start = static_cast<int>(i + 1) - pattern_len;
                int end = static_cast<int>(i + 1);

                bool accept = true;
                if (!e.is_phrase) {
                    // Single-word terms require word boundaries on both sides.
                    // Uses Unicode-aware is_boundary_position (decodes full codepoint).
                    bool left_ok  = is_boundary_position(text, start - 1);
                    bool right_ok = is_boundary_position(text, end);
                    accept = left_ok && right_ok;
                }
                if (accept) {
                    TerminologyMatch m;
                    m.term_id = e.term_id;
                    m.english = e.english;
                    m.english_normalized = e.english_normalized;
                    m.russian = e.russian;
                    m.note = e.note;
                    m.aliases = e.aliases_normalized;
                    m.kind = e.is_phrase ? TerminologyMatchKind::ExactPhrase
                                          : TerminologyMatchKind::ExactWord;
                    m.usage = TerminologyUsage::NotUsed;
                    m.matched_start = start;
                    m.matched_end = end;
                    m.word_count = e.is_phrase
                        ? static_cast<int>(std::count(e.english_normalized.begin(),
                                                       e.english_normalized.end(), ' ') + 1)
                        : 1;
                    result.push_back(std::move(m));
                }
            }
            node = nodes_[node].fail;
        }
    }

    // Deduplicate by (term_id, start, end).
    std::sort(result.begin(), result.end(), [](const TerminologyMatch& a, const TerminologyMatch& b) {
        if (a.term_id != b.term_id) return a.term_id < b.term_id;
        if (a.matched_start != b.matched_start) return a.matched_start < b.matched_start;
        return a.matched_end < b.matched_end;
    });
    result.erase(std::unique(result.begin(), result.end(),
        [](const TerminologyMatch& a, const TerminologyMatch& b) {
            return a.term_id == b.term_id
                && a.matched_start == b.matched_start
                && a.matched_end == b.matched_end;
        }), result.end());

    // V1 ranking: longer phrase > shorter phrase > single word.
    std::stable_sort(result.begin(), result.end(), [](const TerminologyMatch& a, const TerminologyMatch& b) {
        if (a.word_count != b.word_count) return a.word_count > b.word_count;
        return a.matched_start < b.matched_start;
    });

    return result;
}

void SanaeTerminologyIndex::UpdateUsage(std::vector<TerminologyMatch>& matches,
                                         const std::string& current_ru_text) {
    // Use SanaeNormalizeSearchText for RU comparison (broader: ё=е, punctuation→space).
    // This is the existing normalization used by sanae_text.cpp for search.
    std::string ru = SanaeNormalizeSearchText(current_ru_text);
    for (auto& m : matches) {
        std::string accepted = SanaeNormalizeSearchText(m.russian);
        if (!accepted.empty() && ru.find(accepted) != std::string::npos) {
            m.usage = TerminologyUsage::CorrectlyUsed;
            continue;
        }
        bool alias_hit = false;
        for (const auto& alias : m.aliases) {
            std::string a = SanaeNormalizeSearchText(alias);
            if (!a.empty() && ru.find(a) != std::string::npos) {
                alias_hit = true;
                break;
            }
        }
        // NO auto-misused. Absent literal is NEUTRAL.
        m.usage = alias_hit ? TerminologyUsage::CorrectlyUsed : TerminologyUsage::NotUsed;
    }
}

} // namespace sanae
