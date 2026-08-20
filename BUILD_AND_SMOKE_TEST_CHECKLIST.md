# Build and Smoke Test Checklist

**Purpose:** First executable build verification for the Sanae revamp (Phases 0–4).
All code was integrated against real Aegisub source types in a sandbox without
Meson/Ninja/wxHeaders. This checklist covers the first real compile + runtime
verification on a proper build environment.

**Prerequisites:**
- Meson ≥ 0.60, Ninja, C++20 compiler (MSVC or GCC), wxWidgets 3.2, boost,
  ICU, hunspell, ffmpeg, libass, ffms2 (all as Meson subprojects or system deps).
- Clone of https://github.com/yHdra/aegisubsanae with all Phase 0–4 changes applied.
- A real translation team project with at least one heavy episode (500+ lines,
  200+ terminology entries) for profiling.

---

## 1. Clean Configure / Build

```
meson setup build --native-file build-aux/windows-msvc.ini ...
meson compile -C build
meson test -C build --print-errorlogs
```

- [ ] `meson setup` completes without errors.
- [ ] `meson compile` completes without errors.
- [ ] All new source files are compiled (check for missing `meson.build` entries):
  - `src/sanae_baseline_fingerprint.cpp`
  - `src/sanae_terminology_index.cpp`
  - `src/sanae_local_line_id.cpp`
  - `src/sanae_review_issue.cpp`
  - `src/sanae_issue_registry.cpp`
  - `src/sanae_qc_profile.cpp`
  - `src/sanae_qc_checks.cpp`
  - `src/sanae_modified_after_issue.cpp`
  - `src/sanae_local_project_config.cpp`
  - `src/sanae_hotkey_context.cpp`
  - `src/qc_issue_dock.cpp`
  - `src/qc_quick_issue_popover.cpp`
  - `src/line_context_panel.cpp`
  - `src/terminology_hint_panel.cpp`
  - `src/terminology_entry_popover.cpp`
  - `src/terminology_dock.cpp`
  - `src/project_navigator_dock.cpp`
  - `src/project_search_dock.cpp`
  - `src/diff_dock.cpp`
  - `src/command/sanae_workspace.cpp`
  - `src/command/episode_workflow.cpp`
- [ ] All test files compile:
  - `tests/tests/sanae_baseline_fingerprint.cpp` (22 tests)
  - `tests/tests/sanae_terminology_index.cpp` (24 tests)
  - `tests/tests/sanae_local_line_id.cpp` (13 tests)
  - `tests/tests/sanae_review_issue.cpp` (24 tests)
  - `tests/tests/sanae_modified_after_issue.cpp` (9 tests)
  - `tests/tests/sanae_qc_profile.cpp` (7 tests)
  - `tests/tests/sanae_user_role.cpp` (2 tests)
  - `tests/tests/sanae_phase4.cpp` (15 tests)
- [ ] `msgfmt --check-format --check-header po/ru.po` passes.
- [ ] No compiler warnings beyond existing baseline.

## 2. Application Startup

- [ ] Application launches without crash.
- [ ] No missing-symbol / link errors at runtime.
- [ ] Default workspace mode is Translation (or legacy if WorkspaceModes flag is OFF).
- [ ] Status bar shows normal state.
- [ ] Menu bar visible with Sanae entries.

## 3. Old ASS Compatibility

- [ ] Open a pre-revamp `.ass` file — loads normally.
- [ ] Open a pre-revamp `.ass` file with extradata — loads normally.
- [ ] Save the file — ASS content is byte-identical (no new extradata added).
- [ ] Open a file with drawing lines, attachments, styles — all preserved.
- [ ] No Sanae binding is forced on non-Sanae ASS files.

## 4. Old Sidecar Compatibility

- [ ] Open a file with a version-3 sidecar (pre-revamp) — loads successfully.
- [ ] Version-3 sidecar fields (units, folders, sanae binding) are preserved.
- [ ] New fields (review_issues, local_line_ids) are empty (default-initialized).
- [ ] Save the file — sidecar is upgraded to version 4.
- [ ] Re-open the version-4 sidecar — all fields round-trip.
- [ ] Delete the sidecar — file still opens (degraded mode).

## 5. WorkspaceModes OFF Fallback

- [ ] With `Sanae/WorkspaceModes` = false (default):
  - [ ] Application starts in legacy Aegisub layout.
  - [ ] Ctrl+Shift+1/2/3/F do nothing (commands are no-ops).
  - [ ] QCIssueDock is hidden.
  - [ ] All existing Aegisub functionality works normally.
  - [ ] Opening/editing subtitles does not depend on new workspace layout.
  - [ ] No ASS mutation.

## 6. Translation / QC / Advanced Modes

Enable `Sanae/WorkspaceModes` = true for these tests.

- [ ] **Translation mode** (Ctrl+Shift+1):
  - [ ] Video + audio + edit box + grid visible.
  - [ ] QCIssueDock hidden.
  - [ ] LineContextPanel visible (if InlineTerminology is also ON).
  - [ ] Calm, translation-first feel.
- [ ] **QC mode** (Ctrl+Shift+2):
  - [ ] Video visible, audio hidden.
  - [ ] QCIssueDock visible with summary and list.
  - [ ] Clicking an issue navigates to the subtitle line + jumps video.
  - [ ] F4 navigates to next problem (ReadyForReview first for Reviewer).
- [ ] **Advanced mode** (Ctrl+Shift+3):
  - [ ] Full legacy Aegisub layout restored.
  - [ ] All professional tools accessible (styles, timing, karaoke, vector clip).
  - [ ] Legacy FinalReviewDialog accessible.
- [ ] **Mode switching**:
  - [ ] Active subtitle line preserved across switches.
  - [ ] Selection preserved.
  - [ ] Edit state / cursor position preserved.
  - [ ] Video position preserved.
  - [ ] ReviewIssue state unchanged.
  - [ ] No ASS content mutated.
  - [ ] Unsaved text not lost.

## 7. Advanced Legacy Functionality

- [ ] Style Manager opens and works.
- [ ] Timing post-processor works.
- [ ] ASS override tags edit correctly.
- [ ] Margins / layer / effect fields editable.
- [ ] Karaoke mode works.
- [ ] Vector clip tool works.
- [ ] Automation scripts run.
- [ ] Fonts collector works.
- [ ] Shift Times dialog works.
- [ ] Search & Replace works.
- [ ] Translation Assistant works.
- [ ] Styling Assistant works.

## 8. Focus Mode

- [ ] From Translation → Ctrl+Shift+F → toolbar + audio hidden, video + editor + grid remain.
- [ ] Exit Focus (Ctrl+Shift+F again) → Translation mode restored exactly.
- [ ] From QC → Focus → QCIssueDock also hidden.
- [ ] Exit → QC mode restored exactly (QCIssueDock reappears).
- [ ] From Advanced → Focus → toolbar hidden.
- [ ] Exit → Advanced restored exactly.
- [ ] Repeated toggle (Focus → exit → Focus → exit) — stable, no state drift.
- [ ] Window resize while Focus active — layout adjusts without crash.

## 9. EN Source Display

- [ ] With `split_box` checked (Show Original):
  - [ ] EN source text appears above RU editor.
  - [ ] EN source updates when active line changes.
  - [ ] EN source is read-only.
  - [ ] EN source does not trigger terminology heavy Search.
  - [ ] EN source does not alter subtitle data.

## 10. QCIssueDock

- [ ] Visible only in QC mode (when WorkspaceModes is ON).
- [ ] Shows summary: "N problems · N open · N waiting · N critical".
- [ ] Filter checkboxes: Critical, Open.
- [ ] No "my issues" filter (only device identity exists).
- [ ] Click on issue → subtitle line activated + video jumps.
- [ ] F4 → next problem (role-sensitive navigation).
- [ ] Shift+F4 → previous problem.
- [ ] Ctrl+F4 → next critical.
- [ ] Virtual list handles 1000+ issues without lag.

## 11. Hotkey Rebinding

- [ ] Open Preferences → Hotkeys.
- [ ] "Sanae QC" context appears in the tree.
- [ ] All Sanae commands visible (Default context: workspace, submit, F4).
- [ ] Subtitle Edit Box context: Ctrl-T (add term), Ctrl-I (ignore term).
- [ ] Terminology apply_1..5 appear in command dropdown (via "New" button).
- [ ] Rebind a Sanae command (e.g. change F4 to F5) → new binding works.
- [ ] Remove a default binding (e.g. delete Ctrl+Enter) → command still accessible via menu.
- [ ] Restart application → custom bindings persist.
- [ ] Default bindings NOT reintroduced over explicit user customization.

## 12. Subtitle Text Focus Protection

- [ ] In QC mode, focus the SubsEditBox (click in edit area).
- [ ] Type "Q", "C" — these appear as text, NOT as QC commands.
- [ ] Press Enter — commits the line (Subtitle Edit Box context), NOT QC accept.
- [ ] Press Backspace — deletes character, NOT QC return.
- [ ] Focus outside SubsEditBox (click on grid) → Q/C/Enter/Backspace fire QC commands.
- [ ] In Translation mode → Q/C/Enter/Backspace never fire QC commands.
- [ ] In Advanced mode → Q/C/Enter/Backspace never fire QC commands.

## 13. ReviewIssue Sidecar Restart Persistence

- [ ] Create a ReviewIssue on a line (via QuickIssuePopover or sidecar JSON).
- [ ] Close the file.
- [ ] Reopen the file.
- [ ] ReviewIssue is still present with correct:
  - [ ] state (open/ready_for_review/resolved/wont_fix)
  - [ ] body
  - [ ] resolution_note (if wont_fix)
  - [ ] version
  - [ ] baseline_text_hash
  - [ ] baseline_timing_hash
  - [ ] comments (immutable, preserved)
  - [ ] local_line_id
- [ ] Soft-deleted issues have `deleted_at` set.
- [ ] Diagnostics are NOT serialized (recomputed on load).
- [ ] modified_after_issue is NOT persisted (computed from baseline fingerprints).

## 14. local_line_id Rebind

- [ ] Create a ReviewIssue on line 5.
- [ ] Retime line 5 (change start/end).
- [ ] Save + reopen.
- [ ] ReviewIssue is rebound to the correct line (via source_hash fallback).
- [ ] Insert a line before line 5 → save + reopen.
- [ ] ReviewIssue follows the original line (not the new line).
- [ ] Delete line 5 → save + reopen.
- [ ] ReviewIssue is orphaned (no matching line).
- [ ] UI shows orphan indicator.
- [ ] Rebind orphan to a different line → works.
- [ ] Duplicate EN lines with different timing → both get distinct local_line_ids.
- [ ] Genuinely ambiguous duplicates (same EN + same timing) → orphaned, not misbound.

## 15. Submit for QC Blocking Semantics

- [ ] Create a line with an Error-severity Diagnostic (e.g. CPS > error threshold).
- [ ] Press Ctrl+Enter (Submit for QC).
- [ ] Submit is REJECTED with error message listing blocking issues.
- [ ] Workflow state is NOT mutated (no transition to in_review).
- [ ] Fix the Error → Submit succeeds.
- [ ] Info-only Diagnostics → Submit allowed.
- [ ] Warning-only Diagnostics → Submit allowed.
- [ ] Open ReviewIssues → Submit allowed (Submit is translator asking for review).
- [ ] Submit does NOT call Finalize.
- [ ] Finalize with review_state != done → warning only, user may continue.

## 16. Finalize Remains Separate

- [ ] Submit for QC (Ctrl+Enter) does NOT upload compact RUSUB.
- [ ] Finalize (existing menu/command) DOES upload compact RUSUB.
- [ ] Finalize does NOT require review_state=done.
- [ ] Finalize shows warning if review_state != done, but allows continuation.
- [ ] Finalize preserves production ASS, compact pending file, drafts, idempotency key.

## 17. Feature Flag Rollback

- [ ] Set `Sanae/WorkspaceModes` = false → restart.
- [ ] Legacy layout restored, all workspace commands are no-ops.
- [ ] Set `Sanae/UnifiedProblemsList` = false → restart.
- [ ] Old FinalReviewDialog accessible in Advanced mode.
- [ ] New QCIssueDock not shown.
- [ ] Set `Sanae/InlineTerminology` = false → restart.
- [ ] LineContextPanel hidden.
- [ ] Terminology still accessible via old TerminologyDialog.
- [ ] Set all three flags to false → application behaves like pre-revamp Aegisub.
- [ ] No ASS/sidecar corruption from flag toggling.

## 18. Profiling-Enabled Run

- [ ] Set `Sanae/Profiling/Enabled` = true in config.
- [ ] Open the heaviest available project (500+ lines, 200+ terms).
- [ ] Navigate through 20 lines → check `agi::log` Debug output for:
  - [ ] `sanae/profile/grid OnPaint` timings.
  - [ ] `sanae/profile/grid OnActiveLineChanged` timings.
  - [ ] `sanae/profile/grid SetColumnWidths` timings.
  - [ ] `sanae/profile/translation_project RebuildUnits` timings.
  - [ ] `sanae/profile/GenerateCandidates` phase timings (hunspell_load, sort_and_truncate).
  - [ ] `sanae/profile/TerminologyConsistencyIssues` timings.
- [ ] Open Final Review → check `sanae/profile/Finalize` phase timings if finalizing.
- [ ] Record baseline numbers for Phase 5 comparison.
- [ ] Set `Sanae/Profiling/Enabled` = false → no profiling overhead.

---

## Sign-off

After all items above are verified:

- [ ] Record any compile errors and fix.
- [ ] Record any runtime crashes and fix.
- [ ] Record any test failures and fix.
- [ ] Record baseline performance numbers for Phase 5.
- [ ] Update `IMPLEMENTATION_STATUS.md` with build verification results.
- [ ] Mark `FULL BUILD / GUI SMOKE VERIFICATION = PASSED` or document remaining issues.

Only after this checklist is fully verified should Phase 5 (performance optimization)
or Phase 6 (server integration) proceed.
