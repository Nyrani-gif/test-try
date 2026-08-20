// sanae_baseline_fingerprint.h — Canonical baseline fingerprint for ReviewIssue
// Authoritative: SANAE_SERVER_REQUIREMENTS_v0.3.md §9 (Baseline Fingerprint Spec)
//
// These hashes enable multi-device `modified_after_issue` computation.
// The byte representation is FIXED BY CONTRACT, independent of Aegisub's
// internal representation, so that any client (Sanae or future second client)
// produces the same hash for the same line.
//
// Integration with real Aegisub types:
//   - text input: AssDialogue::GetStrippedText() (visible text, no override blocks)
//   - timing input: agi::Time (operator int() returns centiseconds per §0.12)
//
// SHA-256 primitive: reuses SanaeSha256() from sanae_recovery.h (cross-platform
// pure-C++ implementation, already used in production recovery path). Do NOT
// introduce a third SHA-256 implementation.
//
// Test vectors (hardcoded, server req §9.3 / §9.4) verified by
// tests/tests/sanae_baseline_fingerprint.cpp.

#pragma once

#include <string>

class AssDialogue;

namespace agi { class Time; }

namespace sanae {

// SHA-256 of the line's visible text at issue creation.
//
// Contract (§9.1):
//   1. Input = visible text (override blocks {\...} already stripped).
//      In production, pass AssDialogue::GetStrippedText().
//   2. Keep \N as literal two-byte sequence (0x5C 0x4E).
//   3. NO Unicode normalization (exact byte comparison).
//   4. NO whitespace trimming (leading/trailing spaces preserved).
//   5. Encode as UTF-8 (Aegisub stores text as UTF-8).
//   6. SHA-256, lowercase hex, 64 chars.
//
// Note: GetStrippedText() already removes override blocks but keeps \N as
// literal text. This matches the contract. Do NOT additionally normalize.
std::string compute_text_hash(const std::string& visible_text);
std::string compute_text_hash(const AssDialogue& line);

// SHA-256 of the line's timing at issue creation.
//
// Contract (§9.2):
//   canonical_start_cs = to_centiseconds(line.Start)
//   canonical_end_cs   = to_centiseconds(line.End)
//   canonical_string   = "<start_decimal>|<end_decimal>"
//   SHA-256(canonical_string as ASCII), lowercase hex.
//
// agi::Time::operator int() returns centiseconds (see libaegisub/ass/time.h:33,
// confirmed in Phase 0.12). The conversion rounds up at 5ms, which is the
// ASS-native centisecond precision. This is the canonical boundary.
std::string compute_timing_hash(int start_centiseconds, int end_centiseconds);
std::string compute_timing_hash(const agi::Time& start, const agi::Time& end);
std::string compute_timing_hash(const AssDialogue& line);

// Canonical conversion: agi::Time → centiseconds.
// agi::Time stores milliseconds internally; operator int() returns milliseconds
// rounded to centisecond precision ((time+5)-(time+5)%10). To get actual
// centiseconds (1 cs = 10 ms), we divide by 10.
//
// Example: time = 10205 ms
//   operator int() = (10205+5) - (10205+5)%10 = 10210 - 0 = 10210 (ms, rounded)
//   to_centiseconds = 10210 / 10 = 1021 (cs)
//
// This matches the server contract §9.2 which requires centiseconds.
int to_centiseconds(const agi::Time& t);

} // namespace sanae
