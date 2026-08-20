// sanae_comment.h — Immutable comment on a ReviewIssue
// Authoritative: SANAE_SERVER_REQUIREMENTS_v0.3.md §2.2, §7

#pragma once

#include <string>

namespace sanae {

struct SanaeComment {
    std::string id;
    std::string issue_id;
    std::string body;
    std::string created_by_device_id;
    std::string created_at;
    // Immutable in V0.3. NO edited_at. NO deleted_at.
};

} // namespace sanae
