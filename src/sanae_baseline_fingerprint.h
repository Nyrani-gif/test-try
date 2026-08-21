// sanae_baseline_fingerprint.h — Canonical baseline fingerprint for ReviewIssue
// Authoritative: SANAE_SERVER_REQUIREMENTS_v0.3.md §9 (Baseline Fingerprint Spec)

#pragma once

#include <string>

class AssDialogue;
namespace agi { class Time; }

namespace sanae {

// Pure/testable canonical functions. These do not depend on AssDialogue.
// Text is UTF-8 visible text with ASS override blocks already stripped;
// literal \N is preserved, with no normalization or trimming.
std::string compute_text_hash(const std::string& visible_text);

// Timing canonical form is "<start_cs>|<end_cs>" hashed with SHA-256.
std::string compute_timing_hash(int start_centiseconds, int end_centiseconds);
std::string compute_timing_hash(const agi::Time& start, const agi::Time& end);

// agi::Time stores milliseconds. operator int() returns milliseconds rounded
// to 10 ms precision, so actual centiseconds are the rounded value / 10.
int to_centiseconds(const agi::Time& t);

// Thin production adapters, defined in sanae_baseline_fingerprint_adapters.cpp.
std::string compute_text_hash(const AssDialogue& line);
std::string compute_timing_hash(const AssDialogue& line);

} // namespace sanae
