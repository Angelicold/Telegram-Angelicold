# pintopfp Integration — Design

**Date:** 2026-09-20
**Status:** Approved (design), pending implementation plan
**Scope:** Feature 1 of 4 requested by the user. This spec covers **only**
the pintopfp integration. Live wallpapers, Liquid Glass, and the
auto-update pipeline are separate future specs.

## Summary

Embed the user's existing `pintopfp` program (a Python/Tkinter tool that
watches a Pinterest board and sets each new pin as a Telegram profile
photo) into the Telegram Desktop fork as a **native Qt panel**, reachable
from an icon in the left folders side-bar.

The chosen approach is **hybrid**: the UI is rewritten natively in
C++/Qt so it looks like part of Telegram, while the proven Pinterest →
avatar logic keeps running as the existing Python code, launched as a
background process. This preserves the working logic (which already
targets the separate `pinterest_avatar` account) and avoids
reimplementing fragile Pinterest scraping and a second MTProto session
in C++.

## Background & Constraints

- **pintopfp today** (`C:\Users\angelicold\Desktop\pintopfp\main.py`): a
  Tkinter GUI with a `BotWorker` that logs into Pinterest (`py3pin`),
  polls a board, downloads new pins, and sets them as the Telegram
  avatar via a Telethon userbot session (`pinterest_avatar.session`).
  Config lives in `.env` (`API_ID`, `API_HASH`, `SESSION_NAME`,
  Pinterest credentials, `BOARD_ID`, `POLL_INTERVAL`). A `.venv` with
  `telethon`, `py3pin`, `python-dotenv` already exists in that folder.
- **Target account is separate** from the desktop client's account, so
  the desktop client's own MTProto session cannot be reused — the Python
  Telethon session is what authenticates the avatar changes.
- **Telegram avatar rate limits are small and dynamic.** An empirical
  probe (`_avatar_limit_probe.py`) hit `FloodWaitError` immediately with
  a **1941s (~32 min)** wait, because the account was already throttled
  from prior activity. Conclusion: there is **no fixed public quota** and
  no API to query remaining quota; the limit is small and server-driven.
  The design must therefore be **adaptive** (react to the actual
  `FloodWait` the server returns), with a user-configurable *soft cap* as
  a guard. The exact clean threshold will be measured later by the user
  re-running the probe once the account is no longer throttled.

## Requirements

1. An icon in the left folders side-bar (`FiltersMenu`), positioned
   **below the folders list** and above the folder-settings button.
2. The icon appears **only when the folders side-bar is shown**. No
   side-bar → no icon (explicit user decision).
3. Clicking the icon opens a **native Qt panel** styled like Telegram
   (respects light/dark theme).
4. The panel reproduces the Tkinter feature set: run status, Start /
   Stop, "run at Windows startup" toggle, live log, clear-log.
5. The panel shows a **stats block**: avatars set in the last 24h and
   last 4h, and a pause indicator with remaining seconds when the server
   returns a `FloodWait`.
6. The panel provides an **editable settings form** that writes back to
   `.env` (or a sidecar config): board poll interval (default 180s),
   `BOARD_ID`, soft limits (per-24h and per-4h), and the API / Pinterest
   credentials (masked inputs). Secrets stay local and are never
   committed to git.
7. The Python program keeps working standalone (GUI mode unchanged); a
   new headless mode is added, not a replacement.
8. All C++ code is isolated in `Telegram/SourceFiles/pintopfp/` with the
   fewest possible insertion points in existing files, to minimize merge
   conflicts against future upstream updates (feature 3).

## Architecture

Three units with clear boundaries:

### Unit A — Python headless backend (`main.py --headless`)

- **What it does:** runs the existing `BotWorker` loop without Tkinter,
  emits structured lines to stdout, and reads simple commands from
  stdin.
- **How it's used:** launched by Unit C via `QProcess`; process lifetime
  == running state (start = spawn, stop = terminate).
- **Depends on:** the existing `.venv`, `.env`, and
  `pinterest_avatar.session` in the pintopfp folder.

Additions to `main.py` (backward compatible — default is GUI):

- `--headless` flag: instantiate `BotWorker`, run the loop, print each
  log line to stdout as-is.
- Structured status lines alongside human logs, one per line, prefixed
  so the C++ side can parse them unambiguously:
  - `@STATUS running` / `@STATUS stopped`
  - `@STATS set24h=<n> set4h=<n> softLeft24h=<n> softLeft4h=<n>`
  - `@FLOOD seconds=<n>` when a `FloodWaitError` is caught; the worker
    then sleeps for that many seconds and resumes.
  - Plain lines (no `@` prefix) are treated as human log text.
- **Avatar history:** maintain `avatar_history.json` (list of Unix
  timestamps of successful avatar changes). On each poll, compute rolling
  24h / 4h counts. Before setting an avatar, if a soft cap is configured
  and the corresponding window is full, skip and log instead of setting
  (prevents tripping the server flood).
- **FloodWait handling:** wrap `set_avatar` in `try/except
  FloodWaitError`; emit `@FLOOD seconds=e.seconds`, sleep, continue.
- Config values (`POLL_INTERVAL`, `BOARD_ID`, soft caps) are read from
  `.env` at startup; soft caps are new keys `SOFT_LIMIT_24H`,
  `SOFT_LIMIT_4H` (absent = no cap).

### Unit B — the side-bar icon (insertion point)

- **What it does:** adds one `Ui::SideBarButton` to the `FiltersMenu`
  container, below the folders list, that opens Unit C on click.
- **How it's used:** constructed inside `FiltersMenu::setup()` /
  `refresh()`, kept as a member so it lives and dies with the side-bar.
- **Depends on:** `Window::SessionController` (to open the panel), a new
  strip icon asset.
- **Insertion points (minimal):**
  - `window/window_filters_menu.h`: one member field.
  - `window/window_filters_menu.cpp`: construct the button and place it
    in `_container` after `_list`; a click callback that opens Unit C.
  - A new icon in `Telegram/Resources/icons/` + style entry.

### Unit C — the native panel (`Telegram/SourceFiles/pintopfp/`)

- **What it does:** owns the QProcess, renders the UI, parses backend
  output, and edits config.
- **How it's used:** created by Unit B; shown as a panel/inner section in
  the Telegram window.
- **Depends on:** `base::Platform` process APIs / `QProcess`, Telegram UI
  widgets (`Ui::FlatButton`, `Ui::Checkbox`, `Ui::ScrollArea`,
  `Ui::InputField`), and the style system.

Proposed files:

- `pintopfp/pintopfp_process.h/.cpp` — wraps `QProcess`: locate the
  Python interpreter + `main.py`, spawn with `--headless`, expose
  `start()`, `stop()`, and rpl streams for log lines / status / stats /
  flood. Parses the `@`-prefixed protocol.
- `pintopfp/pintopfp_config.h/.cpp` — read/write the `.env` (and the new
  soft-limit keys); model the editable fields; masking handled in the UI.
- `pintopfp/pintopfp_panel.h/.cpp` — the widget: status row, Start/Stop,
  autostart checkbox, stats block, log view, clear-log, settings form.
- `pintopfp/pintopfp_paths.h/.cpp` — resolve the pintopfp folder,
  interpreter (`.venv/Scripts/pythonw.exe`), and `.env` path; a
  configurable override stored in Telegram settings, defaulting to
  `C:\Users\angelicold\Desktop\pintopfp`.

### Data flow

```
Pinterest ──> Unit A (python --headless) ──stdout──> Unit C (QProcess reader)
                    │                                      │
              avatar_history.json                    parse @STATUS/@STATS/@FLOOD
                    │                                      │
              Telethon (pinterest_avatar) <── stdin (start/stop)   render UI
```

## Error handling

- **Python not found / venv missing:** the panel shows an actionable
  error with the resolved path and a button to open settings to fix the
  path; Start is disabled until resolvable.
- **Backend exits unexpectedly:** panel returns to "stopped", surfaces
  the exit code and the last stderr lines in the log.
- **FloodWait:** shown as a pause indicator with a live countdown; the
  backend resumes automatically.
- **Malformed `.env` on save:** validate before writing; on failure keep
  the old file and show the error (mirrors the Tkinter autostart error
  pattern).
- **Autostart toggle failure:** surface the error and revert the toggle
  (same behavior as the current Tkinter code).

## Testing

- **Python backend:** unit-test the rolling-window counting and soft-cap
  skip logic against `avatar_history.json` fixtures (pure functions, no
  network). Manually verify `--headless` emits the protocol lines and
  that GUI mode still launches unchanged.
- **Protocol parser (C++):** unit-test parsing of `@STATUS`, `@STATS`,
  `@FLOOD`, and plain log lines, including malformed input.
- **Config read/write (C++):** round-trip an `.env` fixture; verify
  secrets are preserved and only intended keys change.
- **Manual integration:** icon appears only with the folders side-bar;
  clicking opens the panel; Start spawns the process and the log streams;
  Stop terminates it; the stats block updates; a simulated `@FLOOD` line
  shows the pause countdown.

## Out of scope (explicit)

- Bundling a Python runtime into the installer — v1 points at the
  existing local `.venv`.
- Reimplementing Pinterest scraping or a second MTProto session in C++.
- The other three requested features (live wallpapers, Liquid Glass,
  auto-update pipeline).

## Open items to resolve during implementation

- Exact soft-cap default: filled in after the user re-runs
  `_avatar_limit_probe.py` on a rested account.
- Confirm the panel presentation form (inner section vs. floating panel)
  against how the folders side-bar hosts child sections in this codebase.
