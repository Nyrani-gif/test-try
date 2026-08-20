// workspace_mode.h — Minimal WorkspaceMode enum (Phase 3 minimal, Phase 4 full)
// Phase 3 of SANAE_REVAMP_PLAN.md §3.1

#pragma once

#include <cstdint>

namespace sanae {

enum class WorkspaceMode : uint8_t {
    Translation,
    QC,
    Advanced
};

// Phase 3 minimal: only Translation and QC are used.
// Phase 4 adds Advanced (full Aegisub layout).
// WorkspaceMode is NOT authorization — it controls panel visibility only.
// SanaeUserRole (Translator/Reviewer) is separate and controls action availability.

} // namespace sanae
