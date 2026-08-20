// Copyright (c) 2005, Rodrigo Braz Monteiro, Niels Martin Hansen
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
//   * Redistributions of source code must retain the above copyright notice,
//     this list of conditions and the following disclaimer.
//   * Redistributions in binary form must reproduce the above copyright notice,
//     this list of conditions and the following disclaimer in the documentation
//     and/or other materials provided with the distribution.
//   * Neither the name of the Aegisub Group nor the names of its contributors
//     may be used to endorse or promote products derived from this software
//     without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//
// Aegisub Project http://www.aegisub.org/

/// @file frame_main.cpp
/// @brief Main window creation and control management
/// @ingroup main_ui

#include "frame_main.h"

#include "include/aegisub/context.h"
#include "include/aegisub/menu.h"
#include "include/aegisub/toolbar.h"
#include "include/aegisub/hotkey.h"

#include "ass_file.h"
#include "async_video_provider.h"
#include "audio_controller.h"
#include "audio_box.h"
#include "base_grid.h"
#include "compat.h"
#include "command/command.h"
#include "dialog_detached_video.h"
#include "dialog_manager.h"
#include "libresrc/libresrc.h"
#include "main.h"
#include "options.h"
#include "project.h"
#include "sanae_project.h"
#include "sanae_hotkey_context.h"
#include "qc_issue_dock.h"
#include "subs_controller.h"
#include "subs_edit_box.h"
#include "utils.h"
#include "version.h"
#include "video_box.h"
#include "video_controller.h"
#include "video_display.h"

#include <libaegisub/dispatch.h>
#include <libaegisub/log.h>

#include <wx/dnd.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/splitter.h>
#include <wx/statline.h>
#include <wx/sysopt.h>
#include <wx/toolbar.h>

enum {
        ID_APP_TIMER_STATUSCLEAR = 12002
};

#ifdef WITH_STARTUPLOG
#define StartupLog(a) MessageBox(0, a, "Aegisub startup log", 0)
#else
#define StartupLog(a) LOG_I("frame_main/init") << a
#endif

/// Handle files drag and dropped onto Aegisub
class AegisubFileDropTarget final : public wxFileDropTarget {
        agi::Context *context;
public:
        AegisubFileDropTarget(agi::Context *context) : context(context) { }
        bool OnDropFiles(wxCoord, wxCoord, wxArrayString const& filenames) override {
                std::vector<agi::fs::path> files;
                for (wxString const& fn : filenames)
                        files.push_back(from_wx(fn));
                agi::dispatch::Main().Async([=, this] { context->project->LoadList(files); });
                return true;
        }
};

FrameMain::FrameMain()
: wxFrame(nullptr, -1, "")
, context(std::make_unique<agi::Context>())
{
        StartupLog("Entering FrameMain constructor");

        SetSize(FromDIP(wxSize(920, 700)));

#ifdef __WXGTK__
        // XXX HACK XXX
        // We need to set LC_ALL to "" here for input methods to work reliably.
        setlocale(LC_ALL, "");

        // However LC_NUMERIC must be "C", otherwise some parsing fails.
        setlocale(LC_NUMERIC, "C");
#endif

        StartupLog("Initializing context controls");
        BindConnection(context->ass->AddCommitListener(&FrameMain::UpdateTitle, this));
        BindConnection(context->subsController->AddFileOpenListener(&FrameMain::OnSubtitlesOpen, this));
        BindConnection(context->subsController->AddFileSaveListener(&FrameMain::UpdateTitle, this));
        BindConnection(context->project->AddAudioProviderListener(&FrameMain::OnAudioOpen, this));
        BindConnection(context->project->AddVideoProviderListener(&FrameMain::OnVideoOpen, this));

        StartupLog("Initializing context frames");
        context->parent = this;
        context->frame = this;

        StartupLog("Apply saved Maximized state");
        if (OPT_GET("App/Maximized")->GetBool()) Maximize(true);

        StartupLog("Initialize toolbar");
        wxSystemOptions::SetOption("msw.remap", 0);
        BindConnection(OPT_SUB("App/Show Toolbar", &FrameMain::EnableToolBar, this));
        EnableToolBar(*OPT_GET("App/Show Toolbar"));

        StartupLog("Initialize menu bar");
        menu::GetMenuBar("main", this, (wxID_HIGHEST + 1) + 10000, context.get());

        StartupLog("Create status bar");
        CreateStatusBar(2);

        StartupLog("Set icon");
#ifdef _WIN32
        SetIcon(wxICON(wxicon));
#else
        wxIcon icon;
        icon.CopyFromBitmap(GETIMAGE(wxicon));
        SetIcon(icon);
#endif

        StartupLog("Create views and inner main window controls");
        InitContents();
        BindConnection(OPT_SUB("Video/Detached/Enabled", &FrameMain::OnVideoDetach, this));

        StartupLog("Set up drag/drop target");
        SetDropTarget(new AegisubFileDropTarget(context.get()));

        StartupLog("Load default file");
        context->project->CloseSubtitles();

        StartupLog("Display main window");
        AddFullScreenButton(this);
        Show();
        SetDisplayMode(1, 1);

        // Phase 4: Restore persisted workspace mode.
        // Config stores integer; runtime uses WorkspaceMode enum.
        // Invalid values default to Translation (safe).
        // Only restore if WorkspaceModes feature flag is enabled.
        if (OPT_GET("Sanae/WorkspaceModes")->GetBool()) {
                int saved_mode = OPT_GET("Sanae/Workspace/CurrentMode")->GetInt();
                if (saved_mode >= 0 && saved_mode <= 2)
                        workspace_mode = static_cast<sanae::WorkspaceMode>(saved_mode);
                else
                        workspace_mode = sanae::WorkspaceMode::Translation;
        } else {
                // Feature flag OFF: stay in Translation (legacy-compatible).
                workspace_mode = sanae::WorkspaceMode::Translation;
        }

        StartupLog("Leaving FrameMain constructor");
}

FrameMain::~FrameMain () {
        context->project->CloseAudio();
        context->project->CloseVideo();

        DestroyChildren();
}

void FrameMain::EnableToolBar(agi::OptionValue const& opt) {
        if (opt.GetBool()) {
                if (!GetToolBar()) {
                        toolbar::AttachToolbar(this, "main", context.get(), "Default");
                        GetToolBar()->Realize();
                }
        }
        else if (wxToolBar *old_tb = GetToolBar()) {
                SetToolBar(nullptr);
                delete old_tb;
                Layout();
        }
}

void FrameMain::InitContents() {
        StartupLog("Create background panel");
        auto Panel = new wxPanel(this, -1, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL | wxCLIP_CHILDREN);

        StartupLog("Create subtitles grid");
        context->subsGrid = new BaseGrid(Panel, context.get());

        StartupLog("Create video box");
        videoBox = new VideoBox(Panel, false, context.get());

        StartupLog("Create audio box");
        context->audioBox = audioBox = new AudioBox(Panel, context.get());

        StartupLog("Create subtitle editing box");
        auto EditBox = new SubsEditBox(Panel, context.get());

        StartupLog("Create QCIssueDock");
        qc_dock = new QCIssueDock(Panel, context.get());
        qc_dock->Hide();

        StartupLog("Arrange main sizers");
        ToolsSizer = new wxBoxSizer(wxVERTICAL);
        ToolsSizer->Add(audioBox, 0, wxEXPAND);
        ToolsSizer->Add(EditBox, 1, wxEXPAND);

        // Phase 4: wxSplitterWindow between video and tools.
        // Replaces the fixed wxBoxSizer for TopSizer.
        // Sash position persisted via Sanae/Workspace/SplitterPosition.
        TopSplitter = new wxSplitterWindow(Panel, -1, wxDefaultPosition, wxDefaultSize,
                wxSP_3D | wxSP_LIVE_UPDATE);
        TopSplitter->SetMinimumPaneSize(100);
        videoBox->Reparent(TopSplitter);
        // Wrap ToolsSizer in a panel for the splitter.
        auto tools_panel = new wxPanel(TopSplitter, -1, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL | wxCLIP_CHILDREN);
        tools_panel->SetSizer(ToolsSizer);
        ToolsSizer->SetContainingWindow(tools_panel);

        // Restore sash position with clamping.
        int saved_pos = OPT_GET("Sanae/Workspace/SplitterPosition")->GetInt();
        // Split vertically (video left, tools right).
        TopSplitter->SplitVertically(videoBox, tools_panel, saved_pos);

        // Bind sash position change to persist.
        TopSplitter->Bind(wxEVT_SPLITTER_SASH_POS_CHANGED, [this](wxSplitterEvent&) {
                int pos = TopSplitter->GetSashPosition();
                // Clamp to reasonable bounds.
                int min_pos = TopSplitter->GetMinimumPaneSize();
                int max_pos = TopSplitter->GetSize().GetWidth() - min_pos;
                if (pos < min_pos) pos = min_pos;
                if (pos > max_pos) pos = max_pos;
                OPT_SET("Sanae/Workspace/SplitterPosition")->SetInt(pos);
        });

        MainSizer = new wxBoxSizer(wxVERTICAL);
        MainSizer->Add(new wxStaticLine(Panel),0,wxEXPAND,0);
        MainSizer->Add(TopSplitter,0,wxEXPAND,0);
        // QCIssueDock is shown below the grid in QC mode.
        MainSizer->Add(qc_dock, 0, wxEXPAND | wxALL, 0);
        qc_dock->Hide();
        MainSizer->Add(context->subsGrid,1,wxEXPAND,0);
        Panel->SetSizer(MainSizer);

        StartupLog("Perform layout");
        Layout();
        StartupLog("Leaving InitContents");
}

void FrameMain::SetDisplayMode(int video, int audio) {
        if (!IsShownOnScreen()) return;

        bool sv = false, sa = false;

        if (video == -1) sv = showVideo;
        else if (video)  sv = context->project->VideoProvider() && !context->dialog->Get<DialogDetachedVideo>();

        if (audio == -1) sa = showAudio;
        else if (audio)  sa = !!context->project->AudioProvider();

        // See if anything changed
        if (sv == showVideo && sa == showAudio) return;

        showVideo = sv;
        showAudio = sa;

        bool didFreeze = !IsFrozen();
        if (didFreeze) Freeze();

        context->videoController->Stop();

        // Phase 4: TopSplitter replaces TopSizer for video/tools split.
        // Show/hide video pane via splitter.
        if (TopSplitter) {
                if (showVideo && !TopSplitter->IsSplit())
                        TopSplitter->SplitVertically(videoBox, TopSplitter->GetWindow2(), OPT_GET("Sanae/Workspace/SplitterPosition")->GetInt());
                else if (!showVideo && TopSplitter->IsSplit())
                        TopSplitter->Unsplit(videoBox);
        }
        ToolsSizer->Show(audioBox, showAudio, true);

        MainSizer->Layout();
        Layout();

        if (didFreeze) Thaw();
}

void FrameMain::UpdateTitle() {
        wxString newTitle;
        if (context->subsController->IsModified()) newTitle << "* ";
        newTitle << context->subsController->Filename().filename().wstring();

#ifndef __WXMAC__
        newTitle << " - Aegisub " << GetAegisubLongVersionString();
#endif

#if defined(__WXMAC__)
        // On Mac, set the mark in the close button
        OSXSetModified(context->subsController->IsModified());
#endif

        if (GetTitle() != newTitle) SetTitle(newTitle);
}

void FrameMain::OnVideoOpen(AsyncVideoProvider *provider) {
        if (!provider) {
                SetDisplayMode(0, -1);
                return;
        }

        Freeze();
        int vidx = provider->GetWidth(), vidy = provider->GetHeight();

        // Set zoom level based on video resolution and window size
        double zoom = context->videoDisplay->GetWindowZoom();
        wxSize windowSize = GetSize();
        if (vidx*3*zoom > windowSize.GetX()*4 || vidy*4*zoom > windowSize.GetY()*6)
                context->videoDisplay->SetWindowZoom(zoom * .25);
        else if (vidx*3*zoom > windowSize.GetX()*2 || vidy*4*zoom > windowSize.GetY()*3)
                context->videoDisplay->SetWindowZoom(zoom * .5);

        SetDisplayMode(1,-1);

        if (OPT_GET("Video/Detached/Enabled")->GetBool() && !context->dialog->Get<DialogDetachedVideo>())
                cmd::call("video/detach", context.get());
        Thaw();
}

void FrameMain::OnVideoDetach(agi::OptionValue const& opt) {
        if (opt.GetBool())
                SetDisplayMode(0, -1);
        else if (context->project->VideoProvider())
                SetDisplayMode(1, -1);
}

void FrameMain::StatusTimeout(wxString text,int ms) {
        SetStatusText(text,1);
        StatusClear.SetOwner(this, ID_APP_TIMER_STATUSCLEAR);
        StatusClear.Start(ms,true);
}

BEGIN_EVENT_TABLE(FrameMain, wxFrame)
        EVT_TIMER(ID_APP_TIMER_STATUSCLEAR, FrameMain::OnStatusClear)
        EVT_CLOSE(FrameMain::OnCloseWindow)
        EVT_CHAR_HOOK(FrameMain::OnKeyDown)
        EVT_MOUSEWHEEL(FrameMain::OnMouseWheel)
END_EVENT_TABLE()

void FrameMain::OnCloseWindow(wxCloseEvent &event) {
        wxEventBlocker blocker(this, wxEVT_CLOSE_WINDOW);

        context->videoController->Stop();
        context->audioController->Stop();

        // Ask user if he wants to save first
        if (context->subsController->TryToClose(event.CanVeto()) == wxCANCEL) {
                event.Veto();
                return;
        }

        // Snapshot immutable recovery data now, but never wait for network I/O
        // while the Aegisub window is closing.
        context->sanaeProject->PrepareRecoveryOnShutdown();

        context->dialog.reset();

        // Store maximization state
        OPT_SET("App/Maximized")->SetBool(IsMaximized());

        Destroy();
}

void FrameMain::OnStatusClear(wxTimerEvent &) {
        SetStatusText("",1);
}

void FrameMain::OnAudioOpen(agi::AudioProvider *provider) {
        if (provider)
                SetDisplayMode(-1, 1);
        else
                SetDisplayMode(-1, 0);
}

void FrameMain::OnSubtitlesOpen() {
        UpdateTitle();
        SetDisplayMode(1, 1);
}

void FrameMain::OnKeyDown(wxKeyEvent &event) {
        // Phase 4: Sanae QC context dispatch.
        // Uses GetWorkspaceMode() (single source of truth), not magic int.
        if (sanae::check_qc_hotkey(context.get(), event))
                return;
        hotkey::check("Main Frame", context.get(), event);
}

void FrameMain::SetWorkspaceMode(sanae::WorkspaceMode mode) {
        // Feature flag: if WorkspaceModes is OFF, do not change layout.
        // Legacy Aegisub layout remains. Commands still registered but no-ops.
        if (!OPT_GET("Sanae/WorkspaceModes")->GetBool()) return;

        if (workspace_mode == mode && !focus_mode_active) return;

        // Save current mode for Focus Mode restoration.
        if (focus_mode_active) {
                // Exiting focus mode — restore to the requested mode.
                focus_mode_active = false;
        }

        workspace_mode = mode;

        // Persist to config (integer representation for storage only).
        OPT_SET("Sanae/Workspace/CurrentMode")->SetInt(static_cast<int>(mode));

        // Apply layout presets.
        // All presets preserve: active line, selection, edit state, video position.
        // No ASS mutation occurs during mode switching.
        switch (mode) {
                case sanae::WorkspaceMode::Translation:
                        // Calm translation-first layout.
                        SetDisplayMode(1, 1);
                        if (qc_dock) qc_dock->Hide();
                        break;
                case sanae::WorkspaceMode::QC:
                        // QC review layout.
                        SetDisplayMode(1, 0);
                        if (qc_dock) { qc_dock->Show(); qc_dock->Refresh(); }
                        break;
                case sanae::WorkspaceMode::Advanced:
                        // Full legacy Aegisub layout.
                        SetDisplayMode(1, 1);
                        if (qc_dock) qc_dock->Hide();
                        break;
        }

        Layout();
}

void FrameMain::ToggleFocusMode() {
        // Feature flag: if WorkspaceModes is OFF, Focus Mode is unavailable.
        if (!OPT_GET("Sanae/WorkspaceModes")->GetBool()) return;

        if (!focus_mode_active) {
                // Entering Focus Mode: save current mode and visual state.
                pre_focus_mode = workspace_mode;
                pre_focus_show_toolbar = OPT_GET("App/Show Toolbar")->GetBool();
                focus_mode_active = true;
                OPT_SET("Sanae/Workspace/FocusMode")->SetBool(true);

                // Hide visual noise: toolbar, audio.
                // Keep: video, edit box, grid, LineContextPanel.
                if (pre_focus_show_toolbar)
                        OPT_SET("App/Show Toolbar")->SetBool(false);
                SetDisplayMode(1, 0);  // video on, audio off
                if (qc_dock) qc_dock->Hide();
                Layout();
        } else {
                // Exiting Focus Mode: restore exact previous mode and visual state.
                focus_mode_active = false;
                OPT_SET("Sanae/Workspace/FocusMode")->SetBool(false);
                if (pre_focus_show_toolbar)
                        OPT_SET("App/Show Toolbar")->SetBool(true);
                SetWorkspaceMode(pre_focus_mode);
        }
}

void FrameMain::OnMouseWheel(wxMouseEvent &evt) {
        ForwardMouseWheelEvent(this, evt);
}
