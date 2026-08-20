// Copyright (c) 2026, Aegisub Sanae contributors
//
// Tests for SanaeQCProfile presets.

#include <main.h>

#include "../../src/sanae_qc_profile.h"

using namespace sanae;

TEST(sanae_qc_profile, default_is_team_standard) {
    SanaeQCProfile p;
    EXPECT_EQ(QCProfilePreset::TeamStandard, p.preset);
}

TEST(sanae_qc_profile, team_standard_values) {
    SanaeQCProfile p;
    p.ApplyPreset(QCProfilePreset::TeamStandard);
    EXPECT_EQ(25, p.cps_error_threshold);
    EXPECT_EQ(20, p.cps_warning_threshold);
    EXPECT_EQ(42, p.max_line_length);
    EXPECT_EQ(SanaeQCProfile::SeverityLevel::Warning, p.untranslated_check);
    EXPECT_EQ(SanaeQCProfile::SeverityLevel::Info, p.repeated_punctuation);
    EXPECT_EQ(SanaeQCProfile::SeverityLevel::Error, p.empty_line);
}

TEST(sanae_qc_profile, strict_values) {
    SanaeQCProfile p;
    p.ApplyPreset(QCProfilePreset::StrictQC);
    EXPECT_EQ(20, p.cps_error_threshold);
    EXPECT_EQ(15, p.cps_warning_threshold);
    EXPECT_EQ(39, p.max_line_length);
    EXPECT_EQ(SanaeQCProfile::SeverityLevel::Error, p.untranslated_check);
    EXPECT_EQ(SanaeQCProfile::SeverityLevel::Warning, p.repeated_punctuation);
}

TEST(sanae_qc_profile, minimal_values) {
    SanaeQCProfile p;
    p.ApplyPreset(QCProfilePreset::MinimalQC);
    EXPECT_EQ(30, p.cps_error_threshold);
    EXPECT_EQ(50, p.max_line_length);
    EXPECT_EQ(SanaeQCProfile::SeverityLevel::Off, p.untranslated_check);
    EXPECT_EQ(SanaeQCProfile::SeverityLevel::Off, p.repeated_punctuation);
    EXPECT_EQ(SanaeQCProfile::SeverityLevel::Off, p.whitespace);
    EXPECT_EQ(SanaeQCProfile::QuoteStyle::Off, p.quotes);
    EXPECT_EQ(SanaeQCProfile::DashStyle::Off, p.dashes);
}

TEST(sanae_qc_profile, custom_preserves_values) {
    SanaeQCProfile p;
    p.ApplyPreset(QCProfilePreset::StrictQC);
    p.cps_error_threshold = 99;
    p.ApplyPreset(QCProfilePreset::Custom);
    EXPECT_EQ(99, p.cps_error_threshold);  // preserved
}

TEST(sanae_qc_profile, allow_translator_wont_fix_is_false) {
    EXPECT_FALSE(SanaeQCProfile::AllowTranslatorWontFix);
}

TEST(sanae_qc_profile, require_done_before_finalize_is_false) {
    EXPECT_FALSE(SanaeQCProfile::RequireDoneBeforeFinalize);
}
