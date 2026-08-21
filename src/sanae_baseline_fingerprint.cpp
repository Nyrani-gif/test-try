// sanae_baseline_fingerprint.cpp — pure canonical fingerprint implementation
// Authoritative: SANAE_SERVER_REQUIREMENTS_v0.3.md §9
//
// This translation unit intentionally has no AssDialogue dependency so the
// canonical hashing/time conversion rules remain directly unit-testable.

#include "sanae_baseline_fingerprint.h"

#include "sanae_recovery.h"
#include <libaegisub/ass/time.h>

#include <string>

namespace sanae {

int to_centiseconds(const agi::Time& t) {
    // agi::Time::operator int() returns milliseconds rounded to 10 ms
    // (centisecond precision). Convert the rounded millisecond value to
    // actual centiseconds for the wire/baseline contract.
    return static_cast<int>(t) / 10;
}

std::string compute_text_hash(const std::string& visible_text) {
    // Caller passes already-stripped visible UTF-8 text. No normalization and
    // no trimming: hash the bytes exactly as provided.
    return SanaeSha256(visible_text);
}

std::string compute_timing_hash(int start_centiseconds, int end_centiseconds) {
    if (start_centiseconds < 0) start_centiseconds = 0;
    if (end_centiseconds < 0) end_centiseconds = 0;
    return SanaeSha256(std::to_string(start_centiseconds) + "|" + std::to_string(end_centiseconds));
}

std::string compute_timing_hash(const agi::Time& start, const agi::Time& end) {
    return compute_timing_hash(to_centiseconds(start), to_centiseconds(end));
}

} // namespace sanae
