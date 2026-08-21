// AssDialogue adapters for LocalLineIdRegistry.

#include "sanae_local_line_id.h"

#include "ass_dialogue.h"
#include "sanae_baseline_fingerprint.h"
#include "sanae_recovery.h"
#include "sanae_text.h"

namespace sanae {

std::string compute_source_hash(AssDialogue *line) {
    if (!line) return "";
    auto visible = line->GetStrippedText();
    auto normalized = SanaeNormalizeSource(visible);
    return SanaeSha256(normalized).substr(0, 16);
}

std::string LocalLineIdRegistry::AssignForLine(AssDialogue *line) {
    if (!line) return "";
    return AssignForLine(line->Id,
                         to_centiseconds(line->Start),
                         to_centiseconds(line->End),
                         compute_source_hash(line));
}

std::string LocalLineIdRegistry::LookupByDialogue(AssDialogue *line) const {
    if (!line) return "";
    return LookupByDialogueId(line->Id);
}

} // namespace sanae
