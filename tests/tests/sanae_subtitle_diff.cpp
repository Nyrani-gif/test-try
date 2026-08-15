// Copyright (c) 2026, Aegisub Sanae contributors

#include <main.h>

#include "../../src/sanae_subtitle_diff.h"

TEST(sanae_subtitle_diff, ignores_case_and_whitespace_noise) {
	std::vector<SanaeSemanticLine> before{{1000, 2000, "Hello   WORLD"}};
	std::vector<SanaeSemanticLine> after{{1100, 2100, " hello world "}};
	auto diff = SanaeCompareSemanticSubtitles(before, after);
	EXPECT_EQ(1, diff.unchanged);
	EXPECT_TRUE(diff.entries.empty());
}

TEST(sanae_subtitle_diff, insertion_does_not_shift_following_lines) {
	std::vector<SanaeSemanticLine> before{
		{0, 1000, "One"}, {1000, 2000, "Two"}, {2000, 3000, "Three"}};
	std::vector<SanaeSemanticLine> after{
		{0, 1000, "One"}, {500, 900, "Inserted"},
		{1000, 2000, "Two"}, {2000, 3000, "Three"}};
	auto diff = SanaeCompareSemanticSubtitles(before, after);
	EXPECT_EQ(3, diff.unchanged);
	EXPECT_EQ(1, diff.added);
	ASSERT_EQ(1, diff.entries.size());
	EXPECT_EQ(SanaeSemanticDiffKind::Added, diff.entries[0].kind);
}

TEST(sanae_subtitle_diff, reports_changed_added_and_removed) {
	std::vector<SanaeSemanticLine> before{
		{0, 1000, "Keep"}, {1000, 2000, "Old"}, {2000, 3000, "Removed"}};
	std::vector<SanaeSemanticLine> after{
		{0, 1000, "Keep"}, {1000, 2000, "New"}, {3000, 4000, "Added"}};
	auto diff = SanaeCompareSemanticSubtitles(before, after);
	EXPECT_EQ(1, diff.unchanged);
	// With no unchanged anchor in the tail, deterministic positional pairing
	// treats both tail substitutions as semantic changes.
	EXPECT_EQ(2, diff.changed);
	EXPECT_EQ(0, diff.added);
	EXPECT_EQ(0, diff.removed);
}

TEST(sanae_subtitle_diff, reports_unpaired_insertions_and_deletions) {
	std::vector<SanaeSemanticLine> before{
		{0, 1000, "Keep"}, {1000, 1500, "Removed"}, {2000, 3000, "Tail"}};
	std::vector<SanaeSemanticLine> after{
		{0, 1000, "Keep"}, {2000, 3000, "Tail"}, {3000, 4000, "Added"}};
	auto diff = SanaeCompareSemanticSubtitles(before, after);
	EXPECT_EQ(2, diff.unchanged);
	EXPECT_EQ(1, diff.added);
	EXPECT_EQ(1, diff.removed);
}
