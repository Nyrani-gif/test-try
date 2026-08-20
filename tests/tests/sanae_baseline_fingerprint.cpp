// Copyright (c) 2026, Aegisub Sanae contributors
//
// Tests for sanae_baseline_fingerprint. Verifies the exact test vectors
// hardcoded in SANAE_SERVER_REQUIREMENTS_v0.3.md §9.3 (text) and §9.4 (timing),
// plus integration tests through real AssDialogue / agi::Time types.

#include <main.h>

#include "../../src/sanae_baseline_fingerprint.h"

#include <ass_dialogue.h>
#include <libaegisub/ass/time.h>

#include <string>

// ---- §9.3 baseline_text_hash test vectors ----

TEST(sanae_baseline_fingerprint, text_hash_t1_hello_world) {
        EXPECT_EQ("315f5bdb76d078c43b8ac0064e4a0164612b1fce77c869345bfc94c75894edd3",
                  sanae::compute_text_hash("Hello, world!"));
}

TEST(sanae_baseline_fingerprint, text_hash_t2_cyrillic) {
        EXPECT_EQ("f900b9efa2b147ec7a5f69cc0f1a42227baa7b5a9b0a7bbdaf32f0381a6e7e66",
                  sanae::compute_text_hash("Я защищу их."));
}

TEST(sanae_baseline_fingerprint, text_hash_t3_literal_backslash_n) {
        // "Line with \N break" where \N is two bytes: 0x5C 0x4E (backslash + N),
        // NOT a newline (0x0A).
        EXPECT_EQ("337b95ccef992ab78628c53ec6f89e712ac18dfd08f565a0d3a6c62c990dff18",
                  sanae::compute_text_hash("Line with \\N break"));
}

TEST(sanae_baseline_fingerprint, text_hash_t4_trailing_spaces_preserved) {
        EXPECT_EQ("8feeca5ac770cfe9428ecd09393be5fcb958817b03fd3743c02058c269b7cd63",
                  sanae::compute_text_hash("  trailing spaces  "));
}

TEST(sanae_baseline_fingerprint, text_hash_t5_empty_string) {
        EXPECT_EQ("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
                  sanae::compute_text_hash(""));
}

// ---- §9.4 baseline_timing_hash test vectors ----

TEST(sanae_baseline_fingerprint, timing_hash_u1) {
        EXPECT_EQ("0f3d7cb8004a09e487bebc54fe81529a4331ef5991a0f621b174f8d604195ddd",
                  sanae::compute_timing_hash(1020, 1140));
}

TEST(sanae_baseline_fingerprint, timing_hash_u2_zero_start) {
        EXPECT_EQ("0de72c9841417b2e5acd9bf2f7db6aab7c3cb8bd20ca1a853aa76e1e06f67056",
                  sanae::compute_timing_hash(0, 100));
}

TEST(sanae_baseline_fingerprint, timing_hash_u3_small_values) {
        EXPECT_EQ("22074227d8462b39403011e0bc4c5e7a3f1ee1bae54ae2deb0943dece537f93f",
                  sanae::compute_timing_hash(1, 2));
}

TEST(sanae_baseline_fingerprint, timing_hash_u4_large_values) {
        EXPECT_EQ("21fb136eb0a868a3fefa1bc89ea2f7ade8f80bd766a9c47de943899c81d58334",
                  sanae::compute_timing_hash(99999, 100000));
}

// ---- Edge cases ----

TEST(sanae_baseline_fingerprint, timing_hash_negative_clamped) {
        EXPECT_EQ(sanae::compute_timing_hash(0, 100),
                  sanae::compute_timing_hash(-5, 100));
}

TEST(sanae_baseline_fingerprint, determinism_same_input_same_hash) {
        EXPECT_EQ(sanae::compute_text_hash("Hello, world!"),
                  sanae::compute_text_hash("Hello, world!"));
        EXPECT_EQ(sanae::compute_timing_hash(1020, 1140),
                  sanae::compute_timing_hash(1020, 1140));
}

// ---- Integration with agi::Time ----

TEST(sanae_baseline_fingerprint, to_centiseconds_agi_time_exact) {
        // agi::Time stores milliseconds; operator int() returns ms rounded to
        // centisecond precision. to_centiseconds divides by 10 to get actual cs.
        // 10200 ms → operator int() = 10200 → /10 = 1020 cs.
        agi::Time t(10200);
        EXPECT_EQ(1020, sanae::to_centiseconds(t));
}

TEST(sanae_baseline_fingerprint, to_centiseconds_agi_time_round_up_at_5ms) {
        // agi::Time::operator int() rounds up at 5ms: (10205+5)-(10205+5)%10 = 10210.
        // to_centiseconds = 10210 / 10 = 1021 cs.
        agi::Time t(10205);
        EXPECT_EQ(1021, sanae::to_centiseconds(t));
}

TEST(sanae_baseline_fingerprint, to_centiseconds_agi_time_zero) {
        agi::Time t3(0);
        EXPECT_EQ(0, sanae::to_centiseconds(t3));
}

TEST(sanae_baseline_fingerprint, to_centiseconds_5ms_boundary) {
        // 10204 ms → (10204+5)=10209, 10209%10=9, 10209-9=10200 → /10 = 1020 cs.
        EXPECT_EQ(1020, sanae::to_centiseconds(agi::Time(10204)));
        // 10205 ms → (10205+5)=10210, 10210%10=0, 10210-0=10210 → /10 = 1021 cs.
        EXPECT_EQ(1021, sanae::to_centiseconds(agi::Time(10205)));
}

// ---- Integration with AssDialogue ----

TEST(sanae_baseline_fingerprint, text_hash_via_ass_dialogue_override_tags_removed) {
        // AssDialogue with override tags — GetStrippedText() removes them.
        // "{\\i1}Hello{\\i0}, world!" → visible = "Hello, world!" → matches T1.
        AssDialogue line("{\\i1}Hello{\\i0}, world!");
        EXPECT_EQ("315f5bdb76d078c43b8ac0064e4a0164612b1fce77c869345bfc94c75894edd3",
                  sanae::compute_text_hash(line));
}

TEST(sanae_baseline_fingerprint, text_hash_via_ass_dialogue_literal_backslash_n_preserved) {
        // \N in ASS text is literal backslash + N (two bytes), NOT a newline.
        // GetStrippedText() preserves it as-is.
        AssDialogue line("Line with \\N break");
        EXPECT_EQ("337b95ccef992ab78628c53ec6f89e712ac18dfd08f565a0d3a6c62c990dff18",
                  sanae::compute_text_hash(line));
}

TEST(sanae_baseline_fingerprint, text_hash_via_ass_dialogue_whitespace_preserved) {
        AssDialogue line("  trailing spaces  ");
        EXPECT_EQ("8feeca5ac770cfe9428ecd09393be5fcb958817b03fd3743c02058c269b7cd63",
                  sanae::compute_text_hash(line));
}

TEST(sanae_baseline_fingerprint, text_hash_via_ass_dialogue_utf8_unchanged) {
        // Cyrillic UTF-8 text must pass through unchanged (no NFKC, no fold-case).
        AssDialogue line("Я защищу их.");
        EXPECT_EQ("f900b9efa2b147ec7a5f69cc0f1a42227baa7b5a9b0a7bbdaf32f0381a6e7e66",
                  sanae::compute_text_hash(line));
}

TEST(sanae_baseline_fingerprint, text_hash_via_ass_dialogue_no_normalization) {
        // "Hello" and "hello" must produce DIFFERENT hashes (no case folding).
        EXPECT_NE(sanae::compute_text_hash("Hello"),
                  sanae::compute_text_hash("hello"));
}

TEST(sanae_baseline_fingerprint, timing_hash_via_ass_dialogue) {
        // AssDialogue with Start=10200ms, End=11400ms.
        // to_centiseconds(10200ms) = 1020 cs, to_centiseconds(11400ms) = 1140 cs.
        // compute_timing_hash(1020, 1140) = U1 vector.
        AssDialogue line;
        line.Start = agi::Time(10200);
        line.End = agi::Time(11400);
        EXPECT_EQ("0f3d7cb8004a09e487bebc54fe81529a4331ef5991a0f621b174f8d604195ddd",
                  sanae::compute_timing_hash(line));
}

TEST(sanae_baseline_fingerprint, timing_hash_via_ass_dialogue_5ms_boundary) {
        // Start=10204ms → 1020 cs, End=10205ms → 1021 cs.
        // Documents the 5ms rounding boundary from agi::Time::operator int().
        AssDialogue line;
        line.Start = agi::Time(10204);
        line.End = agi::Time(10205);
        // compute_timing_hash(1020, 1021) — verify this matches direct call.
        EXPECT_EQ(sanae::compute_timing_hash(1020, 1021),
                  sanae::compute_timing_hash(line));
}
