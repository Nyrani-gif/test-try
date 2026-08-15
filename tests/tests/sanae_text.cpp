// Copyright (c) 2026, Aegisub Sanae contributors

#include <main.h>

#include "../../src/sanae_text.h"

TEST(sanae_text, normalization_matches_server_convention) {
	EXPECT_EQ("hello world", SanaeNormalizeSource("  HELLO\t World  "));
	EXPECT_EQ("hello world", SanaeNormalizeSource("hello\xC2\xA0world"));
	EXPECT_EQ("fullwidth", SanaeNormalizeSource("ＦＵＬＬＷＩＤＴＨ"));
	EXPECT_EQ("ёж", SanaeNormalizeSource("Ёж"));
}

TEST(sanae_text, similarity) {
	EXPECT_DOUBLE_EQ(1.0, SanaeSourceSimilarity("same", "same"));
	EXPECT_DOUBLE_EQ(0.0, SanaeSourceSimilarity("", "same"));
	EXPECT_NEAR(0.8, SanaeSourceSimilarity("hello", "hallo"), 1e-9);
	EXPECT_DOUBLE_EQ(0.0, SanaeSourceSimilarity("short", "a very long sentence", 0.92));
}

TEST(sanae_text, repeat_normalization_ignores_presentation_markup) {
	EXPECT_EQ(
		SanaeNormalizeRepeatSource("You're just using your age as an excuse\\Nto tell yourself that, aren't you?"),
		SanaeNormalizeRepeatSource("<i>You're just using your age as an excuse\\Nto tell yourself that, aren't you?</i>"));
	EXPECT_EQ("same text", SanaeNormalizeRepeatSource("{\\i1}<b>Same\\ntext</b>{\\i0}"));
	EXPECT_NE(SanaeNormalizeRepeatSource("Same text!"), SanaeNormalizeRepeatSource("Same text?"));
}

TEST(sanae_text, repeat_normalization_treats_ass_linebreak_as_whitespace) {
	EXPECT_EQ(
		SanaeNormalizeRepeatSource("This is about as far\\Nas I go."),
		SanaeNormalizeRepeatSource("This   is about as far as I go."));
}

TEST(sanae_text, source_repeat_key_does_not_include_rusub_presentation_metadata) {
	// Regression contract for flashbacks: repeat classification is keyed from
	// ENSUB visible text only. RUSUB style, timing drift and comments belong to
	// the translated line and must never enter this source key.
	struct Fixture {
		std::string ensub;
		std::string rusub_style;
		int rusub_start_ms;
		std::string rusub_comment;
	};
	Fixture ordinary{
		"You're just using your age as an excuse\\Nto tell yourself that, aren't you?",
		"Default", 1254870, {}};
	Fixture flashback{
		"<i>You're just using your age as an excuse\\Nto tell yourself that, aren't you?</i>",
		"Default – Flashback", 1254880, "{flashback}"};
	EXPECT_NE(ordinary.rusub_style, flashback.rusub_style);
	EXPECT_NE(ordinary.rusub_start_ms, flashback.rusub_start_ms);
	EXPECT_NE(ordinary.rusub_comment, flashback.rusub_comment);
	EXPECT_EQ(SanaeNormalizeRepeatSource(ordinary.ensub),
		SanaeNormalizeRepeatSource(flashback.ensub));
}

TEST(sanae_text, repeat_fragment_is_long_and_conservative) {
	EXPECT_TRUE(SanaeSourceFragmentMatch(
		"Hmm... It would be great if I could, but I think this is about as far as I go.",
		"This is about as far as I go."));
	EXPECT_FALSE(SanaeSourceFragmentMatch("I think", "Well, I think we should leave now."));
	EXPECT_FALSE(SanaeSourceFragmentMatch("Thank you very much.", "Thank you very much for coming."));
}

TEST(sanae_text, search_normalization) {
	EXPECT_EQ("еж мое имя", SanaeNormalizeSearchText("ЁЖ—моё, имя!"));
	EXPECT_EQ(SanaeLightRussianStem("император"), SanaeLightRussianStem("императора"));
	EXPECT_EQ(SanaeLightRussianStem("император"), SanaeLightRussianStem("императору"));
	EXPECT_EQ(SanaeLightRussianStem("император"), SanaeLightRussianStem("императором"));
	EXPECT_EQ(SanaeLightRussianStem("император"), SanaeLightRussianStem("императоры"));
	EXPECT_TRUE(SanaeSearchTokenMatches("император", "императором"));
}
