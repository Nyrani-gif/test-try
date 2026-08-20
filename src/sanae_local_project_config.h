// sanae_local_project_config.h — Project-scoped local storage
// Phase 3 of SANAE_REVAMP_PLAN.md §5.8
//
// Stores QCProfile and aliases (V1.5) at project scope, NOT per-file.
// Location: ?user/sanae/local-config/<project-uuid>.json
// NOT server-synced in V0.3.

#pragma once

#include "sanae_qc_profile.h"

#include <memory>
#include <string>

namespace sanae {

class SanaeLocalProjectConfig {
public:
    SanaeLocalProjectConfig();

    // Load from ?user/sanae/local-config/<project_id>.json
    void Load(const std::string& project_id);

    // Save to ?user/sanae/local-config/<project_id>.json
    void Save(const std::string& project_id) const;

    SanaeQCProfile& Profile() { return profile_; }
    const SanaeQCProfile& Profile() const { return profile_; }

private:
    SanaeQCProfile profile_;
};

} // namespace sanae
