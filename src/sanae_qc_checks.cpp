// sanae_qc_checks.cpp — pure QC rule engine

#include "sanae_qc_checks.h"

#include <string>

namespace sanae {
namespace {

DiagnosticSeverity profile_severity(SanaeQCProfile::SeverityLevel sl) {
    switch (sl) {
        case SanaeQCProfile::SeverityLevel::Off:     return DiagnosticSeverity::Info;
        case SanaeQCProfile::SeverityLevel::Info:    return DiagnosticSeverity::Info;
        case SanaeQCProfile::SeverityLevel::Warning: return DiagnosticSeverity::Warning;
        case SanaeQCProfile::SeverityLevel::Error:   return DiagnosticSeverity::Error;
    }
    return DiagnosticSeverity::Info;
}

} // namespace

std::vector<SanaeDiagnostic> ComputeDiagnostics(
    const QCRuleInput& input, const SanaeQCProfile& profile) {

    std::vector<SanaeDiagnostic> result;
    auto *line = input.line;
    bool drawing = input.is_drawing;

    if (input.duration_ms <= 0) {
        result.push_back({DiagnosticKind::Duration, DiagnosticSeverity::Error,
            "duration", "End time must be after start time", line});
    }

    if (!drawing && input.duration_ms > 0 && input.cps > 0) {
        if (input.cps > profile.cps_error_threshold) {
            result.push_back({DiagnosticKind::CpsHigh, DiagnosticSeverity::Error,
                "cps", "Reading speed is " + std::to_string(input.cps) + " CPS", line});
        } else if (input.cps > profile.cps_warning_threshold) {
            result.push_back({DiagnosticKind::CpsHigh, DiagnosticSeverity::Warning,
                "cps", "Reading speed is " + std::to_string(input.cps) + " CPS", line});
        } else if (input.cps < profile.cps_low_threshold && profile.cps_low_threshold > 0) {
            result.push_back({DiagnosticKind::CpsLow, DiagnosticSeverity::Info,
                "cps_low", "Reading speed is very low (" + std::to_string(input.cps) + " CPS)", line});
        }
    }

    if (!drawing && profile.max_line_length > 0
        && input.visible_length > profile.max_line_length) {
        result.push_back({DiagnosticKind::Length, DiagnosticSeverity::Warning,
            "length", "Visible line has " + std::to_string(input.visible_length) + " characters", line});
    }

    if (profile.empty_line != SanaeQCProfile::SeverityLevel::Off
        && input.visible_text.empty()) {
        result.push_back({DiagnosticKind::Empty, profile_severity(profile.empty_line),
            "empty", "Line is empty", line});
    }

    if (profile.whitespace != SanaeQCProfile::SeverityLevel::Off && !drawing
        && !input.visible_text.empty()
        && (input.visible_text.front() == ' ' || input.visible_text.back() == ' ')) {
        result.push_back({DiagnosticKind::Whitespace, profile_severity(profile.whitespace),
            "whitespace", "Line has leading or trailing whitespace", line});
    }

    if (!drawing && input.has_double_spaces) {
        result.push_back({DiagnosticKind::Spaces, DiagnosticSeverity::Warning,
            "spaces", "Text contains repeated spaces", line});
    }

    if (input.has_unbalanced_tags) {
        result.push_back({DiagnosticKind::Tags, DiagnosticSeverity::Error,
            "tags", "Unbalanced ASS override braces", line});
    }

    if (input.has_overlap) {
        result.push_back({DiagnosticKind::Overlap, DiagnosticSeverity::Warning,
            "overlap", "Line overlaps the preceding subtitle", line});
    }

    if (!drawing && profile.max_line_breaks > 0
        && input.line_break_count > profile.max_line_breaks) {
        result.push_back({DiagnosticKind::LineBreaks, DiagnosticSeverity::Warning,
            "line_breaks", "Too many line breaks (" + std::to_string(input.line_break_count) + ")", line});
    }

    if (profile.untranslated_check != SanaeQCProfile::SeverityLevel::Off
        && !input.source_text.empty() && input.source_text == input.visible_text) {
        result.push_back({DiagnosticKind::Untranslated,
            profile_severity(profile.untranslated_check),
            "untranslated", "Translation matches source text", line});
    }

    if (profile.repeated_punctuation != SanaeQCProfile::SeverityLevel::Off && !drawing
        && (input.visible_text.find("!!") != std::string::npos
            || input.visible_text.find("??") != std::string::npos)) {
        result.push_back({DiagnosticKind::Punctuation,
            profile_severity(profile.repeated_punctuation),
            "punctuation", "Repeated punctuation", line});
    }

    if (profile.dashes == SanaeQCProfile::DashStyle::EmDash && !drawing) {
        bool has_hyphen_at_start = false;
        for (char c : input.visible_text) {
            if (c == '-') { has_hyphen_at_start = true; break; }
            if (c != ' ') break;
        }
        if (has_hyphen_at_start) {
            result.push_back({DiagnosticKind::Dash, DiagnosticSeverity::Info,
                "dash", "Hyphen used instead of em-dash", line});
        }
    }

    if (profile.ellipsis == SanaeQCProfile::EllipsisStyle::Char && !drawing
        && input.visible_text.find("...") != std::string::npos) {
        result.push_back({DiagnosticKind::Ellipsis, DiagnosticSeverity::Info,
            "ellipsis", "Three dots used instead of ellipsis character", line});
    }

    return result;
}

bool HasBlockingDiagnostic(const std::vector<SanaeDiagnostic>& diags) {
    for (auto const& d : diags) {
        if (d.severity == DiagnosticSeverity::Error) return true;
    }
    return false;
}

} // namespace sanae
