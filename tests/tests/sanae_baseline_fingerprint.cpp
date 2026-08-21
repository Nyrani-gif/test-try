#include <main.h>
#include "../../src/sanae_baseline_fingerprint.h"
#include <string>
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
