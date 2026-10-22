# CLAUDE.md

fm (`irixfm`) is an IRIX-style file manager with the macOS Tahoe "Liquid Glass" look, for KDE
Plasma 6 on Wayland: Qt 6 QML + C++, KDE Frameworks, libvterm. It replaces Dolphin on the user's
desktop. **Read `docs/DEVELOPMENT.md` before changing code**: architecture, design rules,
performance notes and gotchas. The project skill `.claude/skills/irixfm/SKILL.md` has the
workflow in short.

The desktop around it (dock, top bar, themes) is `~/CODE/macos-desktop-setup` (its own `CLAUDE.md`
and `docs/SESSION-NOTES.md`).

## Where things are

```
src/main.cpp        start-up, single instance, IRIXFM_BENCH / IRIXFM_SNAPSHOT, shutdown watchdog
src/fm.*            the `Fm` object for QML: window blur/shape, KIO file actions, kinds, tags, search, sizes
src/terminal.*      terminal tabs: libvterm on a KPty, painted (TerminalView)
qml/Main.qml        the whole window (~3000 lines); other qml/*.qml are its parts
tests/*_test.cpp    off-screen Qt tests, one binary each; tests/single_instance_test.sh
tools/              bench.sh, shot.cpp, leak.cpp, make_icons.py, make_mockup.py
docs/               DEVELOPMENT.md (read first), FINDER-FEATURES.md, TAGS.md, bench-baseline.txt
```

Branches build on each other: main → finder-path → finder-features → finder-phase2 → finder-tags →
irix-tabs → perf (`perf-*` are merged expert branches). The main checkout `~/CODE/irixfm` is on
`perf`, and that's what is installed; `git log` messages explain why each change was made.

QML is loaded from disk at run time (`~/.local/share/irixfm/qml` when installed, the repo with
`IRIXFM_ASSETS=$PWD`), so a QML-only change needs no rebuild, only `make install` and a restart.

## Build, test, install

```
make                                         # build/irixfm (g++ + pkg-config, no cmake)
flock ~/.cache/irixfm-test.lock make test    # every test, off-screen; must end with 0 FAIL
make install                                 # into ~/.local (bin/irixfm, share/irixfm, menu entry "fm")
make tools                                   # build/shot and build/leak (not part of make test)
```

- Count the result: `make test 2>&1 | grep -c '^PASS'` and `grep -c '^FAIL'` (must be 0). The
  `flock` keeps agents in different worktrees from running the suite at the same time.
- One test binary on its own, off-screen, with throwaway settings:
  `XDG_CONFIG_HOME=$(mktemp -d) QT_QPA_PLATFORM=offscreen build/<name>_test`
  (`make build/<name>_test` first).
- `IRIXFM_TEST_NETWORK=1` makes `browser_test` also load real sites (three tabs on example.com).
- `tests/single_instance_test.sh build/irixfm` checks the one-process behaviour (part of `make test`).
- New test file: add a `$(BUILD)/x_test` rule and put it in `TESTS` in the `Makefile`. Tests are
  plain `main()` programs printing `PASS …` / `FAIL …` lines (see `check()` in any test); they find
  QML items by `objectName` and call QML functions with `QMetaObject::invokeMethod`. Which test
  covers what: `terminal_test` (terminal tabs), `browser_test` (web tabs), `split_test` (split view,
  list columns), `tab_test`, `sidebar_test`, `tag_test`, `finder_test` / `phase2_test` (Finder
  commands), `path_test`, `scroll_test`, `dock_test`, `preview_test`.

## Environment variables

| Variable | Effect |
|---|---|
| `IRIXFM_ASSETS=<dir>` | load `qml/` and `icons/` from there (the repo: `$PWD`) instead of `~/.local/share/irixfm` |
| `IRIXFM_SNAPSHOT=<png>` | save the window 2.5 s after start and quit |
| `IRIXFM_BENCH=1\|split` | print start-up and scroll timings and quit (`tools/bench.sh`) |
| `IRIXFM_TERM_BENCH=<dir>` | `terminal_test` runs the terminal benchmark instead of the tests |
| `IRIXFM_SINGLE=1` | single-instance behaviour even off-screen (it's off for off-screen, bench, snapshot runs) |
| `IRIXFM_SHELL=<path>` | shell for terminal tabs instead of the login shell (tests use bash) |
| `IRIXFM_NO_DIALOGS=1` | KIO job errors print instead of opening dialogs (tests) |
| `IRIXFM_TEST_NETWORK=1` | `browser_test` also loads real sites |
| `QT_FORCE_STDERR_LOGGING=1` | Qt/QML messages on stderr instead of the journal |
| `QT_LOGGING_RULES="irixfm.terminal.debug=true"` | log terminal drags and pinches |

## Restarting the user's fm safely

Only when the user wants the new build running (after `make install`):

```
pkill -TERM -x irixfm                         # quits cleanly, saves settings; a watchdog ends it after 5 s
pgrep -ax irixfm                              # read ALL lines: must be empty before starting
systemd-run --user --collect ~/.local/bin/irixfm
```

- **Never start fm straight from Claude's shell** (`irixfm &`). It would inherit Claude's
  environment (e.g. `SHELL=/usr/bin/zsh`) and pass it to fm's terminal tabs and to any tmux server
  started there. That happened: the user's tmux ran zsh instead of fish.
- Don't check with `pgrep … | head -1`: old fm processes hung in WebEngine's shutdown hid behind it
  once (9 h, one at 2.7 GB).
- fm is single-instance: a second `irixfm` hands its folder to the running one and exits, so a
  stale process means the new build doesn't start at all.

## Checking visuals

Always look at the image (Read the PNG) before reporting a visual change.

- Snapshot of the window, off-screen, default settings:
  `IRIXFM_ASSETS=$PWD XDG_CONFIG_HOME=$(mktemp -d) QT_QPA_PLATFORM=offscreen IRIXFM_SNAPSHOT=<out.png> build/irixfm <dir>`
  (taken 2.5 s after start). The glass is transparent in the PNG (KWin draws the blur); see
  DEVELOPMENT.md "Workflow" for compositing it over a wallpaper.
- A state that needs setting up first (list view, split view, a terminal tab, a menu): the
  screenshot harness `tools/shot.cpp`:
  `make build/shot`, then
  `XDG_CONFIG_HOME=$(mktemp -d) XDG_CACHE_HOME=$(mktemp -d) QT_QPA_PLATFORM=offscreen build/shot <start-dir> $PWD <out.png> '<qml expression>'`.
  The expression is evaluated on the window 1.2 s after start, the PNG saved at 6 s. Examples:
  `'viewMode = 1'`, `'toggleSplit()'`, `'openTerminal("shell")'`, `'openTerminal("web")'`.
- The real screen: `spectacle -b -n -f -o <file.png>`. The user may be using the screen, so the
  shot can show something else; prefer the off-screen ways.
- Put scratch images in Claude's scratchpad directory, not in the repo.

## Measuring

- Start-up and scrolling: `make && tools/bench.sh $PWD [runs] [split]` (`IRIXFM_BENCH=1` or
  `split`: QML loaded, first frame, items ready, a 121-frame scroll of ~, /usr/bin, /usr/lib).
  Compare medians; the machine is often busy. Baseline: `docs/bench-baseline.txt`.
- Terminal: `IRIXFM_TERM_BENCH=<dir> QT_QPA_PLATFORM=offscreen build/terminal_test` (throughput,
  paint times, scrollback RSS; PNGs into `<dir>`). See DEVELOPMENT.md "Performance: terminal".
- Leaks: `make build/leak`, then
  `XDG_CONFIG_HOME=$(mktemp -d) XDG_CACHE_HOME=$(mktemp -d) XDG_DATA_HOME=$(mktemp -d) QT_QPA_PLATFORM=offscreen build/leak $PWD nav|tabs|term|web`.
  Prints RSS per round; it should level off after the first round (2026-10-22: term +0 MB,
  tabs +10 MB, web +165 MB at round 1 for Chromium, then flat).

## Working in a team of agents

- One git worktree per agent: `git worktree add -b <branch> ../irixfm-wt-<name> perf` (from the
  branch being worked on). Never work in another agent's worktree or in `~/CODE/irixfm` itself
  unless you are the coordinator.
- **Never `git stash`**: the stash is shared by all worktrees, and it once mixed up two agents' work.
- Run `make test` under `flock ~/.cache/irixfm-test.lock`.
- Each agent commits small commits to its own branch (message ends with the Co-Authored-By line).
- The coordinator merges the branches, runs the full suite, `tools/bench.sh` and the leak test,
  then `make install` and the safe restart above.
- No remote exists; don't push.

## The user

- Writes short, informal requests, often with typos. "irixfm" / "the irix filebrowser" = fm;
  "finder bar" = the top menu bar; "button/buttom finder bar" = the dock; "the osx" = the macOS look.
- Wants things done, then a short summary.
- Wants the newest macOS look (Tahoe, Liquid Glass, "Clear": see-through rather than grey, edges
  brightest at the top) and the Nord colours.
- Terminal font stays **Noto Sans Mono 12** (fm's terminal tabs copy the Alacritty setup).
- "dokument everything" means: fm's `README.md`, `docs/DEVELOPMENT.md` and this file; in
  `~/CODE/macos-desktop-setup` `docs/SESSION-NOTES.md`, `README.md`, the skill and `scripts/install.sh`.
- **No sudo** (it needs a password): give the user a `! sudo …` line to run instead.

## Gotchas (details in docs/DEVELOPMENT.md)

- **The QML disk cache must hit**: don't import QtQuick.Pdf, QtMultimedia or QtWebEngine in files
  loaded at start; load such files by URL (Performance (start-up)).
- **reuseItems**: the delegate pool must be drained when a view switches model (folder ↔ results),
  or reused delegates show blank names (Performance: views).
- **libvterm reflow is off on purpose**: libvterm 0.3.3 aborts the whole process when reflowing
  on resize (Performance: terminal; `terminal.cpp`).
- **Box-drawing characters are painted** (`drawBoxChar()`), not taken from the font, so tmux/nvim
  borders are unbroken lines; keep it when changing the terminal's `paint()` (Performance: terminal).
- **One web profile**: `WebProfile.qml` is a single object shared by all web tabs and windows; two
  profiles on the same storage lock each other out (Web tabs).
- **Single instance**: a local socket named after the binary's path; off-screen, bench and snapshot
  runs stay alone, `IRIXFM_SINGLE=1` tests it anyway (Single instance and shutdown).
- **Shutdown watchdog**: extra windows are deleted before the engine, and the process ends 5 s
  after the event loop whatever hangs (Single instance and shutdown).
- **Terminals sit on the wallpaper**: no glass tint or blur behind a terminal tab (`termHoles` in
  Main.qml, passed to `Fm::updateWindow`) (Terminal tabs).
- **A drop onto a terminal must never reach the folder `DropArea`** behind it: that once moved the
  user's files. The terminal types the paths and answers `CopyAction` (Terminal details).
- **Logs**: Qt logs go to the journal: `journalctl --user _COMM=irixfm`; `QT_FORCE_STDERR_LOGGING=1`
  prints them instead.
- **Crashes**: `coredumpctl list irixfm`, `coredumpctl info <pid>`. ptrace is restricted
  (`ptrace_scope` 1: no `gdb -p`); for a hung process, `kill -ABRT <pid>` and read its core dump.
- **Tests and snapshots save settings**: always use a throwaway `XDG_CONFIG_HOME`.
- Claude's Bash tool may run zsh: an unmatched glob aborts the whole command (use `find … -delete`).
