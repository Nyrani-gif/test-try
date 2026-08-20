// sanae_qc_profile.h — QC profile with presets
// Phase 3 of SANAE_REVAMP_PLAN.md §3.7

#pragma once

#include <cstdint>
#include <string>

namespace sanae {

enum class QCProfilePreset : uint8_t {
    TeamStandard, StrictQC, MinimalQC, Custom
};

struct SanaeQCProfile {
    QCProfilePreset preset = QCProfilePreset::TeamStandard;

    // Typography
    enum class QuoteStyle : uint8_t { Off, Guillemets, Straight, Curly };
    QuoteStyle quotes = QuoteStyle::Guillemets;

    enum class DashStyle : uint8_t { Off, EmDash, Hyphen };
    DashStyle dashes = DashStyle::EmDash;

    enum class EllipsisStyle : uint8_t { Off, Char, ThreeDots };
    EllipsisStyle ellipsis = EllipsisStyle::Char;

    // CPS / length
    int cps_error_threshold = 25;
    int cps_warning_threshold = 20;
    int cps_low_threshold = 5;
    int max_line_length = 42;
    int max_line_breaks = 2;

    // Special checks
    enum class SeverityLevel : uint8_t { Off, Info, Warning, Error };
    SeverityLevel untranslated_check = SeverityLevel::Warning;
    SeverityLevel repeated_punctuation = SeverityLevel::Info;
    SeverityLevel empty_line = SeverityLevel::Error;
    SeverityLevel whitespace = SeverityLevel::Warning;

    // Apply a preset (overwrites all fields).
    void ApplyPreset(QCProfilePreset p);

    // WontFix UX-policy: hardcoded false in V0.3 (Translator cannot WontFix).
    // This is NOT stored in local Preferences — it's a compile-time constant.
    // V0.4 will move this to server-sync QCProfile.
    static constexpr bool AllowTranslatorWontFix = false;

    // Finalize does NOT require review_state=done (client shows warning only).
    static constexpr bool RequireDoneBeforeFinalize = false;
};

} // namespace sanae
