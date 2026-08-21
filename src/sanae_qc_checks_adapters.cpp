// AssDialogue/TranslationProject adapter for the pure QC rule engine.

#include "sanae_qc_checks.h"

#include "ass_dialogue.h"
#include "translation_project.h"

#include <libaegisub/character_count.h>

#include <algorithm>
#include <string>

namespace sanae {
namespace {

bool is_drawing(AssDialogue const& line) {
    auto const& text = line.Text.get();
    for (size_t pos = 0; (pos = text.find("\\p", pos)) != std::string::npos; ) {
        pos += 2;
        if (pos < text.size() && text[pos] != '0' && text[pos] != '}' && text[pos] != '\\')
            return true;
        if (pos < text.size() && text[pos] == '0')
            continue;
    }
    return false;
}

int max_visible_line_length(std::string text) {
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

    if (!line) return {};

    QCRuleInput input;
    input.line = const_cast<AssDialogue *>(line);
    input.visible_text = line->GetStrippedText();
    input.duration_ms = static_cast<int>(line->End) - static_cast<int>(line->Start);
    input.is_drawing = is_drawing(*line);
    input.visible_length = max_visible_line_length(line->Text.get());

    auto const& text = line->Text.get();
    input.has_double_spaces = text.find("  ") != std::string::npos;
    input.has_unbalanced_tags =
        std::count(text.begin(), text.end(), '{') != std::count(text.begin(), text.end(), '}');
    input.has_overlap = prev && prev->End > line->Start;

    for (size_t pos = 0; (pos = text.find("\\N", pos)) != std::string::npos; pos += 2)
        ++input.line_break_count;

    if (!input.is_drawing && input.duration_ms > 0 && tp) {
        input.cps = tp->CharactersPerSecond(
            line, agi::IGNORE_BLOCKS | agi::IGNORE_WHITESPACE);
    }

    if (tp)
        input.source_text = tp->SourceDisplayTextCached(line);

    return ComputeDiagnostics(input, profile);
}

} // namespace sanae
