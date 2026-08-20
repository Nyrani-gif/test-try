// sanae_hotkey_context.cpp — implementation
// Phase 4 — Sanae QC context dispatcher

#include "sanae_hotkey_context.h"

#include "frame_main.h"
#include "include/aegisub/context.h"
#include "include/aegisub/hotkey.h"
#include "options.h"
#include "subs_edit_box.h"
#include "subs_edit_ctrl.h"
#include "workspace_mode.h"

#include <wx/event.h>
#include <wx/window.h>

namespace sanae {

bool qc_context_active(agi::Context *c) {
    if (!c) return false;
    if (!c->frame) return false;

    // Single source of truth: FrameMain::GetWorkspaceMode().
    // No magic integer comparison — runtime logic derives from the same
    // WorkspaceMode enum that SetWorkspaceMode() controls.
    auto *frame = dynamic_cast<FrameMain *>(c->frame);
    if (!frame) return false;
    if (frame->GetWorkspaceMode() != WorkspaceMode::QC) return false;

    // Check that focus is NOT in a SubsTextEditCtrl.
    // SubsEditBox::OnKeyDown dispatches "Subtitle Edit Box" context directly.
    // If SubsEditBox or its edit_ctrl has focus, we must not dispatch "Sanae QC"
    // because that would allow single-key QC commands to intercept typing.
    auto *focus = wxWindow::FindFocus();
    if (!focus) return false;

    // Check if the focused window is a SubsTextEditCtrl by comparing class names.
    // SubsTextEditCtrl inherits from wxStyledTextCtrl.
    wxString class_name = focus->GetClassInfo()->GetClassName();
    if (class_name == "wxStyledTextCtrl") return false;

    // Also check if focus is inside a SubsEditBox by walking up parents.
    wxWindow *parent = focus;
    while (parent) {
        if (parent->GetClassInfo()->GetClassName() == "SubsEditBox")
            return false;
        parent = parent->GetParent();
    }

    return true;
}

bool check_qc_hotkey(agi::Context *c, wxKeyEvent &event) {
    if (!qc_context_active(c)) return false;
    return hotkey::check("Sanae QC", c, event);
}

} // namespace sanae
