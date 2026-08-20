// sanae_baseline_fingerprint.cpp — implementation
// Authoritative: SANAE_SERVER_REQUIREMENTS_v0.3.md §9
//
// Reuses SanaeSha256() from sanae_recovery.h — the existing cross-platform
// SHA-256 implementation used in production recovery path. No third copy
// of SHA-256 in the codebase.

#include "sanae_baseline_fingerprint.h"

#include "ass_dialogue.h"
#include "sanae_recovery.h"
#include <libaegisub/ass/time.h>

#include <string>

namespace sanae {

int to_centiseconds(const agi::Time& t) {
    return static_cast<int>(t) / 10;
}

std::string compute_text_hash(const std::string& visible_text) {
    // Contract: caller passes already-stripped visible text (no override blocks).
    // In production, use AssDialogue::GetStrippedText().
    // We hash UTF-8 bytes as-is. NO normalization, NO trimming.
    return SanaeSha256(visible_text);
}

std::string compute_text_hash(const AssDialogue& line) {
    // GetStrippedText() removes override blocks {\...} and returns visible text.
    // It keeps \N as literal backslash-N (two bytes 0x5C 0x4E), matching the contract.
    return compute_text_hash(line.GetStrippedText());
}

std::string compute_timing_hash(int start_centiseconds, int end_centiseconds) {
    if (start_centiseconds < 0) start_centiseconds = 0;
    if (end_centiseconds < 0) end_centiseconds = 0;
    std::string s = std::to_string(start_centiseconds) + "|" + std::to_string(end_centiseconds);
    return SanaeSha256(s);
}

std::string compute_timing_hash(const agi::Time& start, const agi::Time& end) {
    return compute_timing_hash(to_centiseconds(start), to_centiseconds(end));
}

std::string compute_timing_hash(const AssDialogue& line) {
    return compute_timing_hash(line.Start, line.End);
}

} // namespace sanae
