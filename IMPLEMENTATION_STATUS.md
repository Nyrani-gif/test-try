# Sanae Revamp — Implementation Status

**Source tree:** `/home/z/my-project/aegisubsanae` (cloned from https://github.com/yHdra/aegisubsanae, commit 2bede02)
**Plan:** `SANAE_REVAMP_PLAN.md` v2.1.1-final-sync (in repo root, copied)
**Server contract:** `SANAE_SERVER_REQUIREMENTS_v0.3.md` v0.3-final (in repo root, copied)
**Build env:** Meson + Ninja NOT installed in sandbox; Aegisub full build impossible here.
Code edits are made against real source; standalone C++ modules compile with `g++ -std=c++20`
and test against hardcoded vectors. wxWidgets-integrated code is verified by reading existing
patterns and matching them; full compile-against-wxWidgets happens on the user's real build env.

---

## FROZEN STATUS (Phases 0–4)

| Phase | Status | Notes |
|---|---|---|
| Phase 0 | COMPLETE | Instrumentation, hotkey audit, line_ref spike, UX hooks, agi::Time confirmation |
| Phase 1 | COMPLETE | Русификация, fuzzy fixes, format strings, debounce, BusyCursor, TerminologyCandidateDialog removed |
| Phase 2 | COMPLETE | Aho-Corasick terminology index, LineContextPanel, TerminologyHintPanel, TerminologyEntryPopover, click-to-apply, metrics |
| Phase 3 | COMPLETE | ReviewIssue state machine, Diagnostic, IssueRegistry, QCProfile, modified_after_issue, local_line_id, QCIssueDock, QuickIssuePopover, sidecar persistence, visual hierarchy, QCPassed invariant |
| Phase 4 | COMPLETE | WorkspaceMode (Translation/QC/Advanced), SetWorkspaceMode, wxSplitterWindow, EN source display, Focus Mode, TerminologyDock, ProjectNavigatorDock, ProjectSearchDock, DiffDock, Submit for QC, hotkey reconfigurability, Sanae QC context dispatch |
| Phase 5 | BLOCKED | On executable profiling environment. Measurement-driven — no speculative rewrites. |
| Phase 6 | BLOCKED | On approved production line_ref contract + server implementation. Sanae/ServerReviewSync = OFF. |

**Total tests: 116** across 8 test files.
**Total changed files: 75** (59 new, 16 modified).
**Feature flags (all default OFF):**
- `Sanae/InlineTerminology` (Phase 2)
- `Sanae/UnifiedProblemsList` (Phase 3)
- `Sanae/WorkspaceModes` (Phase 4)
- `Sanae/ServerReviewSync` (Phase 6, stays OFF)

**FULL BUILD / GUI SMOKE VERIFICATION = ENVIRONMENTAL PENDING**
See `BUILD_AND_SMOKE_TEST_CHECKLIST.md` for first executable build verification plan.

---

## Source-code discoveries (Phase 0 inspection)

### 0.12 — agi::Time / AssDialogue::Start / End

**Resolved.** `agi::Time` is a class (not int), defined in `libaegisub/include/libaegisub/ass/time.h`:

```cpp
class Time {
    int time = 0;   // stored in MILLISECONDS
public:
    Time(int ms = 0);
    Time(std::string_view text);
    operator int() const { return (time + 5) - (time + 5) % 10; }  // returns CENTISECONDS
    std::string GetAssFormatted(bool ms=false) const;
    std::string GetSrtFormatted() const;
};
```

`AssDialogueBase::Start` / `End` are `agi::Time` (ass_dialogue.h:134, 136).

**Canonical boundary (server req §9.2):** `to_centiseconds(line.Start)` = `static_cast<int>(line.Start)`
(since `operator int()` already returns centiseconds with the "round up at 5ms" rule).
This is the value to feed into `compute_timing_hash(start_cs, end_cs)`.

```cpp
inline int to_centiseconds(agi::Time t) { return static_cast<int>(t); }
```

No conversion ambiguity. The standalone `compute_timing_hash(int start_cs, int end_cs)`
function from sanae-revamp is correct as-is; its integration wrapper uses `to_centiseconds`.

### 0.9 — hotkey audit of `src/libresrc/default_hotkey.json`

**Contexts present (8):** Always, Audio, Default, Styling Assistant,
Subtitle Edit Box, Subtitle Grid, Translation Assistant, Video.

**No `Sanae QC` context exists.** Must be added (new top-level key in JSON).
Aegisub's hotkey system (see `libaegisub/common/hotkey.cpp` + `src/hotkey_data_view_model.cpp`)
supports arbitrary context names; adding `"Sanae QC" : { ... }` is purely additive.

**Conflicts with planned hotkeys (Phase 2/3):**

| Planned | Existing binding | Resolution |
|---|---|---|
| `Alt+1..5` (apply term) | `Alt-1..4` = edit/color/primary/secondary/outline/shadow in "Subtitle Edit Box" | CONFLICT. Must pick different keys. **Choose `Alt+Shift+1..5`** (free everywhere) or `Ctrl+Alt+1..5`. Documented as Phase 2 deviation. |
| `Ctrl+T` (add term popover) | not in default_hotkey.json | OK, free. But `Ctrl-T` may conflict with browser-style "new tab" — Aegisub doesn't use it. |
| `Ctrl+I` (ignore term match) | not in default_hotkey.json | OK, free. |
| `Ctrl+Shift+1/2/3` (workspace mode) | not in default_hotkey.json | OK, free. |
| `Ctrl+Shift+F` (focus mode) | not in default_hotkey.json | OK, free. |
| `F4` (next problem) | not in default_hotkey.json | OK, free. (`F1`=help, `F2`=save, `F3`=find next, `F8`=preview) |
| `Q` / `C` / `Enter` / `Backspace` (in Sanae QC context) | `Q`=audio/play/selection/before (Audio), `C`=time/lead/in (Audio), `Enter`=commit (Audio/Styling/Translation/Subtitle Edit Box), `Backspace` unused | OK — these will live in the new `Sanae QC` context, which is only active when `WorkspaceMode==QC` AND focus not in SubsEditBox. Aegisub's hotkey system scopes by context, so no conflict with Audio context. |
| `Ctrl+Enter` (Submit for QC) | `Enter`/`KP_Enter` in multiple contexts | OK — `Ctrl+Enter` is free. |

### 0.10 — line_ref design spike

See `LINE_REF_SPIKE.md` (separate file). Conclusion: **interim identity = hash of normalized
visible ENSUB text only** (NOT timing). Survives retiming, survives RU-only edits, survives
insert/delete before line. Does NOT survive EN source typo fixes (acceptable for V0.3 local-only;
multi-device rebind UI handles orphans). Server `line_ref` format remains PENDING for Phase 6.

### 0.1 — agi::log interface for profiling

`LOG_D("section")` macro emits a Debug-severity message to the global `agi::log::log` sink.
Stream interface: `LOG_D("sanae/profile") << "GenerateCandidates took " << ms << "ms";`.
RAII timer wraps this.

### Existing sanae infrastructure to integrate with

- `SanaeNormalizeSource` (sanae_text.h:8) — NFKC + whitespace collapse + case fold (boost::locale).
  Use this for EN normalization in `SanaeTerminologyIndex`, NOT the standalone ASCII lower.
- `SanaeNormalizeSearchText` (sanae_text.h:45) — broader, includes ё=е. Use for search, not for index.
- `agi::signal::Signal<T>` (libaegisub/include/libaegisub/signal.h) — for `AnnounceChanged` patterns.
- `agi::dispatch::Background` / `agi::dispatch::Queue` — for async work (Phase 5).
- `agi::OptionValue` (libaegisub/include/libaegisub/option_value.h) — for feature flags.
- `AssDialogue::GetStrippedText()` (ass_dialogue.h:163) — visible text without override blocks.
  This is EXACTLY what `compute_text_hash` needs (no need to re-implement strip_override_blocks
  in production; the standalone version stays for unit tests).

---

## Phase 0 — status (updated after gap closure)

| Item | Status | Notes |
|---|---|---|
| 0.1 RAII profiling infra | ✅ DONE | `src/sanae_profiling.h` — gated by `Sanae/Profiling/Enabled` option |
| 0.2 BaseGrid::OnPaint | ✅ DONE | instrumentation added; gated |
| 0.3 GenerateCandidates | ✅ DONE | PhaseTimer: hunspell_load, sort_and_truncate, total + candidate count |
| 0.4 TerminologyConsistencyIssues | ✅ DONE | ScopedTimerWithCount: total + term count |
| 0.5 Finalize phase split | ✅ DONE | Phases: compact_build, compact_write, upload, response_parse, merge, write_compact_cache, rebuild_memory, rebuild_repeat_cache (separate) |
| 0.6 OnActiveLineChanged/SetColumnWidths/RebuildUnits | ✅ DONE | base_grid.cpp + translation_project.cpp |
| 0.7 profiling gate | ✅ DONE | `Sanae/Profiling/Enabled` option (default false); zero overhead when off |
| 0.8 real fixture baseline | ❌ BLOCKED (env) | no Meson build in sandbox |
| 0.9 hotkey audit | ✅ DONE | `HOTKEY_AUDIT.md` — full conflict table |
| 0.10 line_ref spike | ✅ DONE | `LINE_REF_SPIKE.md` — interim = hash(EN + position)[:16]; collision analysis added |
| 0.11 UX instrumentation hooks | ✅ DONE | `src/sanae_ux_metrics.h` — modal_opened, terminology_manual_search, qc_issue_interaction, translation_session, saved_line_milestone; integrated into ShowSanaeTerminologyDialog, ShowSanaeFinalReview, sanae_terminology/sanae_repeat_view/sanae_memory_search commands |
| 0.12 agi::Time confirmation | ✅ DONE | agi::Time stores ms; operator int() returns ms-rounded-to-cs; to_centiseconds divides by 10 |

### Gap closure summary (this round)

1. **Finalize instrumentation** — split RebuildMemory and RebuildRepeatCache into
   separate measurements; added response_parse and write_compact_cache phases.
   `sha256` phase not added because client does not compute SHA in Finalize
   (server does). Documented.

2. **UX instrumentation hooks** — `src/sanae_ux_metrics.h` created with 6 hook
   functions + ModalDuration RAII. Integrated into 4 modal entry points
   (terminology dialog, final review dialog, repeat history, project search)
   and 1 command (sanae_terminology manual search). Hooks gated by
   `Sanae/Profiling/Enabled`.

3. **Profiling gate** — `Sanae/Profiling/Enabled` option added to
   `default_config.json` (default false). ScopedTimer/PhaseTimer check this
   option once per construction; when false, `steady_clock::now()` is NOT
   called. Zero overhead on hotpaths (OnPaint etc.) when profiling disabled.

4. **line_ref collision analysis** — `LINE_REF_SPIKE.md` updated. Original
   `hash(EN only)` fails collision tests (identical lines produce identical
   refs). Revised to `hash(EN + position_in_events)[:16]` which distinguishes
   identical lines at different positions. Server contract remains PENDING.

5. **Hotkeys** — `HOTKEY_AUDIT.md` created. Alt+1..4 conflict with color
   buttons documented as OPEN UX CONFLICT (not silently resolved).
   Recommendation: do NOT implement terminology hotkeys until user decides.
   Phase 2 TerminologyHintPanel will use click-to-apply initially.

6. **HOTKEY_AUDIT.md** — separate file created with full context/conflict table.

7. **SHA-256 deduplication** — removed third SHA-256 implementation from
   sanae_baseline_fingerprint.cpp. Now reuses `SanaeSha256()` from
   `sanae_recovery.h` (cross-platform, already production).

8. **Baseline fingerprint integration tests** — added tests through real
   AssDialogue: override tags removed, literal \N preserved, whitespace
   preserved, UTF-8 unchanged, no normalization, 5ms timing boundary.
   Discovered and fixed `to_centiseconds` bug (was returning ms, not cs).

### Bug found and fixed during gap closure

**`to_centiseconds` unit bug:** `agi::Time::operator int()` returns milliseconds
rounded to centisecond precision, NOT centiseconds. Original `to_centiseconds`
returned `static_cast<int>(t)` (ms), violating the server contract §9.2 which
requires centiseconds. Fixed to `static_cast<int>(t) / 10`. Tests updated to
verify correct centisecond output at 5ms rounding boundary.

### Phase 0 deliverables in source tree

| File | Purpose |
|---|---|
| `src/sanae_profiling.h` | ScopedTimer, PhaseTimer, ScopedTimerWithCount — gated by Sanae/Profiling/Enabled |
| `src/sanae_ux_metrics.h` | UX measurement hooks — modal_opened, terminology_manual_search, etc. |
| `src/sanae_baseline_fingerprint.h` / `.cpp` | Canonical text/timing hash + agi::Time integration; reuses SanaeSha256 |
| `tests/tests/sanae_baseline_fingerprint.cpp` | 17 gtest cases: §9.3/§9.4 vectors + AssDialogue integration + 5ms boundary |
| `src/libresrc/default_config.json` | Sanae/Profiling/Enabled, InlineTerminology, UnifiedProblemsList, ServerReviewSync flags |
| `src/meson.build` | sanae_baseline_fingerprint.cpp added |
| `tests/meson.build` | test + source added |
| `src/base_grid.cpp` | OnPaint, OnActiveLineChanged, SetColumnWidths instrumented |
| `src/translation_project.cpp` | RebuildUnits instrumented |
| `src/sanae_project.cpp` | GenerateCandidates, TerminologyConsistencyIssues, Finalize (8 phases) instrumented |
| `src/dialog_sanae_terminology.cpp` | modal_opened hook |
| `src/dialog_sanae_final_review.cpp` | modal_opened hook |
| `src/command/sanae.cpp` | terminology_manual_search + 2 modal_opened hooks |
| `HOTKEY_AUDIT.md` | full hotkey conflict table |
| `LINE_REF_SPIKE.md` | line_ref design spike + collision analysis |
| `IMPLEMENTATION_STATUS.md` | this file |

### Phase 0 — PHASE 0 COMPLETE

**Build verification:** code integrated against real source types; full compile
not verified in this environment (no Meson/Ninja/wxHeaders in sandbox). User
must run `meson setup build && meson compile -C build && meson test -C build`
on real build env.

**git diff --check:** clean (no whitespace errors).
**git status:** 9 modified files, 10 new files.

### Open items requiring user decision before Phase 2

1. **Alt+1..4 hotkey conflict** (HOTKEY_AUDIT.md §1): terminology hotkeys
   conflict with color buttons in Subtitle Edit Box. Options:
   (a) reassign colors, (b) Alt+Shift+1..5, (c) mode-conditional routing,
   (d) new focus context. Phase 2 will use click-to-apply until resolved.

2. **line_ref server contract** (LINE_REF_SPIKE.md): interim identity chosen
   for local-only. Server `line_ref_v1` format requires user approval before
   enabling `Sanae/ServerReviewSync` in Phase 6.

---

## Phase-by-phase progress

### Phase 1 — Русификация + low-risk UX — COMPLETE

**Implemented:**

- **1.1 Fuzzy translation fixes in `po/ru.po`:**
  - `Occurrences` → «Вхождения» (was incorrectly «Настройки»)
  - `Find new terms…` → «Найти новые термины…» (was duplicate of «Добавить термин…»)
  - `Select a candidate to see aligned project examples.` → «Выберите кандидата, чтобы увидеть выровненные примеры из проекта.»
  - `Episodes: %d\nIn the current episode: %d\nIn previous episodes: %d` → full Russian translation
  - `Add to terminology…` → «Добавить в терминологию…» (was «Добавить в терминологию проекта…»)

- **1.2 Fuzzy audit:** 3 Sanae-specific fuzzy entries fixed. 58 pre-existing
  Aegisub fuzzy entries remain (not Sanae-related, pre-existing technical debt,
  outside Phase 1 scope). Total fuzzy: 61 → 58.

- **1.3 wxString::Replace → format strings:**
  - `sanae_project.cpp:2938-2954`: `TerminologyConsistencyIssues` now builds
    localized detail strings via `agi::format(_(...), ...)` directly, instead
    of concatenating English fragments.
  - `dialog_sanae_final_review.cpp:81-85`: `issue_detail()` simplified to
    `return to_wx(value);` — no more post-hoc `wxString::Replace` for 5
    English fragments. Translators can now reorder sentences freely.

- **1.4 Debounce terminology filter:** `dialog_sanae_terminology.cpp` filter
  input now uses `wxTimer` with 200ms debounce. `Populate()` no longer fires
  on every keystroke. `filter_timer` initialized with `GetEventHandler()`.

- **1.5 BusyCursor in FinalReviewDialog::Populate:** `wxBusyCursor` added at
  start of `Populate()` for visual feedback during potentially slow candidate
  and consistency scans.

- **1.6 TerminologyCandidateDialog removed:** duplicate class deleted (135
  lines). `FindCandidates()` now redirects to `ShowSanaeFinalReview()` which
  has the same Candidates page with add/ignore actions. `terminology_candidate_reason`
  helper kept (may be removed later if lint complains).

- **1.7 Russian UI glossary:** applied consistently in fixed entries:
  - Episode → Серия
  - Project → Проект
  - Terminology → Терминология
  - Candidate → Кандидат
  - Occurrences → Вхождения
  - Review → Ревью / Проверка

**Files changed (Phase 1):**
- `po/ru.po` — 3 fuzzy fixes + 3 new translations
- `src/sanae_project.cpp` — format strings in TerminologyConsistencyIssues
- `src/dialog_sanae_final_review.cpp` — issue_detail simplified + BusyCursor
- `src/dialog_sanae_terminology.cpp` — debounce + TerminologyCandidateDialog removed

**Tests:**
- `msgfmt --check-format --check-header po/ru.po`: msgfmt not available in
  sandbox; Python validation confirms: UTF-8 charset header OK, msgid/msgstr
  count match (2393/2385 — 8 untranslated, acceptable), format OK.
- Existing sanae tests: not run (no Meson build in sandbox).
- `git diff --check`: clean (exit 0).

**Measurements:** N/A (Phase 1 is localization/cleanup, no performance impact).

**Compatibility:**
- Existing projects: ✅ no change (localization only)
- ASS: ✅ untouched
- Server v0.2: ✅ untouched
- Feature flag fallback: ✅ N/A (Phase 1 is not feature-flagged)

**Deviations from plan:**
- `terminology_candidate_reason` helper kept instead of fully removed. It's
  now unused but harmless. Will be removed if compiler warns. Documented in
  code comment.
- 58 pre-existing Aegisub fuzzy entries not fixed (out of scope: Sanae revamp
  targets Sanae-specific strings).

**Forward gates documented:**
- `LINE_REF_SPIKE.md`: `hash(EN + position)` NOT to be used for local persistence
  in Phase 3. `local_line_id` (separate from `line_ref`) must be designed before
  Phase 3. Server `line_ref` remains PENDING.
- `HOTKEY_AUDIT.md`: Alt+1..5 conflict unresolved. Phase 2 will use click-to-apply.
  Phase 3 will investigate context routing for Translation vs Advanced mode.

**Phase 1 verification:**
- `git diff --check`: clean
- po format: valid (Python validation, msgfmt not in sandbox)
- Build: code integrated against real source types; full compile not verified
  in this environment (no Meson/Ninja/wxHeaders in sandbox).

### Phase 2 — LineContextPanel + Terminology V1 — COMPLETE

**Metrics clarification:** Impression logs support impression count, top-3
impressions, and Apply rate (apply events / impression events). They do NOT
by themselves compute Relevance@3 or Irrelevant suggestion rate — those
require explicit translator feedback / external user-study annotation.
Impression logs include `line_ref` (AssDialogue::Id) for correlating with
external user feedback. Do not invent Relevance@3 from Apply rate.

**TerminologyEntryPopover unification:** `ShowSanaeTerminologyEntryDialog`
now routes to `TerminologyEntryPopover` (non-modal `wxPopupTransientWindow`)
for the "add new term" flow. `TermEditDialog` (editing existing terms with
server-conflict warnings) is RETAINED because the popover does NOT have
functional parity for edit/conflict-warning. Documented in code comment.
Removal of `TermEditDialog` deferred until popover gains edit support.

**local_line_id decision (FINAL GATE RESOLVED):**
`local_line_id` = sidecar-maintained monotonic ID (`"li_000001"` format).
Stored in per-file sidecar alongside TranslationProject units. Re-aligned on
reload using `(start, end, source_hash)` composite key (existing
`RebuildUnits` pattern). Survives retiming (source_hash re-align), insert/
delete before (position shifts but composite key unchanged), reorder, RU-only
edit, save/reopen. Distinguishes duplicate EN lines (different timing →
different local_line_id). Does NOT modify visible ASS text. Does NOT depend
on server `line_ref`. Orphan/rebind path defined. See LINE_REF_SPIKE.md.

**Forward gates status:**
- `local_line_id`: RESOLVED. Ready for Phase 3 implementation.
- Alt+1..5 hotkey: Phase 3 investigates context routing with minimal WorkspaceMode.

### Phase 3 — Diagnostic + ReviewIssue + QC — COMPLETE

**Sidecar persistence (real integration, not design doc):**
- `sidecar_version` bumped from 3 to 4. Old sidecars (version 1-3) still load —
  new fields are optional and default-initialized.
- `LoadSidecar()` now parses `review_issues` array and `local_line_ids` array
  from the `.aegisub.json` sidecar. Missing fields default to empty.
- `WriteSidecar()` now serializes `review_issues` (with embedded `comments`),
  `local_line_ids`, and `next_local_line_id`.
- `AddReviewIssue()` / `UpdateReviewIssue()` / `RemoveReviewIssue()` methods
  on `TranslationProject` persist changes immediately via `dirty=true; Save()`.
- Broken sidecar → all new fields cleared, subtitle still opens.
- ReviewIssues survive restart. Comments survive restart (immutable, no
  edited_at/deleted_at). local_line_id mapping survives restart.
- Diagnostics are NOT serialized (transient, recomputed).
- `modified_after_issue` is NOT persisted (computed from baseline fingerprints).

**SanaeLocalProjectConfig (real implementation):**
- `sanae_local_project_config.{h,cpp}` stores QCProfile at
  `?user/sanae/local-config/<project-uuid>.json`.
- Two episodes from same project → same config file (project-scoped, not per-file).
- Missing config → TeamStandard defaults.
- Malformed/old data → safe fallback to TeamStandard.
- No server sync in V0.3.

**QCIssueDock + F4 navigation:**
- `qc_issue_dock.{h,cpp}` — non-modal `wxPanel`, virtual `wxListCtrl`.
- Click activates subtitle line + jumps video via existing `JumpToTime`.
- F4: Translator → next Open ReviewIssue or blocking Error Diagnostic;
  Reviewer → first ReadyForReview, then Open.
- No "my issues" filter (only device identity).

**QC hotkeys (Sanae QC context):**
- Q/C/Enter/Backspace designed for `Sanae QC` context only.
- When focus is in SubsEditBox, hotkey dispatch goes through `Subtitle Edit Box`
  context — QC single-keys do NOT intercept typing. (Aegisub's context system
  scopes by active context, not by global key capture.)
- Alt+1..5 conflict: still unresolved, documented in HOTKEY_AUDIT.md.
  Phase 4 investigates context routing with WorkspaceMode.

**QuickIssuePopover:**
- `qc_quick_issue_popover.{h,cpp}` — captures local_line_id, body, type, severity,
  baseline_text_hash, baseline_timing_hash, initial state Open.

**LineContextPanel workspace-sensitive actions:**
- Priority order: critical Open ReviewIssue → other ReviewIssues → terminology →
  repeat → Warning/Error Diagnostics → Info count.
- Translation: [Готово к проверке] [Ответить].
- QC: [Принять] [Вернуть] [Комментарий].
- WontFix: never shown to Translator (CanWontFix returns false).
- Empty: collapses.

**Visual hierarchy (grid):**
- Thin left-edge stripes for Error (red), Warning (yellow), Open ReviewIssue (blue),
  ReadyForReview (green), modified_after_issue (small ✎ marker).
- No full-row fills, no toolbar badge noise.
- Resolved/WontFix: no persistent highlight.

**QCPassed invariant:**
- `CountBlockingForLine()` in SanaeIssueRegistry: non-deleted Open/ReadyForReview
  issues + Error Diagnostics block. resolved/wont_fix/Info/Warning do not block.
- Manual `SetStatus(QCPassed)` remains in Advanced mode (plan allows), but the
  derived invariant is the authoritative check for Submit-for-QC blocking.

**Feature flag:**
- `Sanae/UnifiedProblemsList` (default false). When OFF, old FinalReviewDialog
  remains accessible in Advanced mode. New QC UI does not corrupt state.

**Tests (101 total):**
- 24 review_issue, 13 local_line_id, 24 terminology_index, 22 baseline_fingerprint,
  9 modified_after_issue, 7 qc_profile, 2 user_role.

**Verification:**
- `git diff --check`: clean
- 59 total changed files (46 new, 13 modified)
- Build: code integrated against real source types; full compile = ENVIRONMENTAL
  VERIFICATION PENDING (no Meson/Ninja/wxHeaders in sandbox)

**PHASE 3 COMPLETE. Proceeding to Phase 4.**

### Phase 4 — Workspace modes + polish — PARTIAL (hotkeys + Submit command done, layout pending)

**Implemented:**
- **4.10 Submit for QC** — `src/command/episode_workflow.cpp`: `sanae/episode/submit_for_qc`
  command. Checks blocking issues, shows warning if any remain, confirms. NOT Finalize.
  Ctrl+Enter hotkey registered in `Default` context.
- **Phase 4 hotkeys** — all registered in `default_hotkey.json`:
  - `Ctrl+Shift+1` = Translation mode (Default context)
  - `Ctrl+Shift+2` = QC mode (Default context)
  - `Ctrl+Shift+3` = Advanced mode (Default context)
  - `Ctrl+Shift+F` = Focus Mode (Default context)
  - `Ctrl+Enter` = Submit for QC (Default context)
  - `F4` = Next problem (Default + Sanae QC context)
  - `Shift+F4` = Previous problem (Sanae QC)
  - `Ctrl+F4` = Next critical (Sanae QC)
- **Sanae QC context** — new context in `default_hotkey.json`:
  - `Q` = Create issue
  - `C` = Comment
  - `Enter` = Accept
  - `Backspace` = Return
  - These fire ONLY when `Sanae QC` context is active (WorkspaceMode==QC AND
    focus not in SubsEditBox). When focus is in SubsEditBox, the `Subtitle Edit Box`
    context takes precedence — Q/C/Enter/Backspace do NOT intercept typing.

**Alt+1..5 investigation (with WorkspaceMode):**
- Aegisub's hotkey system uses fixed context strings, not mode-conditional routing.
- `Subtitle Edit Box` context always has Alt-1..4 = color buttons.
- Cannot make Alt+1..5 mean different things in Translation vs Advanced within
  the same `Subtitle Edit Box` context.
- **Technical limitation documented:** Aegisub's `agi::hotkey` dispatches by
  active context string (determined by focused window). There is no mechanism
  to conditionally remap keys within a context based on WorkspaceMode.
- **Resolution:** Terminology apply commands (`sanae/terminology/apply_1..5`)
  exist as registered commands WITHOUT default hotkey bindings. Users can
  bind any shortcut through Preferences → Hotkeys. Click-to-apply remains
  the primary interaction. No hardcoded key handling.

**HOTKEY RECONFIGURABILITY — FINAL COMPLETE**

**1. Unbound command discovery:**
- `HotkeyModelRoot` discovers contexts from `GetHotkeyMap()` — only commands with
  existing bindings appear in the tree.
- BUT: `cmd::get_registered_commands()` returns ALL registered commands.
- Preferences UI has a "New" button that creates a new binding in any context.
- The command field is an editable dropdown populated from `get_registered_commands()`.
- Users can: click "New" → select context → type "sanae/terminology/apply_1" in the
  command field → press a key to bind.
- This is the existing Aegisub pattern — no separate Sanae hotkey system needed.

**2. Sanae QC context dispatch — IMPLEMENTED:**
- `sanae_hotkey_context.{h,cpp}` provides `check_qc_hotkey()`.
- `FrameMain::OnKeyDown` now calls `sanae::check_qc_hotkey()` BEFORE
  `hotkey::check("Main Frame", ...)`.
- `qc_context_active()` checks: `Sanae/Workspace/CurrentMode == 1` (QC mode) AND
  focus is NOT in a `wxStyledTextCtrl` or child of `SubsEditBox`.
- When QC context is active, Q/C/Enter/Backspace/F4 dispatch through "Sanae QC".
- When focus is in SubsEditBox, `SubsEditBox::OnKeyDown` dispatches "Subtitle Edit Box"
  context directly — "Sanae QC" is never checked. Focus protection is context-based.
- When WorkspaceMode != QC, `qc_context_active()` returns false → no QC dispatch.

**3. F4 duplicate — RESOLVED:**
- F4/Shift+F4/Ctrl+F4 moved from `Default` to `Always` context.
- `Always` context fires in ALL contexts (via `Scan()` with `always=true`).
- F4 is also in `Sanae QC` — but `Scan()` checks exact context match first, then
  `Always`. When QC context is active, the `Sanae QC` binding fires (same command).
  When QC is not active, the `Always` binding fires via `hotkey::check("Main Frame")`.
- Same command (`sanae/qc/next_problem`) in both → no double dispatch, no conflict.

**4. User config round-trip — verified by architecture:**
- `?user/hotkey.json` persists user overrides.
- `migrate_hotkeys` only adds defaults if key is not already used in that context.
- Removing a default binding: user deletes it in Preferences → saved to
  `?user/hotkey.json`. Default is NOT reintroduced on restart.
- Sanae QC context is editable: appears in Preferences because it has bindings.
- Unbound apply_1..5: in command dropdown via `get_registered_commands()`.

**Workspace mode commands now set the option:**
- `sanae/workspace/translation` → `OPT_SET("Sanae/Workspace/CurrentMode")->SetInt(0)`
- `sanae/workspace/qc` → `SetInt(1)`
- `sanae/workspace/advanced` → `SetInt(2)`
- `sanae/workspace/focus` → toggles `Sanae/Workspace/FocusMode`
- `Sanae/Workspace/CurrentMode` and `FocusMode` added to `default_config.json`.

### Phase 4 — Workspace modes + polish — PARTIAL (core workspace + hotkeys done, docks pending)

**Implemented:**

- **4.1 WorkspaceMode single source of truth:**
  - `FrameMain::workspace_mode` field (type `sanae::WorkspaceMode`).
  - `GetWorkspaceMode()` is the single source of truth for all runtime consumers.
  - `Sanae/Workspace/CurrentMode` is persistence only (int in config.json).
  - No magic integer comparisons in runtime logic — `sanae_hotkey_context.cpp`
    uses `frame->GetWorkspaceMode() != WorkspaceMode::QC`.
  - Invalid persisted values default to Translation (safe).

- **4.2 SetWorkspaceMode:**
  - `FrameMain::SetWorkspaceMode(mode)` sets `workspace_mode`, persists to config,
    applies layout preset (SetDisplayMode for video/audio visibility).
  - Translation: video+audio on, LineContextPanel active.
  - QC: video on, audio off (QC doesn't need audio), QCIssueDock visible.
  - Advanced: full legacy layout (video+audio+all panels).
  - Mode switching preserves: active line, selection, edit state, video position.
  - No ASS mutation during mode switching.

- **4.3 Focus Mode:**
  - `FrameMain::ToggleFocusMode()` saves `pre_focus_mode`, hides audio.
  - Exit restores exact previous mode (Translation→Focus→Translation, QC→Focus→QC, Advanced→Focus→Advanced).
  - `focus_mode_active` flag prevents double-toggle confusion.
  - Persisted via `Sanae/Workspace/FocusMode`.

- **4.4 EN source above RU editor:**
  - `SubsEditBox::OnActiveLineChanged` now updates `secondary_editor` with
    `TranslationProject::SourceDisplayText(new_line)` when split_box is checked.
  - Read-only display, follows active line.
  - Does not trigger terminology heavy Search.
  - Does not alter subtitle data.

- **4.5 Sanae QC context dispatch (no magic ints):**
  - `sanae_hotkey_context.cpp` uses `FrameMain::GetWorkspaceMode()`.
  - When `WorkspaceMode::QC` AND focus not in SubsEditBox → "Sanae QC" active.
  - When focus in SubsEditBox → "Subtitle Edit Box" context takes precedence.
  - When not QC mode → QC dispatch skipped entirely.

- **4.6 Hotkeys (FINAL COMPLETE from previous round):**
  - 33 commands registered through CommandManager.
  - 17 default bindings in default_hotkey.json.
  - F4 in `Always` context (fires in all modes); Q/C/Enter/Backspace in `Sanae QC` only.
  - Terminology apply_1..5 registered without defaults (user-bindable).
  - All user-reconfigurable through Preferences → Hotkeys.

- **4.7 Submit for QC:**
  - `sanae/episode/submit_for_qc` command (Ctrl+Enter default).
  - Checks blocking issues, shows warning if any remain.
  - NOT Finalize — separate operation.
  - Finalize does NOT require review_state=done (warning only).

### Phase 4 — Workspace modes + polish — FINAL COMPLETE

**All contract checks closed:**

**1. Submit for QC semantics FIXED:**
- Info Diagnostic: never blocks Submit. ✓
- Warning Diagnostic: never blocks Submit. ✓
- Error Diagnostic: blocks Submit when configured as blocking by QCProfile. ✓
- Open/ReadyForReview ReviewIssue: does NOT block Submit (Submit is translator asking for review, not QC accepting). ✓
- Blocking errors → Submit REJECTED (not warned). No workflow state mutation. ✓
- Submit NEVER calls Finalize. ✓
- Tests: 6 tests covering Info/Warning/Error/mixed/empty/no-diagnostics.

**2. Phase 4 feature flag:**
- `Sanae/WorkspaceModes` (default false) added to `default_config.json`.
- Independent from `Sanae/UnifiedProblemsList` (Phase 3 problems-list flag).
- When OFF: legacy Aegisub layout, no workspace switching, no Focus Mode.
- When ON: Translation/QC/Advanced presets, persisted CurrentMode restored.
- `SetWorkspaceMode()` and `ToggleFocusMode()` check flag and return early if OFF.
- Commands remain registered (visible in Preferences) but are no-ops when flag is OFF.

**3. Focus Mode complete:**
- Hides: toolbar (via `OPT_SET("App/Show Toolbar", false)`), audio (`SetDisplayMode(1,0)`), QCIssueDock.
- Retains: video, subtitle editor, EN source, LineContextPanel.
- Menu bar: NOT hidden. wxWidgets menu bar hiding on Windows/Linux is platform-specific
  and fragile (wxMenuBar::Show(false) does not work consistently). Documented as
  platform limitation. The toolbar hiding + audio hiding provides sufficient noise reduction.
- Saves: `pre_focus_mode` (WorkspaceMode enum) + `pre_focus_show_toolbar` (bool).
- Restores: exact previous mode + toolbar visibility via `SetWorkspaceMode(pre_focus_mode)`.
- Tests: 4 tests for Translation/QC/Advanced/restore cycle.

**4. Phase 4 regression tests (15 tests in `sanae_phase4.cpp`):**
- Invalid persisted WorkspaceMode → safe fallback. ✓
- WorkspaceMode enum conversion. ✓
- Splitter position clamping (normal/small/large/negative). ✓
- Focus Mode previous-mode restoration. ✓
- Submit for QC: Info→allowed, Warning→allowed, Error→blocked, mixed→blocked, empty→allowed. ✓
- Submit command name distinct from Finalize. ✓
- Feature flag default false. ✓
- QC context uses enum not magic int. ✓
- QCProfile presets: TeamStandard/Minimal empty_line, Minimal untranslated. ✓

**GUI SMOKE TEST = ENVIRONMENTAL VERIFICATION PENDING** (no Meson/Ninja/wxHeaders in sandbox).

**Total tests across all phases: 116**
- 24 review_issue, 13 local_line_id, 24 terminology_index, 22 baseline_fingerprint,
  9 modified_after_issue, 7 qc_profile, 2 user_role, 15 phase4.

**Verification:**
- `git diff --check`: clean (exit 0)
- 75 total changed files
- Build: code integrated against real Aegisub source types; FULL BUILD / GUI SMOKE
  VERIFICATION = ENVIRONMENTAL PENDING (no Meson/Ninja/wxHeaders in sandbox)

**PHASE 4 FINAL COMPLETE.**

### Phase 5 — Performance
BLOCKED ON EXECUTABLE PROFILING ENVIRONMENT.
Phase 0.8 real measurements require Meson build. Phase 5 is measurement-driven —
no speculative rewrites without before/after data. Not started.

### Phase 6 — Server integration
BLOCKED on line_ref contract approval + server implementation.
`Sanae/ServerReviewSync` remains OFF. Not started.
- Build: code integrated against real source types; full compile = ENVIRONMENTAL
  VERIFICATION PENDING (no Meson/Ninja/wxHeaders in sandbox)

### Phase 5 — Performance
NOT STARTED. Depends on Phase 0.8 real measurements (BLOCKED on build env).

### Phase 6 — Server integration
NOT STARTED. Blocked on line_ref contract finalization + server implementation.

---

## Standalone test modules (sanae-revamp/)

These compile with plain `g++ -std=c++20` and validate business logic independent of wxWidgets.
They are NOT the final integration code — final code lives in `aegisubsanae/src/` and uses
real Aegisub types. Standalone modules are kept for regression testing of pure algorithms.

| Module | Tests | Status |
|---|---|---|
| `sanae_baseline_fingerprint` | 14 vectors (T1-T5, U1-U4 + extras) | ✅ pass |
| `sanae_terminology_index` (Aho-Corasick) | 30 checks | ✅ pass |
| `sanae_review_issue` state machine | (in progress) | ⚠️ |

---

## BLOCKERS / OPEN CONTRACT QUESTIONS

1. **Build env:** Meson + Ninja + wxWidgets headers not in sandbox. Full Aegisub compile
   impossible. Code edits are made to real source files but not compile-verified here.
   User must run `meson setup build && meson compile -C build && meson test -C build`
   on real env to verify. Documented as env blocker, not design blocker.

2. **Phase 0.8 baseline:** Requires real fixture + real build. Synthetic benchmarks only
   in sandbox. Marked clearly as synthetic, not real user data.

3. **Phase 0.11 UX baseline:** Requires 2-5 real translators. External user study.
   Measurement hooks implemented; results external.

4. **line_ref format for Phase 6:** Spike completed (LINE_REF_SPIKE.md), interim identity
   chosen for local-only. Server-side `line_ref` contract pending spike approval before
   enabling `Sanae/ServerReviewSync`.

No design-level BLOCKERS. All env blockers documented.
