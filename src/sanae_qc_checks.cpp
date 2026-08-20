// sanae_qc_checks.cpp — implementation
// Phase 3.4-3.5 of SANAE_REVAMP_PLAN.md

#include "sanae_qc_checks.h"

#include "ass_dialogue.h"
#include "ass_file.h"
#include "options.h"
#include "translation_project.h"

#include <libaegisub/character_count.h>

#include <algorithm>
#include <string>

namespace sanae {

namespace {

DiagnosticSeverity profile_severity(SanaeQCProfile::SeverityLevel sl) {
    switch (sl) {
        case SanaeQCProfile::SeverityLevel::Off:     return DiagnosticSeverity::Info; // shouldn't be called
        case SanaeQCProfile::SeverityLevel::Info:    return DiagnosticSeverity::Info;
        case SanaeQCProfile::SeverityLevel::Warning: return DiagnosticSeverity::Warning;
        case SanaeQCProfile::SeverityLevel::Error:   return DiagnosticSeverity::Error;
    }
    return DiagnosticSeverity::Info;
}

bool is_drawing(AssDialogue const& line) {
    auto const& text = line.Text.get();
    for (size_t pos = 0; (pos = text.find("\\p", pos)) != std::string::npos; ) {
        pos += 2;
        if (pos < text.size() && text[pos] != '0' && text[pos] != '}' && text[pos] != '\\')
            return true;
        if (pos < text.size() && text[pos] == '0') {
            // \p0 turns off drawing mode; check if there's another \pNONZERO later
            continue;
        }
    }
    return false;
}

int max_visible_line_length(std::string text) {
    // Strip override blocks
    std::string result;
    bool in_block = false;
    for (char c : text) {
        if (in_block) {
            if (c == '}') in_block = false;
        } else {
            if (c == '{') in_block = true;
            else result.push_back(c);
        }
    }
    // Find longest segment between \N
    int max_len = 0;
    size_t pos = 0;
    while (pos <= result.size()) {
        size_t next = result.find("\\N", pos);
        if (next == std::string::npos) next = result.size();
        int len = static_cast<int>(next - pos);
        if (len > max_len) max_len = len;
        pos = next + 2;
    }
    return max_len;
}

} // namespace

std::vector<SanaeDiagnostic> ComputeDiagnostics(
    const AssDialogue *line,
    const AssDialogue *prev,
    const SanaeQCProfile& profile,
    const TranslationProject *tp) {

    std::vector<SanaeDiagnostic> result;
    if (!line) return result;

    bool drawing = is_drawing(*line);

    // --- Duration ---
    int duration = static_cast<int>(line->End) - static_cast<int>(line->Start);
    if (duration <= 0) {
        result.push_back({DiagnosticKind::Duration, DiagnosticSeverity::Error,
            "duration", "End time must be after start time", const_cast<AssDialogue*>(line)});
    }

    // --- CPS high/low (skip for drawing lines) ---
    if (!drawing && duration > 0 && tp) {
        int cps = tp->CharactersPerSecond(line, agi::IGNORE_BLOCKS | agi::IGNORE_WHITESPACE);
        if (cps > 0) {
            if (cps > profile.cps_error_threshold) {
                result.push_back({DiagnosticKind::CpsHigh, DiagnosticSeverity::Error,
                    "cps", "Reading speed is " + std::to_string(cps) + " CPS",
                    const_cast<AssDialogue*>(line)});
            } else if (cps > profile.cps_warning_threshold) {
                result.push_back({DiagnosticKind::CpsHigh, DiagnosticSeverity::Warning,
                    "cps", "Reading speed is " + std::to_string(cps) + " CPS",
                    const_cast<AssDialogue*>(line)});
            } else if (cps < profile.cps_low_threshold && profile.cps_low_threshold > 0) {
                result.push_back({DiagnosticKind::CpsLow, DiagnosticSeverity::Info,
                    "cps_low", "Reading speed is very low (" + std::to_string(cps) + " CPS)",
                    const_cast<AssDialogue*>(line)});
            }
        }
    }

    // --- Line length ---
    if (!drawing) {
        int visible_length = max_visible_line_length(line->Text.get());
        if (profile.max_line_length > 0 && visible_length > profile.max_line_length) {
            result.push_back({DiagnosticKind::Length, DiagnosticSeverity::Warning,
                "length", "Visible line has " + std::to_string(visible_length) + " characters",
                const_cast<AssDialogue*>(line)});
        }
    }

    auto const& text = line->Text.get();

    // --- Empty line ---
    if (profile.empty_line != SanaeQCProfile::SeverityLevel::Off) {
        auto stripped = line->GetStrippedText();
        if (stripped.empty()) {
            result.push_back({DiagnosticKind::Empty, profile_severity(profile.empty_line),
                "empty", "Line is empty", const_cast<AssDialogue*>(line)});
        }
    }

    // --- Whitespace (trailing/leading) ---
    if (profile.whitespace != SanaeQCProfile::SeverityLevel::Off && !drawing) {
        auto stripped = line->GetStrippedText();
        if (!stripped.empty() && (stripped.front() == ' ' || stripped.back() == ' ')) {
            result.push_back({DiagnosticKind::Whitespace, profile_severity(profile.whitespace),
                "whitespace", "Line has leading or trailing whitespace",
                const_cast<AssDialogue*>(line)});
        }
    }

    // --- Double spaces ---
    if (!drawing && text.find("  ") != std::string::npos) {
        result.push_back({DiagnosticKind::Spaces, DiagnosticSeverity::Warning,
            "spaces", "Text contains repeated spaces", const_cast<AssDialogue*>(line)});
    }

    // --- Tags (unbalanced braces) ---
    if (std::count(text.begin(), text.end(), '{') != std::count(text.begin(), text.end(), '}')) {
        result.push_back({DiagnosticKind::Tags, DiagnosticSeverity::Error,
            "tags", "Unbalanced ASS override braces", const_cast<AssDialogue*>(line)});
    }

    // --- Style does not exist ---
    // (requires AssFile context — skip if not available; CheckLine already does this)

    // --- Overlap ---
    if (prev && prev->End > line->Start) {
        result.push_back({DiagnosticKind::Overlap, DiagnosticSeverity::Warning,
            "overlap", "Line overlaps the preceding subtitle", const_cast<AssDialogue*>(line)});
    }

    // --- Line breaks ---
    if (!drawing && profile.max_line_breaks > 0) {
        int breaks = 0;
        for (size_t pos = 0; (pos = text.find("\\N", pos)) != std::string::npos; pos += 2)
            ++breaks;
        if (breaks > profile.max_line_breaks) {
            result.push_back({DiagnosticKind::LineBreaks, DiagnosticSeverity::Warning,
                "line_breaks", "Too many line breaks (" + std::to_string(breaks) + ")",
                const_cast<AssDialogue*>(line)});
        }
    }

    // --- Untranslated (RU == EN) ---
    if (profile.untranslated_check != SanaeQCProfile::SeverityLevel::Off && tp) {
        auto source = tp->SourceDisplayTextCached(line);
        auto target = line->GetStrippedText();
        if (!source.empty() && source == target) {
            result.push_back({DiagnosticKind::Untranslated, profile_severity(profile.untranslated_check),
                "untranslated", "Translation matches source text",
                const_cast<AssDialogue*>(line)});
        }
    }

    // --- Repeated punctuation ---
    if (profile.repeated_punctuation != SanaeQCProfile::SeverityLevel::Off && !drawing) {
        auto visible = line->GetStrippedText();
        if (visible.find("!!") != std::string::npos || visible.find("??") != std::string::npos) {
            result.push_back({DiagnosticKind::Punctuation, profile_severity(profile.repeated_punctuation),
                "punctuation", "Repeated punctuation", const_cast<AssDialogue*>(line)});
        }
    }

    // --- Dash check (em-dash vs hyphen) ---
    if (profile.dashes != SanaeQCProfile::DashStyle::Off && !drawing) {
        auto visible = line->GetStrippedText();
        bool has_hyphen_at_start = false;
        for (char c : visible) {
            if (c == '-') { has_hyphen_at_start = true; break; }
            if (c != ' ') break;
        }
        if (has_hyphen_at_start && profile.dashes == SanaeQCProfile::DashStyle::EmDash) {
            result.push_back({DiagnosticKind::Dash, DiagnosticSeverity::Info,
                "dash", "Hyphen used instead of em-dash", const_cast<AssDialogue*>(line)});
        }
    }

    // --- Ellipsis check ---
    if (profile.ellipsis != SanaeQCProfile::EllipsisStyle::Off && !drawing) {
        auto visible = line->GetStrippedText();
        if (profile.ellipsis == SanaeQCProfile::EllipsisStyle::Char) {
            if (visible.find("...") != std::string::npos) {
                result.push_back({DiagnosticKind::Ellipsis, DiagnosticSeverity::Info,
                    "ellipsis", "Three dots used instead of ellipsis character",
                    const_cast<AssDialogue*>(line)});
            }
        }
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
