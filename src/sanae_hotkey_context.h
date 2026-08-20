// sanae_hotkey_context.h — Sanae QC context dispatcher
// Phase 4 — enables "Sanae QC" hotkey context at runtime
//
// The existing Aegisub hotkey system dispatches by context string:
//   hotkey::check("Subtitle Edit Box", c, event)
//   hotkey::check("Subtitle Grid", c, event)
//   hotkey::check("Main Frame", c, event)
//
// "Sanae QC" context is declared in default_hotkey.json but never dispatched.
// This module provides the dispatch function that activates "Sanae QC" when:
//   - WorkspaceMode == QC
//   - Focus is NOT in SubsEditBox (SubsEditBox dispatches its own context)
//
// When focus IS in SubsEditBox, SubsEditBox::OnKeyDown dispatches "Subtitle Edit Box"
// context. "Sanae QC" bindings (Q/C/Enter/Backspace) do NOT fire because they
// are not in "Subtitle Edit Box" context. This is the focus protection.
//
// When WorkspaceMode != QC, "Sanae QC" is not dispatched at all.
// Q/C/Enter/Backspace pass through as normal keys.
//
// F4 is in both "Default" and "Sanae QC". When "Sanae QC" is active, Scan()
// returns the "Sanae QC" binding (exact context match takes precedence over Default).
// When "Sanae QC" is not active, "Default" binding fires via FrameMain::OnKeyDown.
// The command is the same (sanae/qc/next_problem) in both contexts, so behavior
// is consistent. The command itself checks WorkspaceMode/UserRole internally.

#pragma once

class wxKeyEvent;
namespace agi { class Context; }

namespace sanae {

// Check if Sanae QC context should be active.
// Returns true if WorkspaceMode == QC AND focus is not in a SubsTextEditCtrl.
bool qc_context_active(agi::Context *c);

// Dispatch a key event through the "Sanae QC" context.
// Returns true if the event was consumed (a hotkey fired).
// Returns false if no hotkey matched (caller should Skip the event).
bool check_qc_hotkey(agi::Context *c, wxKeyEvent &event);

} // namespace sanae
