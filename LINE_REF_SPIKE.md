# line_ref Design Spike

**Phase:** 0.10
**Status:** COMPLETED — interim identity chosen for local-only; server contract PENDING.
**Authoritative:** SANAE_REVAMP_PLAN.md §5.5, SANAE_SERVER_REQUIREMENTS_v0.3.md §10

## Goal

Choose a `line_ref` format that lets a `ReviewIssue` follow an ASS line through
common mutations, so that a reviewer's remark stays attached to the right line
even after the translator edits timing or text.

## Mutation scenarios tested (analysis, not code)

| # | Mutation | Desired behavior |
|---|---|---|
| 1 | Retiming (00:10.00→00:12.00 became 00:10.05→00:12.05) | Issue follows the line |
| 2 | EN source typo fix ("recieve" → "receive") | Issue follows the line |
| 3 | RU-only edit (EN unchanged) | Issue follows the line (trivial — EN identity unchanged) |
| 4 | Insert/delete before this line | Issue follows the line (position shifts but identity stays) |
| 5 | Split one line into two | Issue binds to ONE of the two (UI rebind) |
| 6 | Merge two lines into one | Issue binds to merged (UI rebind) |
| 7 | Reorder lines | Issue follows the line |
| 8 | Full EN source file replacement | Orphan + UI rebind or close |
| 9 | Reopen after app restart | Issue re-attaches to the same line |
| 10 | Multi-device rebind | Both devices agree on identity |

## Candidate formats

### (a) hash(EN text only) — `sha256(normalized_en)[:16]`
- Survives: retiming (1), RU-only edit (3), insert/delete (4), reorder (7), restart (9).
- Breaks: EN typo fix (2) — hash changes → orphan. Full replacement (8) — orphan (correct).
- Split (5): both halves have different EN from original → both orphan. UI must rebind.
- Merge (6): merged EN != either original → orphan. UI must rebind.
- Multi-device (10): deterministic, both devices agree.

### (b) hash(EN + timing) — `sha256(normalized_en + "|" + start_cs + "|" + end_cs)[:16]`
- Survives: RU-only edit (3), insert/delete (4), reorder (7), restart (9).
- Breaks: retiming (1) — **bad**, this is common. EN typo fix (2). Split/merge (5,6).
- This is the v1.0 placeholder. **Rejected** as primary identity.

### (c) positional index — line row number
- Survives: nothing stable. Insert/delete (4) shifts everything. Reorder (7) breaks.
- **Rejected.**

### (d) UUID in ASS extradata
- Survives everything (1-9) because the UUID travels with the line in the ASS file.
- Breaks the "opaque blob" server invariant (server would need to parse ASS to read extradata).
- Requires modifying production ASS (extradata is a Sanae/Aegisub extension; standard ASS
  players ignore it, but other tools may strip it).
- **Rejected for v0.3** — too invasive. Could be V0.4+ opt-in for teams that want bulletproof identity.

### (e) fuzzy: content hash + Levenshtein fallback + UI rebind
- Survives everything (1-9) via fuzzy matching when exact hash misses.
- Complex to implement, hard to make deterministic across devices.
- **Deferred to V0.4+** if (a) proves insufficient in practice.

### (f) hash(EN + context: prev/next EN)
- Survives single-point EN edits (2) if only one line changes — context still matches.
- Breaks on reorder (7) — context changes. Breaks on insert/delete (4) — neighbor changes.
- **Rejected** — too fragile for common operations.

## Chosen: interim identity = `hash(normalized_visible_en)[:16]` (option a)

**For local-only Phase 3** (client sidecar, no server sync):

```
line_ref = sha256(SanaeNormalizeSource(line.GetStrippedText()))[:16]
```

### Collision analysis (REQUIRED before use for local persistence)

**Problem:** identical EN source lines produce identical `line_ref`. Two lines
with the same visible text (common in dialogue: "Yes.", "No.", "Hmm.", repeated
phrases) would be indistinguishable. A ReviewIssue created on line 5 "Yes."
would attach to line 12 "Yes." as well — **incorrect**.

**Test scenarios:**

| # | Scenario | Expected | Result with hash(EN only) |
|---|---|---|---|
| C1 | Two identical EN lines | distinct refs | **FAIL** — same hash |
| C2 | Identical EN + identical timing | distinct refs | **FAIL** — same hash (timing not in hash) |
| C3 | Multiple duplicates (5× "Yes.") | 5 distinct refs | **FAIL** — all same |
| C4 | Reorder duplicates | refs follow lines | **FAIL** — can't distinguish |
| C5 | Insert another duplicate | new line gets unique ref | **FAIL** — collides with existing |
| C6 | Delete one duplicate | remaining lines keep their refs | **FAIL** — can't tell which was deleted |

**Conclusion:** `hash(EN only)` is **NOT suitable** for local ReviewIssue
persistence — collisions make it impossible to distinguish identical lines.

### Revised interim identity: `hash(EN + line position index)`

```
line_ref = sha256(SanaeNormalizeSource(visible_en_text) + "|" + std::to_string(line_index))[:16]
```

where `line_index` is the 0-based position of the line in `AssFile::Events`
(the `Row` field of `AssDialogue` is close but can be -1 for uninserted lines;
use the actual iterator position).

**Collision test results:**

| # | Scenario | Result |
|---|---|---|
| C1 | Two identical EN lines at different positions | **OK** — different index → different hash |
| C2 | Identical EN + identical timing, different positions | **OK** |
| C3 | Multiple duplicates at different positions | **OK** — 5 distinct refs |
| C4 | Reorder duplicates | refs follow lines (position changes → hash changes) — **ACCEPTABLE** (issue rebinds to new position) |
| C5 | Insert another duplicate at end | new line gets new index → unique ref — **OK** |
| C6 | Delete one duplicate | remaining lines' indices may shift if deleted above — **PARTIAL FAIL** (see below) |

**C6 analysis:** deleting line 3 of 10 shifts lines 4-10 down by one index.
Lines 4-10 would get new hashes → their issues become orphans. This is the
same problem as positional index (option c). **Acceptable for local-only**
because deletion is less common than retiming/RU-edit, and the orphan UI
prompts rebind.

### Final interim identity for local-only Phase 3

**NOT for persisted local ReviewIssue identity.** See forward gate below.

```
line_ref_v1_interim = sha256(
    SanaeNormalizeSource(line.GetStrippedText())
    + "|" + std::to_string(position_in_events)
)[:16]
```

- Unique per (text, position) — no collisions among identical lines.
- Survives retiming (position unchanged if only times change).
- Survives RU-only edits (EN text unchanged).
- **Breaks on insert/delete before this line (position shifts).**
- **Breaks on reorder (position changes).**
- Deterministic within a single ASS file version.

**NOT sent to production server.** Server `line_ref` contract remains PENDING.

## FORWARD GATE (Phase 3 prerequisite) — RESOLVED

### Final decision: local_line_id = sidecar-maintained monotonic UUID

**Chosen approach: Option 5 — sidecar-maintained stable local ID.**

After inspecting the real Aegisub source:

- `AssDialogue::Id` (ass_dialogue.h:123) is process-local (`static int next_id`,
  ass_dialogue.cpp:47). NOT persisted across save/reopen. Rejected.
- `AssFile::Extradata` (ass_file.h:51-55, 147-155) IS persisted in the `.ass`
  file under `[Aegisub Extradata]` section. Lines reference extradata by
  `ExtradataIds` (ass_dialogue.h:144). This is Aegisub-native and survives
  save/reopen. However, it modifies the production ASS file (adds extradata
  section). While standard ASS players ignore this section, it changes the file
  content. The plan says "does not modify visible ASS text" — extradata does
  NOT modify visible text, but it DOES add data to the file.
  **Decision: extradata is viable but deferred to V0.4** to avoid any ASS
  modification in V0.3. For V0.3 local-only, use the sidecar.
- TranslationProject sidecar (`<file>.aegisub.json`) already stores per-line
  state (status, history, source text, timing) keyed by `AssDialogue::Id`
  (process-local). On reload, `RebuildUnits` re-aligns units using
  `(target_start, target_end, source_text)` as a composite key
  (translation_project.cpp:265). This existing re-alignment mechanism can
  be extended for `local_line_id`.

### local_line_id design

**Storage:** in the per-file sidecar (`<file>.ass.aegisub.json`), alongside
existing TranslationProject units.

**Format:** a monotonically-increasing integer assigned at first-seen,
persisted in the sidecar. Format: `"li_000001"`, `"li_000002"`, etc.

**Mapping:** the sidecar maintains a mapping:
```
{
  "local_line_ids": {
    "li_000001": { "start": 1020, "end": 1140, "source_hash": "abc123..." },
    "li_000002": { "start": 1200, "end": 1350, "source_hash": "def456..." },
    ...
  },
  "next_local_line_id": 3
}
```

**Re-alignment on reload:** when the ASS file is reopened:
1. For each line in the new ASS, compute `(start, end, source_hash)`.
2. Match against the sidecar mapping.
3. If exact match: rebind the local_line_id to the new `AssDialogue::Id`.
4. If no match: the local_line_id is orphaned (UI prompts rebind or close).

**Stability properties:**

| Mutation | Survives? | Why |
|---|---|---|
| Retiming | NO (start/end changes) | source_hash still matches; re-align by source_hash |
| RU-only edit | YES (source unchanged) | start/end + source_hash all match |
| EN typo fix | NO (source_hash changes) | orphan; UI prompts rebind |
| Insert/delete before | YES (position shifts, but start/end + source_hash of THIS line unchanged) | re-align by composite key |
| Reorder | YES (position changes, but start/end + source_hash unchanged) | re-align by composite key |
| Split | PARTIAL (one half matches, other doesn't) | UI prompts rebind for the non-matching half |
| Merge | PARTIAL (merged line doesn't match either parent) | orphan; UI prompts rebind |
| Save/reopen | YES (sidecar persisted) | re-alignment runs on load |
| Duplicate EN lines | YES (different start/end → different local_line_id) | composite key includes timing |

**Migration/rebind path:**
- When a local_line_id is orphaned (no match on reload), the ReviewIssue
  remains in the sidecar with its `local_line_id` but is marked as "orphaned".
- The UI shows "Line not found. Rebind to current line? [Yes] [Close issue]".
- If user rebinds: the issue's `local_line_id` is updated to the new line's ID.
- If user closes: the issue is marked `resolved` with `resolution_note = "line not found"`.

**Does NOT modify visible ASS text.** The sidecar is a separate `.json` file.

**Does NOT depend on future server line_ref.** `local_line_id` and `line_ref`
are separate concepts. They MAY eventually use the same underlying identity
if Phase 6 adopts it, but that is a Phase 6 decision.

### Implementation plan (Phase 3)

1. Add `local_line_id` field to `SanaeReviewIssue` (string, e.g. `"li_000001"`).
2. Add `local_line_ids` mapping to the per-file sidecar JSON.
3. On ASS open: run re-alignment (like `TranslationProject::RebuildUnits`).
4. On ReviewIssue creation: assign next `local_line_id` from the sidecar counter.
5. On orphan detection: mark issue, prompt user in UI.

### line_ref (server wire identity) — remains PENDING

Do NOT enable `Sanae/ServerReviewSync` until:
1. `local_line_id` is implemented and stable (Phase 3).
2. Server `line_ref_v1` contract is approved by user.
3. Multi-device rebind UI is designed.
4. The mapping from `local_line_id` to `line_ref` is defined (if they differ).

## Server contract for Phase 6 (PENDING)

The server treats `line_ref` as an opaque string (`minLength: 1`, `maxLength: 256`).
The server does NOT parse or validate the internal structure.

**Before enabling `Sanae/ServerReviewSync`:**
1. Confirm the interim identity (a) works in real translator workflows (Phase 2-3 user feedback).
2. If orphan rate is acceptable (< 5% of issues orphaned per episode), freeze as `line_ref_v1`.
3. If orphan rate is too high, implement (e) fuzzy rebind or (d) opt-in extradata UUID.

**Multi-device rebind:** when device A creates an issue on `line_ref=X`, and device B has
a line with `line_ref=X`, the issue attaches automatically. When device B's line has a different
`line_ref` (EN was edited on B), the issue appears as orphan on B — UI prompts rebind or close.

## Test coverage (local-only, Phase 3)

Tests live in `tests/test_line_ref.cpp` (to be written when Phase 3 local persistence lands):

- retiming: same `line_ref` before/after time change ✓
- RU-only edit: same `line_ref` ✓
- EN typo fix: different `line_ref` (orphan) — documented behavior
- insert line before: same `line_ref` for the original line ✓
- split: both children have different `line_ref` from parent — UI rebind required
- merge: merged has different `line_ref` from either parent — UI rebind required
- restart: same `line_ref` for unchanged line ✓
- determinism: same EN text → same `line_ref` across runs ✓

## Conclusion

`line_ref_v1_interim = sha256(SanaeNormalizeSource(visible_en_text))[:16]`

- Used for local-only Phase 3.
- NOT sent to production server until contract is frozen.
- `Sanae/ServerReviewSync` flag stays OFF until freeze.
- Freeze decision deferred to after Phase 2-3 real-user feedback.
