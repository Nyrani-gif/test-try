// AssDialogue adapters for canonical baseline fingerprints.
// Kept out of the pure gtest-linked core to avoid application-layer linkage.

#include "sanae_baseline_fingerprint.h"

#include "ass_dialogue.h"

namespace sanae {

std::string compute_text_hash(const AssDialogue& line) {
    // GetStrippedText removes ASS override blocks while preserving literal \N.
    return compute_text_hash(line.GetStrippedText());
}

std::string compute_timing_hash(const AssDialogue& line) {
    return compute_timing_hash(line.Start, line.End);
}

} // namespace sanae
