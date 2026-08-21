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
    if (!c || !c->frame) return false;
    auto *frame = dynamic_cast<FrameMain *>(c->frame);
    if (!frame || frame->GetWorkspaceMode() != WorkspaceMode::QC) return false;
    auto *focus = wxWindow::FindFocus();
    if (!focus) return false;
    if (dynamic_cast<SubsTextEditCtrl *>(focus)) return false;
    for (wxWindow *p = focus; p; p = p->GetParent()) {
        if (dynamic_cast<SubsEditBox *>(p)) return false;
    }
    return true;
}
bool check_qc_hotkey(agi::Context *c, wxKeyEvent &event) {
    if (!qc_context_active(c)) return false;
    return hotkey::check("Sanae QC", c, event);
}
}
