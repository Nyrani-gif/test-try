# Sanae Aegisub v0.4 — buildfix5 inline terminology wiring

This patch completes the previously stubbed inline terminology controls on top of
`aegisubsanae-v0.4-buildfix4-chatgpt.zip`.

## Implemented

- `Alt+1..5` applies terminology suggestions 1..5 to `SubsTextEditCtrl`.
- `Ctrl+T` opens `TerminologyEntryPopover` for the active line, prefilled from EN/RU context.
- `Ctrl+I` ignores the top-ranked terminology match for the current episode.
- Each inline term row has working **Apply** and **Ignore** buttons.
- Ignore state is filtered from later inline searches using existing ignore drafts/snapshot entries.
- New `Sanae Terminology` hotkey context resolves the legacy `Alt+1..4` color conflict:
  - Translation/QC + revamp enabled: `Alt+1..5` = terminology.
  - Advanced mode: original `Alt+1..4` color shortcuts remain active.
- Added a hotkey migration (`sanae/terminology/v1`) so existing user `hotkey.json` files receive the new defaults once.

## Required flags

For the mode-conditional Alt+1..5 routing:

- `Sanae/InlineTerminology = true`
- `Sanae/WorkspaceModes = true`

These are in `src/libresrc/default_config.json` for defaults and in the user's runtime
`config.json` after the application has been launched.

## Smoke test

1. Start in Translation mode with an episode that has terminology matches.
2. Focus the RU subtitle edit box.
3. Press `Alt+1`; suggestion 1 should be inserted/replaced through the normal STC modified/commit path.
4. Press `Ctrl+T`; the add-term popover should appear with EN/RU prefill.
5. Press `Ctrl+I`; the first suggestion should disappear and stay suppressed after changing lines and returning.
6. Switch to Advanced mode and verify `Alt+1..4` still invoke the legacy color commands.
7. Open Preferences → Hotkeys and verify the `Sanae Terminology` context exists.

## Verification in this environment

- `default_hotkey.json` parses successfully as JSON.
- `default_config.json` parses successfully as JSON.
- Full wxWidgets/Windows compilation was not possible in this container because the wxWidgets build headers/toolchain are not installed here. Rebuild and GUI smoke-test on the GLM 5.2 environment used for your Windows build.
