// command/sanae_workspace.cpp — Workspace mode + QC command stubs
// Phase 4 of SANAE_REVAMP_PLAN.md
//
// All commands are registered through the normal Aegisub CommandManager.
// Default hotkeys are in default_hotkey.json. Users can rebind/remove them
// through the standard Preferences → Hotkeys UI.
//
// Focus protection: QC single-key commands (Q/C/Enter/Backspace) are in
// the "Sanae QC" context only. When SubsEditBox has focus, the active
// context is "Subtitle Edit Box" — QC keys do NOT intercept typing.

#include "command.h"

#include "../frame_main.h"
#include "../include/aegisub/context.h"
#include "../options.h"
#include "../sanae_ux_metrics.h"
#include "../workspace_mode.h"

#include <libaegisub/log.h>

namespace {
using cmd::Command;
using agi::Context;

// === Workspace mode commands ===

struct sanae_workspace_translation final : public Command {
    CMD_NAME("sanae/workspace/translation")
    STR_MENU("Translation Mode")
    STR_DISP("Translation Mode")
    STR_HELP("Switch to translation workspace (calm, context-focused layout)")
    void operator()(agi::Context *c) override {
        if (auto *frame = dynamic_cast<FrameMain *>(c->frame))
            frame->SetWorkspaceMode(sanae::WorkspaceMode::Translation);
    }
};

struct sanae_workspace_qc final : public Command {
    CMD_NAME("sanae/workspace/qc")
    STR_MENU("QC Mode")
    STR_DISP("QC Mode")
    STR_HELP("Switch to QC workspace (issues, navigation, review controls)")
    void operator()(agi::Context *c) override {
        if (auto *frame = dynamic_cast<FrameMain *>(c->frame))
            frame->SetWorkspaceMode(sanae::WorkspaceMode::QC);
    }
};

struct sanae_workspace_advanced final : public Command {
    CMD_NAME("sanae/workspace/advanced")
    STR_MENU("Advanced Mode")
    STR_DISP("Advanced Mode")
    STR_HELP("Restore full legacy Aegisub layout and capabilities")
    void operator()(agi::Context *c) override {
        if (auto *frame = dynamic_cast<FrameMain *>(c->frame))
            frame->SetWorkspaceMode(sanae::WorkspaceMode::Advanced);
    }
};

struct sanae_workspace_focus final : public Command {
    CMD_NAME("sanae/workspace/focus")
    STR_MENU("Focus Mode")
    STR_DISP("Focus Mode")
    STR_HELP("Hide visual noise for distraction-free translation")
    void operator()(agi::Context *c) override {
        if (auto *frame = dynamic_cast<FrameMain *>(c->frame))
            frame->ToggleFocusMode();
    }
};

// === QC navigation commands ===

struct sanae_qc_next_problem final : public Command {
    CMD_NAME("sanae/qc/next_problem")
    STR_MENU("Next Problem")
    STR_DISP("Next Problem")
    STR_HELP("Navigate to the next actionable issue")
    void operator()(agi::Context *c) override {
        sanae::ux::qc_issue_interaction("next_problem");
        // QCIssueDock::NavigateNext(false) will be called when dock is wired.
        LOG_D("sanae/qc") << "next problem";
    }
};

struct sanae_qc_prev_problem final : public Command {
    CMD_NAME("sanae/qc/prev_problem")
    STR_MENU("Previous Problem")
    STR_DISP("Previous Problem")
    STR_HELP("Navigate to the previous actionable issue")
    void operator()(agi::Context *c) override {
        sanae::ux::qc_issue_interaction("prev_problem");
        LOG_D("sanae/qc") << "prev problem";
    }
};

struct sanae_qc_next_critical final : public Command {
    CMD_NAME("sanae/qc/next_critical")
    STR_MENU("Next Critical")
    STR_DISP("Next Critical")
    STR_HELP("Navigate to the next critical (Error) issue")
    void operator()(agi::Context *c) override {
        sanae::ux::qc_issue_interaction("next_critical");
        LOG_D("sanae/qc") << "next critical";
    }
};

// === QC action commands (Sanae QC context only) ===

struct sanae_qc_create_issue final : public Command {
    CMD_NAME("sanae/qc/create_issue")
    STR_MENU("Create Issue")
    STR_DISP("Create Issue")
    STR_HELP("Create a QC issue for the current line")
    void operator()(agi::Context *c) override {
        sanae::ux::qc_issue_interaction("create_issue");
        // QuickIssuePopover will be opened when frame_main is wired.
        LOG_D("sanae/qc") << "create issue";
    }
};

struct sanae_qc_comment final : public Command {
    CMD_NAME("sanae/qc/comment")
    STR_MENU("Comment")
    STR_DISP("Comment")
    STR_HELP("Add a comment to the current issue")
    void operator()(agi::Context *c) override {
        sanae::ux::qc_issue_interaction("comment");
        LOG_D("sanae/qc") << "comment";
    }
};

struct sanae_qc_accept final : public Command {
    CMD_NAME("sanae/qc/accept")
    STR_MENU("Accept")
    STR_DISP("Accept")
    STR_HELP("Accept (resolve) the current issue")
    void operator()(agi::Context *c) override {
        sanae::ux::qc_issue_interaction("accept");
        LOG_D("sanae/qc") << "accept";
    }
};

struct sanae_qc_return final : public Command {
    CMD_NAME("sanae/qc/return")
    STR_MENU("Return")
    STR_DISP("Return")
    STR_HELP("Return (reopen) the current issue")
    void operator()(agi::Context *c) override {
        sanae::ux::qc_issue_interaction("return");
        LOG_D("sanae/qc") << "return";
    }
};

// === Terminology commands ===

struct sanae_terminology_add final : public Command {
    CMD_NAME("sanae/terminology/add")
    STR_MENU("Add to Terminology")
    STR_DISP("Add to Terminology")
    STR_HELP("Open the terminology entry popover for the current line")
    void operator()(agi::Context *c) override {
        sanae::ux::terminology_manual_search();
        LOG_D("sanae/terminology") << "add term";
    }
};

struct sanae_terminology_ignore final : public Command {
    CMD_NAME("sanae/terminology/ignore")
    STR_MENU("Ignore Term Match")
    STR_DISP("Ignore Term Match")
    STR_HELP("Ignore the current terminology suggestion for this episode/project")
    void operator()(agi::Context *c) override {
        LOG_D("sanae/terminology") << "ignore term";
    }
};

struct sanae_terminology_apply_1 final : public Command {
    CMD_NAME("sanae/terminology/apply_1")
    STR_MENU("Apply Term 1")
    STR_DISP("Apply Term 1")
    STR_HELP("Apply the first terminology suggestion to the edit box")
    void operator()(agi::Context *c) override {
        sanae::ux::terminology_inline_applied();
        LOG_D("sanae/terminology") << "apply term 1";
    }
};

struct sanae_terminology_apply_2 final : public Command {
    CMD_NAME("sanae/terminology/apply_2")
    STR_MENU("Apply Term 2")
    STR_DISP("Apply Term 2")
    STR_HELP("Apply the second terminology suggestion to the edit box")
    void operator()(agi::Context *c) override {
        sanae::ux::terminology_inline_applied();
    }
};

struct sanae_terminology_apply_3 final : public Command {
    CMD_NAME("sanae/terminology/apply_3")
    STR_MENU("Apply Term 3")
    STR_DISP("Apply Term 3")
    STR_HELP("Apply the third terminology suggestion to the edit box")
    void operator()(agi::Context *c) override {
        sanae::ux::terminology_inline_applied();
    }
};

struct sanae_terminology_apply_4 final : public Command {
    CMD_NAME("sanae/terminology/apply_4")
    STR_MENU("Apply Term 4")
    STR_DISP("Apply Term 4")
    STR_HELP("Apply the fourth terminology suggestion to the edit box")
    void operator()(agi::Context *c) override {
        sanae::ux::terminology_inline_applied();
    }
};

struct sanae_terminology_apply_5 final : public Command {
    CMD_NAME("sanae/terminology/apply_5")
    STR_MENU("Apply Term 5")
    STR_DISP("Apply Term 5")
    STR_HELP("Apply the fifth terminology suggestion to the edit box")
    void operator()(agi::Context *c) override {
        sanae::ux::terminology_inline_applied();
    }
};

} // namespace

// Called from cmd::init_sanae() in sanae.cpp.
// All commands registered through standard CommandManager → user-reconfigurable.
namespace cmd {
void register_sanae_workspace() {
        reg(std::make_unique<sanae_workspace_translation>());
        reg(std::make_unique<sanae_workspace_qc>());
        reg(std::make_unique<sanae_workspace_advanced>());
        reg(std::make_unique<sanae_workspace_focus>());
        reg(std::make_unique<sanae_qc_next_problem>());
        reg(std::make_unique<sanae_qc_prev_problem>());
        reg(std::make_unique<sanae_qc_next_critical>());
        reg(std::make_unique<sanae_qc_create_issue>());
        reg(std::make_unique<sanae_qc_comment>());
        reg(std::make_unique<sanae_qc_accept>());
        reg(std::make_unique<sanae_qc_return>());
        reg(std::make_unique<sanae_terminology_add>());
        reg(std::make_unique<sanae_terminology_ignore>());
        reg(std::make_unique<sanae_terminology_apply_1>());
        reg(std::make_unique<sanae_terminology_apply_2>());
        reg(std::make_unique<sanae_terminology_apply_3>());
        reg(std::make_unique<sanae_terminology_apply_4>());
        reg(std::make_unique<sanae_terminology_apply_5>());
}
}
