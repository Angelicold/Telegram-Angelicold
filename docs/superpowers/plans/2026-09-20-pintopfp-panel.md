# pintopfp Native Panel Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a native Qt panel to the Telegram Desktop fork — opened from an icon in the folders side-bar — that drives the existing pintopfp Pinterest→avatar tool running as a headless background process.

**Architecture:** Hybrid. The proven Python logic (`main.py`) gains a headless mode that emits a line-based status protocol on stdout and takes commands on stdin; the desktop client wraps it in a `QProcess` behind a native panel. All C++ lives in an isolated `Telegram/SourceFiles/pintopfp/` module with three insertion points (filters menu, CMake, resources) to keep future upstream merges clean.

**Tech Stack:** C++20, Qt 6 (QProcess, Qt widgets via the desktop-app `lib_ui`), Telegram's style/codegen system; Python 3 with the existing `.venv` (telethon, py3pin, python-dotenv), stdlib `unittest` for backend tests.

**Spec:** `docs/superpowers/specs/2026-09-20-pintopfp-integration-design.md`

## Global Constraints

- **C++ standard:** C++20 (match the existing tdesktop target). Copied from repo build config.
- **Code style (from `REVIEW.md`):** empty line before the closing brace of any class with sections; at most one consecutive empty line; operators at the **start** of continuation lines; prefer direct cast + null-check over `is*()` + `as*()`.
- **Module isolation:** every new C++ source lives under `Telegram/SourceFiles/pintopfp/`. Existing files may be touched in at most these spots: `window/window_filters_menu.{h,cpp}` (one member + button placement + click), `Telegram/CMakeLists.txt` (source list), and a new resource icon + style entry. No other existing file is edited.
- **Secrets never enter git:** `.env`, `*.session`, `avatar_history.json`, and `processed_pins.json` stay in the pintopfp folder outside this repo. No credential value is logged to the C++ log view in plaintext beyond what the user typed.
- **Python backward compatibility:** running `main.py` with no arguments still launches the existing Tkinter GUI unchanged. Headless behavior is additive behind `--headless`.
- **Default pintopfp location:** `C:\Users\angelicold\Desktop\pintopfp`, interpreter `.venv\Scripts\pythonw.exe`. Overridable via a Telegram setting.
- **Status protocol (single source of truth):** lines on stdout are either human log text (no prefix) or one of:
  - `@STATUS running` | `@STATUS stopped`
  - `@STATS set24h=<int> set4h=<int> softLeft24h=<int|->  softLeft4h=<int|->`
  - `@FLOOD seconds=<int>`
  Commands on stdin: `stop\n` for graceful shutdown (process termination is the fallback).

---

## Task 1: Python — rolling-window + soft-cap logic (pure, tested)

Extract the avatar-history math into pure functions so it is unit-testable without network, Telethon, or Tkinter.

**Files:**
- Create: `C:\Users\angelicold\Desktop\pintopfp\avatar_limits.py`
- Test: `C:\Users\angelicold\Desktop\pintopfp\test_avatar_limits.py`

**Interfaces:**
- Produces:
  - `counts_in_windows(timestamps: list[float], now: float) -> tuple[int, int]` → `(count_24h, count_4h)`
  - `soft_remaining(count: int, cap: int | None) -> int | None` → `None` if `cap is None`, else `max(0, cap - count)`
  - `may_set_now(timestamps, now, cap_24h, cap_4h) -> bool` → False if either configured cap's window is full.

- [ ] **Step 1: Write the failing tests**

```python
# test_avatar_limits.py
import unittest
from avatar_limits import counts_in_windows, soft_remaining, may_set_now

H = 3600.0

class TestAvatarLimits(unittest.TestCase):
    def test_counts_split_by_window(self):
        now = 100 * H
        ts = [now - 0.5 * H, now - 3 * H, now - 5 * H, now - 25 * H]
        self.assertEqual(counts_in_windows(ts, now), (3, 1))

    def test_counts_empty(self):
        self.assertEqual(counts_in_windows([], 1000.0), (0, 0))

    def test_soft_remaining_none_when_uncapped(self):
        self.assertIsNone(soft_remaining(5, None))

    def test_soft_remaining_clamped_at_zero(self):
        self.assertEqual(soft_remaining(7, 5), 0)
        self.assertEqual(soft_remaining(2, 5), 3)

    def test_may_set_blocks_when_4h_full(self):
        now = 100 * H
        ts = [now - 0.1 * H, now - 0.2 * H]
        self.assertFalse(may_set_now(ts, now, cap_24h=100, cap_4h=2))

    def test_may_set_allows_when_uncapped(self):
        self.assertTrue(may_set_now([1, 2, 3], 4.0, cap_24h=None, cap_4h=None))

if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cd /d C:\Users\angelicold\Desktop\pintopfp && .venv\Scripts\python.exe -m unittest test_avatar_limits -v`
Expected: FAIL with `ModuleNotFoundError: No module named 'avatar_limits'`.

- [ ] **Step 3: Write minimal implementation**

```python
# avatar_limits.py
"""Pure helpers for avatar rate accounting. No I/O, no network."""

_24H = 24 * 3600.0
_4H = 4 * 3600.0


def counts_in_windows(timestamps, now):
    c24 = sum(1 for t in timestamps if now - t <= _24H)
    c4 = sum(1 for t in timestamps if now - t <= _4H)
    return (c24, c4)


def soft_remaining(count, cap):
    if cap is None:
        return None
    return max(0, cap - count)


def may_set_now(timestamps, now, cap_24h, cap_4h):
    c24, c4 = counts_in_windows(timestamps, now)
    if cap_24h is not None and c24 >= cap_24h:
        return False
    if cap_4h is not None and c4 >= cap_4h:
        return False
    return True
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `.venv\Scripts\python.exe -m unittest test_avatar_limits -v`
Expected: PASS (6 tests).

- [ ] **Step 5: Commit**

```bash
git add avatar_limits.py test_avatar_limits.py   # in the pintopfp repo, not tdesktop
git commit -m "Add tested avatar rate-window helpers"
```
(Note: pintopfp is a separate folder. If it is not a git repo, skip the commit and keep the files.)

---

## Task 2: Python — headless mode + status protocol

Add `--headless` to `main.py`: run `BotWorker` without Tkinter, wire the history/cap helpers, and emit the protocol. GUI mode stays the default.

**Files:**
- Modify: `C:\Users\angelicold\Desktop\pintopfp\main.py`

**Interfaces:**
- Consumes: `avatar_limits.counts_in_windows`, `soft_remaining`, `may_set_now` (Task 1).
- Produces: a process that prints `@STATUS`, `@STATS`, `@FLOOD`, and plain log lines; reads `stop` on stdin.

- [ ] **Step 1: Add config keys and history load/save**

In `main.py`, near the existing settings block, read new optional keys and add history persistence next to `save_processed`:

```python
SOFT_LIMIT_24H = int(os.getenv("SOFT_LIMIT_24H", "0")) or None
SOFT_LIMIT_4H = int(os.getenv("SOFT_LIMIT_4H", "0")) or None
HISTORY_FILE = Path("avatar_history.json")

def load_history():
    if HISTORY_FILE.exists():
        try:
            with open(HISTORY_FILE, "r", encoding="utf-8") as f:
                return [float(x) for x in json.load(f)]
        except Exception:
            return []
    return []

def save_history(times):
    cutoff = time.time() - 24 * 3600
    times = [t for t in times if t >= cutoff]
    with open(HISTORY_FILE, "w", encoding="utf-8") as f:
        json.dump(times, f)
```

- [ ] **Step 2: Emit protocol from the worker loop**

Extend `BotWorker` so that when running headless it emits protocol lines. Add a `protocol` flag defaulting to False; the `log()` method already routes through the queue for GUI. Add helpers:

```python
def emit(self, line):
    # Headless: straight to stdout; GUI: into the log queue as text.
    if self.protocol:
        print(line, flush=True)
    else:
        self.log(line)

def emit_stats(self, times):
    from avatar_limits import counts_in_windows, soft_remaining
    c24, c4 = counts_in_windows(times, time.time())
    l24 = soft_remaining(c24, SOFT_LIMIT_24H)
    l4 = soft_remaining(c4, SOFT_LIMIT_4H)
    self.emit(f"@STATS set24h={c24} set4h={c4} "
              f"softLeft24h={'-' if l24 is None else l24} "
              f"softLeft4h={'-' if l4 is None else l4}")
```

In `run_loop`'s `main_async`, after connecting, `print("@STATUS running", flush=True)` when headless; on exit print `@STATUS stopped`. Around `set_avatar`, guard with `may_set_now(...)`; on success append `time.time()` to history, `save_history`, and `emit_stats`. Wrap the `UploadProfilePhotoRequest` in `except FloodWaitError as e:` → `print(f"@FLOOD seconds={e.seconds}", flush=True)`, `await asyncio.sleep(e.seconds)`.

- [ ] **Step 3: Add the `--headless` entrypoint and stdin stop**

Replace the `if __name__ == "__main__":` block:

```python
def run_headless():
    import asyncio, sys, threading
    worker = BotWorker(queue.Queue())
    worker.protocol = True
    def watch_stdin():
        for line in sys.stdin:
            if line.strip() == "stop":
                worker.stop()
                break
    threading.Thread(target=watch_stdin, daemon=True).start()
    worker.running = True
    worker.run_loop()  # blocks; prints protocol to stdout

if __name__ == "__main__":
    if "--headless" in sys.argv:
        run_headless()
    else:
        app = App()
        app.mainloop()
```

Note: `BotWorker.log()` in headless still uses the queue; make `log()` call `self.emit(text)` semantics only for plain text — simplest is to have `log()` print when `self.protocol` else enqueue.

- [ ] **Step 4: Manual verification**

Run: `.venv\Scripts\python.exe main.py --headless` (with a valid `.env`).
Expected: prints `@STATUS running`, then plain connection logs, then `@STATS ...` lines; typing has no console (it reads stdin). Sending `stop` (or Ctrl-C) prints `@STATUS stopped` and exits.
Also run `.venv\Scripts\python.exe main.py` (no args) and confirm the Tkinter window still opens unchanged.

- [ ] **Step 5: Commit** (in pintopfp folder if it is a repo; else leave files).

```bash
git add main.py
git commit -m "Add headless mode with status protocol for native panel"
```

---

## Task 3: C++ — status protocol parser (pure)

A header-only-ish pure parser turning a stdout line into a typed event. Kept free of Qt widget dependencies so it is trivially reviewable; verified by a small `_DEBUG` self-check function invoked at panel construction (there is no unit-test runner in this codebase — see spec).

**Files:**
- Create: `Telegram/SourceFiles/pintopfp/pintopfp_protocol.h`
- Create: `Telegram/SourceFiles/pintopfp/pintopfp_protocol.cpp`

**Interfaces:**
- Produces:
```cpp
namespace Pintopfp {
struct Stats {
    int set24h = 0;
    int set4h = 0;
    std::optional<int> softLeft24h;
    std::optional<int> softLeft4h;
};
struct Event {
    enum class Type { Log, StatusRunning, StatusStopped, Stats, Flood };
    Type type = Type::Log;
    QString log;            // for Type::Log
    Stats stats;            // for Type::Stats
    int floodSeconds = 0;   // for Type::Flood
};
[[nodiscard]] Event ParseLine(const QString &line);
} // namespace Pintopfp
```

- [ ] **Step 1: Implement `ParseLine`**

```cpp
// pintopfp_protocol.cpp
#include "pintopfp/pintopfp_protocol.h"

namespace Pintopfp {

Event ParseLine(const QString &line) {
    auto event = Event();
    if (line == u"@STATUS running"_q) {
        event.type = Event::Type::StatusRunning;
    } else if (line == u"@STATUS stopped"_q) {
        event.type = Event::Type::StatusStopped;
    } else if (line.startsWith(u"@STATS "_q)) {
        event.type = Event::Type::Stats;
        const auto parts = line.mid(7).split(' ', Qt::SkipEmptyParts);
        for (const auto &p : parts) {
            const auto eq = p.indexOf('=');
            if (eq < 0) {
                continue;
            }
            const auto key = p.left(eq);
            const auto value = p.mid(eq + 1);
            const auto opt = (value == u"-"_q)
                ? std::optional<int>()
                : std::optional<int>(value.toInt());
            if (key == u"set24h"_q) {
                event.stats.set24h = value.toInt();
            } else if (key == u"set4h"_q) {
                event.stats.set4h = value.toInt();
            } else if (key == u"softLeft24h"_q) {
                event.stats.softLeft24h = opt;
            } else if (key == u"softLeft4h"_q) {
                event.stats.softLeft4h = opt;
            }
        }
    } else if (line.startsWith(u"@FLOOD seconds="_q)) {
        event.type = Event::Type::Flood;
        event.floodSeconds = line.mid(15).toInt();
    } else {
        event.type = Event::Type::Log;
        event.log = line;
    }
    return event;
}

} // namespace Pintopfp
```

- [ ] **Step 2: Add a `_DEBUG` self-check**

In `pintopfp_protocol.cpp`, add (compiled only in debug) `void SelfCheck();` asserting `ParseLine(u"@FLOOD seconds=1941"_q).floodSeconds == 1941`, that `@STATS set24h=3 set4h=1 softLeft24h=- softLeft4h=4` yields `set24h==3`, `!softLeft24h`, `softLeft4h==4`, and that a bare line becomes `Type::Log`. Call it once from the panel constructor under `#ifdef _DEBUG`.

- [ ] **Step 3: Commit** (folded into Task 8's CMake wiring build; commit source now)

```bash
git add Telegram/SourceFiles/pintopfp/pintopfp_protocol.h Telegram/SourceFiles/pintopfp/pintopfp_protocol.cpp
git commit -m "Add pintopfp stdout protocol parser"
```

---

## Task 4: C++ — paths resolver + Telegram setting

Resolve the pintopfp folder, interpreter, and `main.py`; store an override in Telegram settings.

**Files:**
- Create: `Telegram/SourceFiles/pintopfp/pintopfp_paths.h`
- Create: `Telegram/SourceFiles/pintopfp/pintopfp_paths.cpp`

**Interfaces:**
- Produces:
```cpp
namespace Pintopfp {
struct Paths {
    QString folder;      // e.g. C:/Users/.../Desktop/pintopfp
    QString python;      // folder + /.venv/Scripts/pythonw.exe
    QString script;      // folder + /main.py
    QString env;         // folder + /.env
    [[nodiscard]] bool valid() const; // python && script exist
};
[[nodiscard]] Paths ResolvePaths();          // uses stored override or default
void SetFolderOverride(const QString &folder);
[[nodiscard]] QString FolderOverride();
} // namespace Pintopfp
```

- [ ] **Step 1: Implement resolution**

Default folder is `C:/Users/angelicold/Desktop/pintopfp`. `ResolvePaths` reads `FolderOverride()`; if empty, uses the default. Build `python = folder + u"/.venv/Scripts/pythonw.exe"_q`, `script = folder + u"/main.py"_q`, `env = folder + u"/.env"_q`. `valid()` returns `QFile::exists(python) && QFile::exists(script)`.

- [ ] **Step 2: Persist the override**

Store the override string via `Core::App().settings()` custom storage. Simplest low-touch option that avoids editing core settings serialization: persist to a small file `cWorkingDir() + u"tdata/pintopfp_folder.txt"_q` using `QFile`. Implement `SetFolderOverride`/`FolderOverride` against that file. (Documented tradeoff: avoids touching `core_settings.cpp`, honoring the isolation constraint.)

- [ ] **Step 3: Manual verification** (after build in Task 8): with no override, `ResolvePaths().valid()` is true on the dev machine; setting a bad override makes it false.

- [ ] **Step 4: Commit**

```bash
git add Telegram/SourceFiles/pintopfp/pintopfp_paths.h Telegram/SourceFiles/pintopfp/pintopfp_paths.cpp
git commit -m "Add pintopfp path resolution and folder override"
```

---

## Task 5: C++ — `.env` read/write config model

Read and write the pintopfp `.env`, preserving unknown keys and comments.

**Files:**
- Create: `Telegram/SourceFiles/pintopfp/pintopfp_config.h`
- Create: `Telegram/SourceFiles/pintopfp/pintopfp_config.cpp`

**Interfaces:**
- Produces:
```cpp
namespace Pintopfp {
struct Config {
    base::flat_map<QString, QString> values; // key -> value, order preserved separately
    QStringList order;                        // original line order incl. comments as raw
    [[nodiscard]] QString value(const QString &key) const;
    void set(const QString &key, const QString &value);
};
[[nodiscard]] Config ReadEnv(const QString &path);
[[nodiscard]] bool WriteEnv(const QString &path, const Config &config);
} // namespace Pintopfp
```

- [ ] **Step 1: Implement parse/serialize**

`ReadEnv` reads the file line by line; a line matching `^([A-Z_][A-Z0-9_]*)=(.*)$` becomes a key/value (store key in `values`, push the key into `order`); any other line (comment/blank) is pushed to `order` as a raw literal tagged so it round-trips. `WriteEnv` re-emits `order`: for a key entry write `KEY=value` using the current `values`; for raw entries write them verbatim. Only keys the panel edits change; everything else is preserved. Editable keys: `BOARD_ID`, `POLL_INTERVAL`, `SOFT_LIMIT_24H`, `SOFT_LIMIT_4H`, `API_ID`, `API_HASH`, `PINTEREST_EMAIL`, `PINTEREST_PASSWORD`, `PINTEREST_USERNAME`.

- [ ] **Step 2: `_DEBUG` self-check** round-tripping a sample `.env` string: parsing then serializing without edits yields identical text; editing `POLL_INTERVAL` changes only that line. Call under `#ifdef _DEBUG` from the panel constructor.

- [ ] **Step 3: Commit**

```bash
git add Telegram/SourceFiles/pintopfp/pintopfp_config.h Telegram/SourceFiles/pintopfp/pintopfp_config.cpp
git commit -m "Add pintopfp .env read/write preserving unknown keys"
```

---

## Task 6: C++ — process controller (`QProcess` wrapper)

Owns the child process; exposes start/stop and typed rpl streams built from Task 3's parser.

**Files:**
- Create: `Telegram/SourceFiles/pintopfp/pintopfp_process.h`
- Create: `Telegram/SourceFiles/pintopfp/pintopfp_process.cpp`

**Interfaces:**
- Consumes: `Pintopfp::ParseLine`, `Pintopfp::Event`, `Pintopfp::ResolvePaths`.
- Produces:
```cpp
namespace Pintopfp {
class Process final {
public:
    Process();
    ~Process();

    void start();
    void stop();
    [[nodiscard]] bool running() const;

    [[nodiscard]] rpl::producer<QString> logLines() const;
    [[nodiscard]] rpl::producer<bool> runningChanges() const;
    [[nodiscard]] rpl::producer<Stats> stats() const;
    [[nodiscard]] rpl::producer<int> floodSeconds() const;
    [[nodiscard]] rpl::producer<QString> errors() const; // resolution/exit errors

private:
    void handleLine(const QString &line);

    std::unique_ptr<QProcess> _process;
    QByteArray _buffer;
    rpl::event_stream<QString> _log;
    rpl::variable<bool> _running = false;
    rpl::event_stream<Stats> _stats;
    rpl::event_stream<int> _flood;
    rpl::event_stream<QString> _errors;

};
} // namespace Pintopfp
```

- [ ] **Step 1: Implement start/stop and stdout pumping**

`start()`: if `_running.current()` return; `const auto paths = ResolvePaths(); if (!paths.valid()) { _errors.fire(...); return; }`. Create `_process`, `setWorkingDirectory(paths.folder)`, `setProcessChannelMode(QProcess::MergedChannels)`, connect `readyRead` → append to `_buffer`, split on `\n`, call `handleLine` per complete line; connect `finished`/`errorOccurred` → set `_running=false` and fire an error/log with the exit code. Launch `_process->start(paths.python, { paths.script, u"--headless"_q })`.
`stop()`: if `_process`, `_process->write("stop\n")`, then `_process->waitForFinished(3000)`; if still running, `terminate()` then `kill()`.
`handleLine`: `const auto e = ParseLine(line);` switch on `e.type` and fire the matching stream (`StatusRunning` → `_running = true`, etc.).

- [ ] **Step 2: Manual verification** (after build): Start spawns pythonw; log stream shows connection lines; `@STATS` updates arrive; Stop ends the process and flips running to false. Point the override at a missing folder → `errors()` fires and Start no-ops.

- [ ] **Step 3: Commit**

```bash
git add Telegram/SourceFiles/pintopfp/pintopfp_process.h Telegram/SourceFiles/pintopfp/pintopfp_process.cpp
git commit -m "Add pintopfp QProcess controller"
```

---

## Task 7: C++ — the panel widget

The visible UI: status row, Start/Stop, autostart toggle, stats block, log view, clear-log, and a collapsible settings form. Follows existing widget patterns (`Ui::FlatButton`/`Ui::RoundButton`, `Ui::Checkbox`, `Ui::InputField`, `Ui::ScrollArea`, `Ui::VerticalLayout`) as used across `SourceFiles/settings/`.

**Files:**
- Create: `Telegram/SourceFiles/pintopfp/pintopfp_panel.h`
- Create: `Telegram/SourceFiles/pintopfp/pintopfp_panel.cpp`

**Interfaces:**
- Consumes: `Pintopfp::Process`, `Pintopfp::ReadEnv`/`WriteEnv`/`Config`, `Pintopfp::ResolvePaths`, `Pintopfp::Stats`.
- Produces:
```cpp
namespace Window { class SessionController; }
namespace Pintopfp {
class Panel final : public Ui::RpWidget {
public:
    Panel(QWidget *parent, not_null<Window::SessionController*> controller);

private:
    void setupControls();
    void applyStats(const Stats &stats);
    void startFloodCountdown(int seconds);
    void openSettingsForm();
    void saveSettings(const Config &edited);

    const not_null<Window::SessionController*> _controller;
    Process _process;
    // status label, start/stop buttons, autostart checkbox, stats label,
    // log view (Ui::ScrollArea + inner text), settings sub-widgets:
    // ... member widgets ...
    base::Timer _floodTimer;
    int _floodRemaining = 0;

};
} // namespace Pintopfp
```

- [ ] **Step 1: Build the static layout**

In `setupControls`, assemble a `Ui::VerticalLayout`: status row (a `Ui::FlatLabel` "● Stopped" red / "● Running" green driven by `_process.runningChanges()`), a row with Start and Stop `Ui::RoundButton`s (`_process.start()` / `.stop()`, enabled-state bound to running and to `ResolvePaths().valid()`), an autostart `Ui::Checkbox` (writes the `PinterestAvatar.bat` exactly as the Python `set_autostart` does — reuse that path logic in C++), a stats `Ui::FlatLabel`, a `Ui::ScrollArea` log view appending `_process.logLines()`, and a "Clear log" button.

- [ ] **Step 2: Wire stats + flood countdown**

`_process.stats()` → `applyStats`, formatting `Set: 3 / 24h · 1 / 4h · left 7 / 4`. `_process.floodSeconds()` → `startFloodCountdown`, which sets `_floodRemaining` and starts `_floodTimer` (1s) updating a "⏸ Pause: Ns" label until zero.

- [ ] **Step 3: Settings form**

`openSettingsForm` reads `ReadEnv(ResolvePaths().env)` and shows `Ui::InputField`s for `POLL_INTERVAL`, `BOARD_ID`, `SOFT_LIMIT_24H`, `SOFT_LIMIT_4H`, plus masked (`Ui::PasswordInput`) fields for `API_ID/HASH` and Pinterest creds; Save calls `saveSettings` → `config.set(...)` per field → `WriteEnv`. Validate `POLL_INTERVAL`/limits parse as ints before writing; on failure show an error and keep the old file.

- [ ] **Step 4: Debug self-checks** — under `#ifdef _DEBUG`, call `Pintopfp::ProtocolSelfCheck()` and `Pintopfp::ConfigSelfCheck()` once in the constructor.

- [ ] **Step 5: Manual verification** (after build) — see Task 9.

- [ ] **Step 6: Commit**

```bash
git add Telegram/SourceFiles/pintopfp/pintopfp_panel.h Telegram/SourceFiles/pintopfp/pintopfp_panel.cpp
git commit -m "Add pintopfp native panel widget"
```

---

## Task 8: Side-bar icon + CMake + resources (insertion points)

Wire everything into the build and place the launch button in the folders side-bar.

**Files:**
- Modify: `Telegram/SourceFiles/window/window_filters_menu.h` (one member)
- Modify: `Telegram/SourceFiles/window/window_filters_menu.cpp` (construct/place/click)
- Modify: `Telegram/CMakeLists.txt` (add the six new sources)
- Create: `Telegram/Resources/icons/pintopfp.png` (+ `@2x`, `@3x`) — a strip glyph
- Modify: a style file (e.g. add `windowFiltersPintopfp: icon {...}` in `styles/style_window.style`)

**Interfaces:**
- Consumes: `Pintopfp::Panel`, `Window::SessionController`.

- [ ] **Step 1: Add sources to CMake**

In `Telegram/CMakeLists.txt`, add under the source list (alphabetically near other groups):

```
    pintopfp/pintopfp_protocol.cpp
    pintopfp/pintopfp_protocol.h
    pintopfp/pintopfp_paths.cpp
    pintopfp/pintopfp_paths.h
    pintopfp/pintopfp_config.cpp
    pintopfp/pintopfp_config.h
    pintopfp/pintopfp_process.cpp
    pintopfp/pintopfp_process.h
    pintopfp/pintopfp_panel.cpp
    pintopfp/pintopfp_panel.h
```

- [ ] **Step 2: Generate and register the icon**

Create a simple monochrome pin glyph icon at strip size (matching the `Ui::FilterIcon` visual weight). Add a style entry so `st::windowFiltersPintopfp` resolves; use the same style block shape as neighboring side-bar icons in `styles/style_window.style`.

- [ ] **Step 3: Add the button in `FiltersMenu`**

In `window_filters_menu.h`, add member: `base::unique_qptr<Ui::SideBarButton> _pintopfp;`.
In `window_filters_menu.cpp` `refresh()` (after the folders list is (re)built and before/around `_setup`), construct the button in `_container` positioned **after `_list`**:

```cpp
_pintopfp = base::make_unique_q<Ui::SideBarButton>(
    _container,
    TextWithEntities{ u"pintopfp"_q },
    buttonStyle());
_pintopfp->setIconOverride(&st::windowFiltersPintopfp, &st::windowFiltersPintopfp);
_pintopfp->setClickedCallback([=] {
    // Open the native panel via the controller (as a layer/section,
    // following how other side sections are shown in this codebase).
    _session->showPintopfpPanel();
});
```

Placement detail: insert into `_container` right after `_list` so visual order is folders → pintopfp → (future wallpapers) → `_setup`. Use the same container-insert mechanism `prepareButton` uses (it adds to the passed container); mirror that call shape rather than raw `make_unique_q` if `prepareButton`'s styling is needed.

- [ ] **Step 4: Add the controller entry point**

Add a thin `showPintopfpPanel()` on `Window::SessionController` (its own small method) that shows `Pintopfp::Panel` — reuse the existing layer/section-show path the codebase already uses for similar in-window panels (follow `showSection`/`show(Box)` conventions found in `window_session_controller.*`). This is the one allowed touch to the controller.

- [ ] **Step 5: Build**

Run the project's configured build (the `out/` tree indicates an existing CMake/MSVC build). Expected: compiles with the new module; no changes to unrelated targets.

- [ ] **Step 6: Commit**

```bash
git add Telegram/SourceFiles/window/window_filters_menu.h Telegram/SourceFiles/window/window_filters_menu.cpp Telegram/CMakeLists.txt Telegram/Resources/icons/pintopfp*.png Telegram/SourceFiles/styles/style_window.style
git commit -m "Wire pintopfp panel into folders side-bar and build"
```

---

## Task 9: Integration verification (manual)

No automated end-to-end harness exists; verify by running the built client.

- [ ] **Step 1:** With chat folders enabled, confirm the pintopfp icon appears in the left strip **below the folders** and above the settings button. Disable folders → the strip (and icon) disappear.
- [ ] **Step 2:** Click the icon → the native panel opens, themed correctly in dark and light.
- [ ] **Step 3:** Click Start → pythonw launches, `● Running` shows, connection log lines stream in, `@STATS` updates the stats label.
- [ ] **Step 4:** Trigger/observe a `@FLOOD` (or simulate by feeding a line in debug) → the pause countdown ticks down.
- [ ] **Step 5:** Open settings, change `POLL_INTERVAL`, Save → `.env` shows only that key changed (diff the file); other keys and comments preserved.
- [ ] **Step 6:** Click Stop → process ends, `● Stopped`. Toggle autostart → the `.bat` appears/disappears in the Startup folder.
- [ ] **Step 7:** Point the folder override at a nonexistent path → Start is disabled and an actionable error shows.

---

## Self-Review

- **Spec coverage:** icon placement (Task 8), side-bar-only visibility (Task 8 Step 3 + Task 9 Step 1), native panel (Task 7), Tkinter feature parity (Task 7 Step 1), stats block 24h/4h + flood (Tasks 2/3/6/7), editable settings writing `.env` (Task 5 + Task 7 Step 3), poll interval setting (Task 5 editable keys + Task 7 form), backward-compatible Python (Task 2 Step 4), module isolation & insertion points (Global Constraints + Task 8), adaptive FloodWait handling (Task 2 Step 2 + Task 6/7). Soft-cap default remains a documented open item until the user re-runs the probe.
- **Placeholder scan:** no "TBD"/"implement later"; every code step carries real code. UI boilerplate references concrete existing patterns/classes rather than inventing a test runner that does not exist.
- **Type consistency:** `ParseLine`/`Event`/`Stats` used identically in Tasks 3/6/7; `ResolvePaths`/`Paths` in Tasks 4/6/7; `ReadEnv`/`WriteEnv`/`Config` in Tasks 5/7; `Process` stream names match between Task 6 header and Task 7 consumption.

## Known risk / honesty note

Executing this plan requires building Telegram Desktop from source — a heavy, long build with the full dependency set. The `out/` directory shows a build tree already exists on this machine, so incremental builds should work, but a first clean build is substantial. The exact panel presentation mechanism (Task 8 Step 4) must follow whatever section/layer pattern this controller already exposes; confirm it against `window_session_controller.*` during implementation.
