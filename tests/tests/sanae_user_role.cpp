// Copyright (c) 2026, Aegisub Sanae contributors
//
// Tests for SanaeUserRole (client UX policy, NOT authorization).

#include <main.h>

#include "../../src/sanae_user_role.h"

using namespace sanae;

TEST(sanae_user_role, translator_cannot_wont_fix) {
    EXPECT_FALSE(CanWontFix(SanaeUserRole::Translator));
}

TEST(sanae_user_role, reviewer_can_wont_fix) {
    EXPECT_TRUE(CanWontFix(SanaeUserRole::Reviewer));
}
