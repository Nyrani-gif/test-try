// sanae_user_role.h — Client-side workflow role (NOT authorization)
// Phase 3 of SANAE_REVAMP_PLAN.md §3.8

#pragma once

#include <cstdint>

namespace sanae {

enum class SanaeUserRole : uint8_t {
    Translator,
    Reviewer
};

// WontFix UX-policy: only Reviewer can WontFix.
// This is CLIENT UX ONLY — server has NO RBAC in V0.3.
// Any device can technically PATCH state=wont_fix (server doesn't enforce role).
// The restriction lives in the client UI.

inline bool CanWontFix(SanaeUserRole role) {
    // AllowTranslatorWontFix is hardcoded false in V0.3.
    // When V0.4 adds server-sync QCProfile, this becomes configurable.
    if (role == SanaeUserRole::Reviewer) return true;
    return false;  // Translator cannot WontFix in V0.3
}

} // namespace sanae
