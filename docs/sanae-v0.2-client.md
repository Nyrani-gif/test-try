# Sanae — Aegisub v0.2 (beta): client implementation map

This document describes the Windows client implementation. The deployed Sanae Server and its v0.2 OpenAPI snapshot are authoritative; this client does not add or substitute server endpoints.

## Existing Aegisub architecture used

- `agi::Context` owns one `Project`, `TranslationProject`, subtitle controller, selection controller, grid, and now one `SanaeProjectManager` per Aegisub window.
- `TranslationProject` remains the read-only source-track/alignment/review-sidecar subsystem. The optional Sanae project/episode/source/finalized-revision identity is stored in the existing `.aegisub.json` sidecar, never in the production ASS.
- `SubsController` and `SubtitleFormat` remain responsible for production subtitle lifetime and ASS reading/writing.
- `DialogTranslation` remains the translation workflow. Project mode only replaces the terminal “No more lines” result with Final Review.
- `BaseGrid` remains the subtitle grid. It asks the local project manager for a cached repeat result while painting; painting never performs network or project-memory searches.
- Existing preferences, command registration, JSON menus, gettext catalogues, and Meson targets are extended in place.

## New client modules

| Module | Responsibility |
| --- | --- |
| `sanae_api.*` | Thin libcurl transport for the exact v0.2 routes, JSON/multipart requests, standard error envelopes, idempotency headers, and the Windows Credential Manager device token. |
| `sanae_project.*` | Per-window project directory, snapshot/draft cache, full/incremental sync, immutable file downloads, source memory, repeat index, review analysis, and atomic Finalize orchestration. |
| `sanae_text.*` | Server-compatible NFKC, whitespace-collapse, case-fold normalization and normalized Levenshtein similarity. |
| `sanae_compact_rusub.*` | Constructs a server-safe ASS copy without drawings, attachments, extradata, empty events, or technical duplicate layers. |
| `sanae_batch_import.*` | Testable folder scanning, episode-code matching, resumable queue serialization, per-row idempotency keys, and Windows SHA-256 verification. |
| `dialog_sanae_project.*` | Enrollment, Seasons/Projects directory, Sync, Add Episode + ENSUB, attach/detach, cached offline access, and enrollment reset. |
| `dialog_sanae_batch_import.*` | Batch-import preview, mapping correction, preflight, sequential execution, retry/resume, progress, and report export. |
| `dialog_sanae_final_review.*` | Candidate, terminology, consistency, queued-term, repeat, ignore, and Finalize stages. |
| `command/sanae.cpp` | File-menu and grid-context commands. |

## Ownership and lifetime

`agi::Context::sanaeProject` is constructed after `TranslationProject` and destroyed before it. It subscribes to subtitle-open, ASS-commit, TranslationProject-change, and repeat-option signals. Ordinary windows with no Sanae binding retain standard Aegisub behavior; the manager does no automatic network work.

## Local cache

The cache is rooted below the existing Aegisub user directory:

```text
?user/sanae/
  directory.json
  projects/<project-uuid>/
    snapshot.json
    files/<immutable-file-uuid>.ass
    drafts/<episode-uuid>.json
    pending/<episode-uuid>.compact.ass
    imports/
      batch-import.json
      pending/<episode-uuid>.compact.ass
```

- `directory.json` retains Seasons/Projects for offline selection.
- `snapshot.json` is the last valid merged project snapshot and revision.
- Server files are immutable and keyed by server file UUID. Explicit Sync checks size and, on Windows, SHA-256 before reusing cached bytes; new downloads are checked against size, SHA-256, and ETag.
- Draft JSON contains only queued terminology/ignore operations and the pending Finalize idempotency key. Production subtitle text remains in the local ASS.
- `imports/batch-import.json` retains absolute source paths, corrected episode mappings, inclusion flags, server episode IDs, row results, and the create/finalize idempotency keys. A row left in `running` state by interruption is restored as a retryable failure.
- Writes use Aegisub's existing `agi::io::Save` path.

## Networking and sync state

The transport uses only these contract routes:

- `POST /api/v1/auth/enroll`
- `GET /api/v1/seasons`
- `GET /api/v1/projects`
- `POST /api/v1/projects/{project_id}/sync`
- `POST /api/v1/projects/{project_id}/episodes`
- `GET /api/v1/files/{file_id}`
- `POST /api/v1/episodes/{episode_id}/finalize`

Bearer tokens are read from the Generic Credential target `Aegisub Sanae/Device Token`. HTTPS is required except for explicit localhost development URLs. Redirects are disabled. Enrollment and ordinary episode creation retain their idempotency keys for an in-process retry of the same request. Finalize persists its key so a response-loss retry remains safe across application restarts. Batch import persists separate create and Finalize UUID keys for every row before issuing either request.

Sync starts at the cached `current_revision`, merges entities by server UUID, and falls back to `since_revision: 0` only for the contract's `invalid_since_revision` response. A full snapshot replaces the entity arrays. No sync is triggered by editing, playback, selection, or painting.

## Project memory and source repeats

Only previous finalized episodes are indexed. ENSUB visible text is the source of truth; compact RUSUB is timing-aligned only to display the prior translation. Exact matches use normalized ENSUB text. Similar matching uses a configurable high threshold (default `0.92`) and a first-byte candidate bucket before Levenshtein evaluation.

Results are cached per current ASS line ID. The grid uses two configurable pale background colors and preserves selection/comment priority. The context menu can show the matched ENSUB/RUSUB or search the already synchronized memory. It never substitutes text.

## Batch import

`Batch Import…` is a separate action in the existing Sanae Project dialog; it does not create a workspace or replace the translation workflow. The wizard accepts an ENSUB folder, a RUSUB folder, or both, and scans only ASS/SSA files in those folders. Common `SxxExx`, `Episode xx`, `Ep xx`, and numeric filename forms are recognized. Numeric codes such as `01` and `1` compare equal.

The preview has one row per episode and shows the matched files, cached server state, proposed action, and result. Duplicate filename matches, duplicate row codes, ambiguous server matches, missing ENSUBs for new episodes, invalid files, and the episode attached to the current ASS are blocked before execution. The user can correct the episode code or replace either file and can include or skip each row.

Execution is deliberately sequential because each successful mutation advances the project revision used by the next Finalize. New episodes are created through the existing episode route without attaching them to the current ASS. A supplied ENSUB for an existing server episode must have the same SHA-256 as its current source; a different source is blocked because the v0.2 contract has no source-replacement route. Existing finalized episodes are protected by default, with an explicit option to create a later finalized revision.

Historical RUSUB files use the same compact constructor and Finalize route as interactive Final Review, with empty terminology and ignore operation arrays. The production ASS, TranslationProject binding, selection, and source track are not changed. After the queue finishes, the project snapshot, memory index, and repeat cache are rebuilt once. A text report can be saved from the completed queue.

## Terminology and Final Review

Candidate scoring combines occurrences, capitalization, prior-episode presence, phrases, and absence from the active local English dictionary. Existing terminology and ignores are excluded using server-normalized values.

Final Review appears in this order:

1. terminology candidates;
2. project terminology consistency;
3. within-episode consistency;
4. manually queued/accepted terms;
5. source-repeat review;
6. queued ignores;
7. Finalize.

All additions and ignores require an explicit user action. Accepted candidate terms also participate in the terminology consistency pass. Double-clicking an issue selects its production ASS line.

## Compact RUSUB and Finalize

Compact construction copies valid script/style structure while removing Fonts/Graphics attachments, project properties, extradata, comment events, drawing-mode events, and empty visible events. Output event text contains visible text only. Overlapping technical layers are collapsed when visible text, actor/effect/margins match and their timing overlaps by at least 90%; the first layer's valid ASS metadata is kept, with the earliest Start and latest End. Sequential semantic repetitions are retained.

Finalize sends multipart fields named `metadata` and `compact_rusub`. Metadata contains the sidecar's base project/source/finalized-revision values and explicit create operations for queued terminology and ignores. The client enforces the 1 MiB metadata, 16 MiB compact ASS, and 1000 terminology-operation limits before sending. On success it merges returned entities, updates the sidecar base revision, stores the immutable compact file, clears drafts, and rebuilds memory. On failure it preserves the production ASS, compact pending file, drafts, and idempotency key.

## Settings and failure behavior

- Server base URL.
- Source-repeat highlighting on/off.
- Similar-match threshold from `0.80` to `1.00`.
- Exact/similar grid colors.
- Batch-queue options: protect already finalized episodes (on by default), continue after a row error, and Sync when finished. These are stored with the resumable queue rather than added as global editor preferences.

Network failure never discards the last valid cache. Cached project/episode selection, source tracks, terminology, and memory remain usable. The batch wizard can scan and preflight against the last snapshot when its opening Sync fails; uploads fail per row and remain queued with their idempotency keys for retry. Completed rows are not repeated. Authentication can be reset from the project dialog. A corrupt sidecar cannot prevent an ASS from opening; its optional Sanae binding is discarded with the other invalid sidecar state.

## Verification targets

- `meson setup build --native-file build-aux/windows-msvc.ini ...`
- `meson compile -C build`
- `meson test -C build --print-errorlogs`
- `msgfmt --check-format --check-header po/ru.po`
- `tests/tests/sanae_batch_import.cpp` covers episode extraction, numeric canonicalization, ENSUB/RUSUB pairing, duplicate detection, resumable queue state, and idempotency-key shape.
- `tests/tests/sanae_text.cpp` covers normalization and similarity behavior.

The repository's Windows beta workflow already runs the MSVC/Meson/Ninja build, test suite, installer build, portable build, and Russian catalogue check.
