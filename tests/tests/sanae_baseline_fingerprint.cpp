#include <main.h>
#include "../../src/sanae_baseline_fingerprint.h"
#include <string>
#include <libaegisub/ass/time.h>
using namespace sanae;
TEST(sanae_baseline_fingerprint, text_t1) { EXPECT_EQ("315f5bdb76d078c43b8ac0064e4a0164612b1fce77c869345bfc94c75894edd3", compute_text_hash("Hello, world!")); }
TEST(sanae_baseline_fingerprint, text_t2) { EXPECT_EQ("f900b9efa2b147ec7a5f69cc0f1a42227baa7b5a9b0a7bbdaf32f0381a6e7e66", compute_text_hash("Я защищу их.")); }
TEST(sanae_baseline_fingerprint, text_t3) { EXPECT_EQ("337b95ccef992ab78628c53ec6f89e712ac18dfd08f565a0d3a6c62c990dff18", compute_text_hash("Line with \\N break")); }
TEST(sanae_baseline_fingerprint, text_t4) { EXPECT_EQ("8feeca5ac770cfe9428ecd09393be5fcb958817b03fd3743c02058c269b7cd63", compute_text_hash("  trailing spaces  ")); }
TEST(sanae_baseline_fingerprint, text_t5) { EXPECT_EQ("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", compute_text_hash("")); }
TEST(sanae_baseline_fingerprint, timing_u1) { EXPECT_EQ("0f3d7cb8004a09e487bebc54fe81529a4331ef5991a0f621b174f8d604195ddd", compute_timing_hash(1020, 1140)); }
TEST(sanae_baseline_fingerprint, timing_u2) { EXPECT_EQ("0de72c9841417b2e5acd9bf2f7db6aab7c3cb8bd20ca1a853aa76e1e06f67056", compute_timing_hash(0, 100)); }
TEST(sanae_baseline_fingerprint, timing_u3) { EXPECT_EQ("22074227d8462b39403011e0bc4c5e7a3f1ee1bae54ae2deb0943dece537f93f", compute_timing_hash(1, 2)); }
TEST(sanae_baseline_fingerprint, timing_u4) { EXPECT_EQ("21fb136eb0a868a3fefa1bc89ea2f7ade8f80bd766a9c47de943899c81d58334", compute_timing_hash(99999, 100000)); }
TEST(sanae_baseline_fingerprint, negative_clamped) { EXPECT_EQ(compute_timing_hash(0, 100), compute_timing_hash(-5, 100)); }
TEST(sanae_baseline_fingerprint, determinism) { EXPECT_EQ(compute_text_hash("Hello"), compute_text_hash("Hello")); }
TEST(sanae_baseline_fingerprint, no_case_folding) { EXPECT_NE(compute_text_hash("Hello"), compute_text_hash("hello")); }


TEST(sanae_baseline_fingerprint, agi_time_operator_int_is_ms_rounded_to_cs) {
    EXPECT_EQ(0, static_cast<int>(agi::Time(4)));
    EXPECT_EQ(10, static_cast<int>(agi::Time(5)));
    EXPECT_EQ(10, static_cast<int>(agi::Time(14)));
    EXPECT_EQ(20, static_cast<int>(agi::Time(15)));
}

TEST(sanae_baseline_fingerprint, to_centiseconds_divides_by_ten) {
    struct Case { int ms; int cs; };
    const Case cases[] = {
        {0, 0}, {4, 0}, {5, 1}, {9, 1}, {10, 1}, {14, 1},
        {15, 2}, {19, 2}, {20, 2}, {99, 10}, {100, 10}, {105, 11},
    };
    for (auto const& c : cases)
        EXPECT_EQ(c.cs, to_centiseconds(agi::Time(c.ms))) << "ms=" << c.ms;
}

TEST(sanae_baseline_fingerprint, to_centiseconds_regression_ms_not_cs) {
    EXPECT_EQ(10, static_cast<int>(agi::Time(5)));
    EXPECT_EQ(1, to_centiseconds(agi::Time(5)));
    EXPECT_NE(static_cast<int>(agi::Time(5)), to_centiseconds(agi::Time(5)));
}

TEST(sanae_baseline_fingerprint, to_centiseconds_monotonic_across_boundary) {
    int previous = -1;
    for (int ms = 0; ms <= 200; ++ms) {
        int current = to_centiseconds(agi::Time(ms));
        EXPECT_GE(current, previous) << "ms=" << ms;
        previous = current;
    }
}

TEST(sanae_baseline_fingerprint, to_centiseconds_negative_safe) {
    // agi::Time clamps negative construction to zero.
    EXPECT_EQ(0, static_cast<int>(agi::Time(-5)));
    EXPECT_EQ(0, to_centiseconds(agi::Time(-5)));
}
