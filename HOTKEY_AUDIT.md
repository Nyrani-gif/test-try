# Hotkey Audit

**Phase:** 0.9
**Source:** `src/libresrc/default_hotkey.json` (362 lines, 8 contexts)
**Authoritative:** SANAE_REVAMP_PLAN.md §4.8

## Existing contexts (8)

| Context | Purpose |
|---|---|
| Always | Global hotkeys (audio KP shortcuts, time/next/prev) |
| Audio | Audio timing workflow (Q/W/E/R/T/Y etc.) |
| Default | File/edit/grid/video operations (Ctrl-Q, Ctrl-S, F1-F3, etc.) |
| Styling Assistant | Styling dialog workflow |
| Subtitle Edit Box | Edit box specific (Alt-1..4 color buttons, Enter commit) |
| Subtitle Grid | Grid navigation (arrows, Ctrl-A) |
| Translation Assistant | Translation dialog workflow |
| Video | Video tools (A/S/D/F/G/H/J for tools, arrows for frame nav) |

## Existing bindings relevant to Sanae revamp

### Keys already in use (CONFLICT analysis)

| Key | Context | Binding | Conflict with plan? |
|---|---|---|---|
| `Alt-1` | Subtitle Edit Box | edit/color/primary | **YES** — plan wants Alt+1 for term apply |
| `Alt-2` | Subtitle Edit Box | edit/color/secondary | **YES** — plan wants Alt+2 |
| `Alt-3` | Subtitle Edit Box | edit/color/outline | **YES** — plan wants Alt+3 |
| `Alt-4` | Subtitle Edit Box | edit/color/shadow | **YES** — plan wants Alt+4 |
| `Alt-5` | (free) | — | OK for term apply #5 |
| `Alt-Down` | Default | grid/move/down | no conflict |
| `Alt-Up` | Default | grid/move/up | no conflict |
| `Alt-Left` | Subtitle Grid, Video | video/frame/prev/large | no conflict |
| `Alt-Right` | Subtitle Grid, Video | video/frame/next/large | no conflict |
| `Alt-O` | Default | app/options | no conflict |
| `Ctrl-T` | (free) | — | OK for add-term popover |
| `Ctrl-I` | Default | time/shift | **YES** — plan wants Ctrl+I for ignore term |
| `Ctrl-Shift-1/2/3` | (free) | — | OK for workspace modes |
| `Ctrl-Shift-F` | (free) | — | OK for focus mode |
| `Ctrl-Shift-S` | Default | subtitle/save/as | no conflict (different modifiers) |
| `Ctrl-Shift-D` | Default | edit/line/split/after | no conflict |
| `Ctrl-Shift-V` | Default | edit/line/paste/over | no conflict |
| `F1` | Default | help/contents | no conflict |
| `F2` | Default | subtitle/save | no conflict |
| `F3` | Default | subtitle/find/next | no conflict |
| `F4` | (free) | — | OK for next-problem |
| `F8` | Styling Assistant, Translation Assistant | preview | no conflict (different context) |
| `Q` | Audio | audio/play/selection/before | **see below** |
| `C` | Audio | time/lead/in | **see below** |
| `Enter` | Audio, Styling Assistant, Subtitle Edit Box, Translation Assistant | commit | **see below** |
| `Backspace` | (free) | — | OK for QC return |
| `Ctrl-Enter` | (free) | — | OK for Submit for QC |

## Conflict analysis

### 1. Alt+1..5 for terminology (Phase 2)

**Plan requirement (§4.8):** `Alt+1…Alt+5` in Subtitle Edit Box context for
applying terminology matches 1-5.

**Existing:** `Alt-1..4` in Subtitle Edit Box = color buttons (primary/secondary/outline/shadow).

**Analysis:** This is a direct conflict in the SAME context (Subtitle Edit Box).
Aegisub's hotkey system does NOT support context-conditional rebinding within
a single context — a key has one binding per context.

**Options:**

(a) **Reassign color buttons to different keys.** Breaks existing user muscle
    memory. Color buttons are used by typesetters/stylers in Advanced mode.

(b) **Use a different modifier for terminology.** e.g. `Alt+Shift+1..5` or
    `Ctrl+Alt+1..5`. Avoids conflict but diverges from plan's `Alt+1..5`.

(c) **Workspace-mode-conditional routing.** In Translation mode, Alt+1..5 =
    terminology; in Advanced mode, Alt+1..4 = color buttons. This requires
    Aegisub's hotkey system to support mode-conditional context activation,
    which it currently does NOT (contexts are fixed strings, not mode-derived).

(d) **New context "Sanae Terminology" activated when LineContextPanel is focused.**
    Most precise, but requires changes to hotkey dispatch logic.

**Recommendation:** This is a **UX CONFLICT** that must be surfaced to the user,
not silently resolved. Per plan §4.8: "не выбирай новый shortcut молча".

**Proposed for Phase 2:**
- Default: `Alt+Shift+1..5` for terminology (option b), documented as deviation.
- Future: implement option (d) — new "Sanae Terminology" context activated when
  TerminologyHintPanel has focus. This gives `Alt+1..5` in that context without
  conflict, matching the plan exactly. Requires hotkey dispatch extension.

**Status:** OPEN — requires user decision before Phase 2 implementation.
Do NOT silently pick `Alt+Shift+1..5`.

### 2. Ctrl+I for ignore term match (Phase 2)

**Plan requirement:** `Ctrl+I` in Subtitle Edit Box for ignoring a terminology match.

**Existing:** `Ctrl-I` in Default context = time/shift.

**Analysis:** Different contexts (Default vs Subtitle Edit Box). Aegisub's hotkey
system supports the same key in different contexts — the active context wins.
When SubsEditBox has focus, `Ctrl+I` would route to the Subtitle Edit Box binding.

**Resolution:** OK — add `Ctrl-I` to "Subtitle Edit Box" context for
`sanae/terminology/ignore`. No conflict in practice (Default's `Ctrl-I` only
fires when no other context is active).

### 3. Q / C / Enter / Backspace in Sanae QC context (Phase 3)

**Plan requirement:** `Q`, `C`, `Enter`, `Backspace` in new "Sanae QC" context
for QC actions (create issue, comment, accept, return).

**Existing:** `Q` (Audio: play/before), `C` (Audio: lead/in), `Enter` (multiple:
commit), `Backspace` (free).

**Analysis:** Plan §4.8 specifies: "QC single-key commands work ONLY in context
Sanae QC and NEVER intercept input in SubsEditBox." Aegisub's context system
supports this — "Sanae QC" context is active only when `WorkspaceMode==QC` AND
focus is not in SubsEditBox.

**Resolution:** OK — create new "Sanae QC" context. `Q`/`C`/`Enter`/`Backspace`
live there, scoped to QC mode. No conflict with Audio context (different active
context). `Enter` in Sanae QC = accept issue; `Enter` in Subtitle Edit Box = commit
line (no conflict — different active contexts).

### 4. F4 for next-problem (Phase 3)

**Plan requirement:** `F4` for "next problem" navigation.

**Existing:** F1=help, F2=save, F3=find/next. F4 is free.

**Resolution:** OK — `F4` in Always context (or new "Sanae QC" context).
`Shift+F4` and `Ctrl+F4` also free.

### 5. Ctrl+Enter for Submit for QC (Phase 4)

**Plan requirement:** `Ctrl+Enter` for Submit for QC.

**Existing:** `Enter` in multiple contexts, but `Ctrl+Enter` is free everywhere.

**Resolution:** OK — `Ctrl-Enter` in Always or Default context.

## New contexts to add

| Context | When active | Keys |
|---|---|---|
| `Sanae QC` | WorkspaceMode==QC AND focus not in SubsEditBox | Q, C, Enter, Backspace, F4, Shift+F4, Ctrl+F4 |

## New bindings to add (no conflict)

| Key | Context | Command | Phase |
|---|---|---|---|
| `Ctrl-T` | Subtitle Edit Box | sanae/terminology/add | 2 |
| `Ctrl-Shift-1` | Always | sanae/workspace/translation | 4 |
| `Ctrl-Shift-2` | Always | sanae/workspace/qc | 4 |
| `Ctrl-Shift-3` | Always | sanae/workspace/advanced | 4 |
| `Ctrl-Shift-F` | Always | sanae/workspace/focus | 4 |
| `F4` | Sanae QC | sanae/qc/next_problem | 3 |
| `Shift-F4` | Sanae QC | sanae/qc/prev_problem | 3 |
| `Ctrl-F4` | Sanae QC | sanae/qc/next_critical | 3 |
| `Ctrl-Enter` | Always | sanae/episode/submit_for_qc | 4 |

## Bindings REQUIRING user decision (OPEN)

| Key | Conflict | Options |
|---|---|---|
| `Alt-1..4` | Subtitle Edit Box color buttons vs terminology | (a) reassign colors, (b) Alt+Shift+1..5, (c) mode-conditional routing, (d) new focus context |

**Status:** Do NOT implement terminology hotkeys until this conflict is resolved
with the user. Phase 2 TerminologyHintPanel will show term suggestions without
hotkey shortcuts initially; clicking applies them. Hotkeys added after conflict
resolution.
