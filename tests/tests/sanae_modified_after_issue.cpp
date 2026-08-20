// Copyright (c) 2026, Aegisub Sanae contributors
//
// Tests for modified_after_issue computation.
// Verifies SANAE_REVAMP_PLAN.md §3.6 + server req §3.6.

#include <main.h>

#include "../../src/sanae_modified_after_issue.h"
#include "../../src/sanae_baseline_fingerprint.h"

#include <ass_dialogue.h>
#include <libaegisub/ass/time.h>

#include <string>

TEST(sanae_modified_after_issue, unchanged_text_and_timing_returns_false) {
    AssDialogue line;
    line.Text = "Hello world";
    line.Start = agi::Time(10000);
    line.End = agi::Time(20000);

    auto text_hash = sanae::compute_text_hash(line);
    auto timing_hash = sanae::compute_timing_hash(line);

    EXPECT_FALSE(sanae::ComputeModifiedAfterIssue(&line, text_hash, timing_hash, "translation"));
}

TEST(sanae_modified_after_issue, text_change_for_translation_issue_returns_true) {
    AssDialogue line;
    line.Text = "Hello world";
    line.Start = agi::Time(10000);
    line.End = agi::Time(20000);

    auto text_hash = sanae::compute_text_hash(line);
    auto timing_hash = sanae::compute_timing_hash(line);

    // Change text
    line.Text = "Goodbye world";

    EXPECT_TRUE(sanae::ComputeModifiedAfterIssue(&line, text_hash, timing_hash, "translation"));
}

TEST(sanae_modified_after_issue, timing_change_for_translation_issue_returns_false) {
    // Translation issues: timing changes do NOT set modified_after_issue.
    AssDialogue line;
    line.Text = "Hello world";
    line.Start = agi::Time(10000);
    line.End = agi::Time(20000);

    auto text_hash = sanae::compute_text_hash(line);
    auto timing_hash = sanae::compute_timing_hash(line);

    // Change timing only
    line.Start = agi::Time(15000);
    line.End = agi::Time(25000);

    EXPECT_FALSE(sanae::ComputeModifiedAfterIssue(&line, text_hash, timing_hash, "translation"));
}

TEST(sanae_modified_after_issue, timing_change_for_timing_issue_returns_true) {
    AssDialogue line;
    line.Text = "Hello world";
    line.Start = agi::Time(10000);
    line.End = agi::Time(20000);

    auto text_hash = sanae::compute_text_hash(line);
    auto timing_hash = sanae::compute_timing_hash(line);

    // Change timing
    line.Start = agi::Time(15000);
    line.End = agi::Time(25000);

    EXPECT_TRUE(sanae::ComputeModifiedAfterIssue(&line, text_hash, timing_hash, "timing"));
}

TEST(sanae_modified_after_issue, text_change_for_timing_issue_returns_false) {
    // Timing issues: text changes do NOT set modified_after_issue.
    AssDialogue line;
    line.Text = "Hello world";
    line.Start = agi::Time(10000);
    line.End = agi::Time(20000);

    auto text_hash = sanae::compute_text_hash(line);
    auto timing_hash = sanae::compute_timing_hash(line);

    // Change text only
    line.Text = "Goodbye world";

    EXPECT_FALSE(sanae::ComputeModifiedAfterIssue(&line, text_hash, timing_hash, "timing"));
}

TEST(sanae_modified_after_issue, change_back_to_baseline_returns_false) {
    AssDialogue line;
    line.Text = "Hello world";
    line.Start = agi::Time(10000);
    line.End = agi::Time(20000);

    auto text_hash = sanae::compute_text_hash(line);
    auto timing_hash = sanae::compute_timing_hash(line);

    // Change then change back
    line.Text = "Temporary";
    EXPECT_TRUE(sanae::ComputeModifiedAfterIssue(&line, text_hash, timing_hash, "translation"));

    line.Text = "Hello world";
    EXPECT_FALSE(sanae::ComputeModifiedAfterIssue(&line, text_hash, timing_hash, "translation"));
}

TEST(sanae_modified_after_issue, style_issue_both_fields_matter) {
    AssDialogue line;
    line.Text = "Hello world";
    line.Start = agi::Time(10000);
    line.End = agi::Time(20000);

    auto text_hash = sanae::compute_text_hash(line);
    auto timing_hash = sanae::compute_timing_hash(line);

    // Text change
    line.Text = "Changed";
    EXPECT_TRUE(sanae::ComputeModifiedAfterIssue(&line, text_hash, timing_hash, "style"));

    // Reset and try timing
    line.Text = "Hello world";
    line.Start = agi::Time(99999);
    EXPECT_TRUE(sanae::ComputeModifiedAfterIssue(&line, text_hash, timing_hash, "style"));
}

TEST(sanae_modified_after_issue, null_line_returns_false) {
    EXPECT_FALSE(sanae::ComputeModifiedAfterIssue(nullptr, "hash", "hash", "translation"));
}

TEST(sanae_modified_after_issue, empty_baseline_hashes_return_false) {
    AssDialogue line;
    line.Text = "Hello";
    EXPECT_FALSE(sanae::ComputeModifiedAfterIssue(&line, "", "", "translation"));
}
