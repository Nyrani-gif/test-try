// command/episode_workflow.cpp — Submit for QC command
// Phase 4.10 of SANAE_REVAMP_PLAN.md
//
// Ctrl+Enter = Submit for QC (NOT Finalize).
// In local/degraded mode: performs local workflow state transition.
// Phase 6 will connect to server review-transition endpoint.
//
// BLOCKING SEMANTICS (authoritative):
#include "../ass_dialogue.h"
#include "../ass_file.h"
#include "../compat.h"
//   Info Diagnostic: never blocks Submit.
//   Warning Diagnostic: never blocks Submit.
//   Error Diagnostic: blocks Submit when configured as blocking by QCProfile.
//   Open/ReadyForReview ReviewIssue: does NOT block Submit (Submit is the
//     translator asking for review, not the QC accepting the episode).
//   If blocking Error Diagnostics exist: Submit is REJECTED (not warned).
//   Rejected Submit does NOT mutate workflow state.
//   Submit NEVER calls Finalize.

#include "command.h"

#include "../include/aegisub/context.h"
#include "../options.h"
using cmd::Command;
#include "../sanae_project.h"
#include "../sanae_ux_metrics.h"
#include "../sanae_qc_checks.h"
#include "../sanae_qc_profile.h"
#include "../sanae_local_project_config.h"
#include "../translation_project.h"

#include <wx/msgdlg.h>
#include <wx/string.h>

namespace {
struct sanae_episode_submit_for_qc final : public Command {
    CMD_NAME("sanae/episode/submit_for_qc")
    STR_MENU("Submit for &QC")
    STR_DISP("Submit for QC")
    STR_HELP("Mark this episode as ready for QC review (not Finalize)")

    void operator()(agi::Context *c) override {
        if (!c->sanaeProject || !c->sanaeProject->HasOpenEpisode()) {
            wxMessageBox(_("No Sanae episode is open."), _("Submit for QC"),
                         wxOK | wxICON_INFORMATION, c->parent);
            return;
        }

        sanae::ux::modal_opened("submit_for_qc");

        // Check for blocking Error Diagnostics using QCProfile.
        // Info/Warning never block. Error blocks per QCProfile configuration.
        // ReviewIssues (open/ready_for_review) do NOT block Submit —
        // Submit is the translator asking for review, not QC accepting.
        if (c->translationProject && c->ass) {
            // Load QCProfile from project-scoped local config.
            sanae::SanaeLocalProjectConfig config;
            auto project_id = c->sanaeProject->ActiveProjectId();
            if (!project_id.empty())
                config.Load(project_id);
            auto const& profile = config.Profile();

            int blocking_count = 0;
            wxString blocking_details;

            // Check each line for blocking Error Diagnostics.
            AssDialogue *prev = nullptr;
            for (auto& line : c->ass->Events) {
                auto diags = sanae::ComputeDiagnostics(&line, prev, profile, c->translationProject);
                if (sanae::HasBlockingDiagnostic(diags)) {
                    ++blocking_count;
                    if (blocking_count <= 5) {
                        if (!blocking_details.empty()) blocking_details += "\n";
                        blocking_details += wxString::Format(_("Line %d: %s"),
                            line.Row + 1, diags[0].message);
                    }
                }
                prev = &line;
            }

            if (blocking_count > 0) {
                // BLOCKING: reject Submit. Do not mutate workflow state.
                // Do not call Finalize. Do not continue.
                wxMessageBox(
                    wxString::Format(_("%d blocking error(s) prevent submission:\n\n%s\n\nFix these errors before submitting for QC."),
                        blocking_count, blocking_details),
                    _("Submit for QC — Blocked"),
                    wxOK | wxICON_ERROR,
                    c->parent);
                return;  // REJECTED — no workflow state mutation.
            }
        }

        // No blocking errors: perform local workflow transition.
        // In degraded mode (no server v0.3 or ServerReviewSync off),
        // this is a local flag. Phase 6 will call POST /episodes/{id}/review-transition.
        sanae::ux::qc_issue_interaction("submit_for_qc");
        wxMessageBox(_("Episode submitted for QC review."),
                     _("Submit for QC"), wxOK | wxICON_INFORMATION, c->parent);

        // NOTE: Submit for QC does NOT call Finalize.
        // Finalize is a separate operation (server compact RUSUB upload).
        // Finalize does NOT require review_state=done (warning only).
    }
};
} // namespace

namespace cmd {
void register_sanae_episode_workflow() {
        reg(std::make_unique<sanae_episode_submit_for_qc>());
}
}
