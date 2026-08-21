// Tests for modified_after_issue field-specific hash comparison.

#include <main.h>

#include "../../src/sanae_modified_after_issue.h"
#include "../../src/sanae_baseline_fingerprint.h"

using namespace sanae;

TEST(sanae_modified_after_issue, unchanged_text_and_timing_returns_false) {
    auto text = compute_text_hash("Hello world");
    auto timing = compute_timing_hash(1000, 2000);
    EXPECT_FALSE(ComputeModifiedAfterIssue(text, timing, text, timing, "translation"));
}

TEST(sanae_modified_after_issue, text_change_for_translation_issue_returns_true) {
    auto baseline = compute_text_hash("Hello world");
    auto timing = compute_timing_hash(1000, 2000);
    EXPECT_TRUE(ComputeModifiedAfterIssue(
        compute_text_hash("Goodbye world"), timing, baseline, timing, "translation"));
}

TEST(sanae_modified_after_issue, timing_change_for_translation_issue_returns_false) {
    auto text = compute_text_hash("Hello world");
    auto baseline_timing = compute_timing_hash(1000, 2000);
    EXPECT_FALSE(ComputeModifiedAfterIssue(
        text, compute_timing_hash(1500, 2500), text, baseline_timing, "translation"));
}

TEST(sanae_modified_after_issue, timing_change_for_timing_issue_returns_true) {
    auto text = compute_text_hash("Hello world");
    auto baseline_timing = compute_timing_hash(1000, 2000);
    EXPECT_TRUE(ComputeModifiedAfterIssue(
        text, compute_timing_hash(1500, 2500), text, baseline_timing, "timing"));
}

TEST(sanae_modified_after_issue, text_change_for_timing_issue_returns_false) {
    auto baseline = compute_text_hash("Hello world");
    auto timing = compute_timing_hash(1000, 2000);
    EXPECT_FALSE(ComputeModifiedAfterIssue(
        compute_text_hash("Goodbye world"), timing, baseline, timing, "timing"));
}

TEST(sanae_modified_after_issue, change_back_to_baseline_returns_false) {
    auto baseline = compute_text_hash("Hello world");
    auto timing = compute_timing_hash(1000, 2000);
    EXPECT_TRUE(ComputeModifiedAfterIssue(
        compute_text_hash("Temporary"), timing, baseline, timing, "translation"));
    EXPECT_FALSE(ComputeModifiedAfterIssue(
        baseline, timing, baseline, timing, "translation"));
}

TEST(sanae_modified_after_issue, style_issue_both_fields_matter) {
    auto baseline = compute_text_hash("Hello world");
    auto timing = compute_timing_hash(1000, 2000);
    EXPECT_TRUE(ComputeModifiedAfterIssue(
        compute_text_hash("Changed"), timing, baseline, timing, "style"));
    EXPECT_TRUE(ComputeModifiedAfterIssue(
        baseline, compute_timing_hash(9999, 99999), baseline, timing, "style"));
}

TEST(sanae_modified_after_issue, empty_baseline_hashes_return_false) {
    EXPECT_FALSE(ComputeModifiedAfterIssue("current-text", "current-timing", "", "", "translation"));
}
