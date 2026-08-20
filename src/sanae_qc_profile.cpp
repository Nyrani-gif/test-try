// sanae_qc_profile.cpp — implementation
// Phase 3 of SANAE_REVAMP_PLAN.md §3.7

#include "sanae_qc_profile.h"

namespace sanae {

void SanaeQCProfile::ApplyPreset(QCProfilePreset p) {
    preset = p;
    switch (p) {
        case QCProfilePreset::TeamStandard:
            quotes = QuoteStyle::Guillemets;
            dashes = DashStyle::EmDash;
            ellipsis = EllipsisStyle::Char;
            cps_error_threshold = 25;
            cps_warning_threshold = 20;
            cps_low_threshold = 5;
            max_line_length = 42;
            max_line_breaks = 2;
            untranslated_check = SeverityLevel::Warning;
            repeated_punctuation = SeverityLevel::Info;
            empty_line = SeverityLevel::Error;
            whitespace = SeverityLevel::Warning;
            break;
        case QCProfilePreset::StrictQC:
            quotes = QuoteStyle::Guillemets;
            dashes = DashStyle::EmDash;
            ellipsis = EllipsisStyle::Char;
            cps_error_threshold = 20;
            cps_warning_threshold = 15;
            cps_low_threshold = 5;
            max_line_length = 39;
            max_line_breaks = 2;
            untranslated_check = SeverityLevel::Error;
            repeated_punctuation = SeverityLevel::Warning;
            empty_line = SeverityLevel::Error;
            whitespace = SeverityLevel::Warning;
            break;
        case QCProfilePreset::MinimalQC:
            quotes = QuoteStyle::Off;
            dashes = DashStyle::Off;
            ellipsis = EllipsisStyle::Off;
            cps_error_threshold = 30;
            cps_warning_threshold = 25;
            cps_low_threshold = 3;
            max_line_length = 50;
            max_line_breaks = 3;
            untranslated_check = SeverityLevel::Off;
            repeated_punctuation = SeverityLevel::Off;
            empty_line = SeverityLevel::Error;
            whitespace = SeverityLevel::Off;
            break;
        case QCProfilePreset::Custom:
            // Don't overwrite — keep current values.
            break;
    }
}

} // namespace sanae
