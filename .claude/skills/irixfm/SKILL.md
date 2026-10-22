---
name: irixfm
description: Change, fix, test, optimise, build, install or run fm, the IRIX-style file manager with the macOS Tahoe look (repo ~/CODE/irixfm, binary irixfm). Use when the user mentions fm, "irixfm", "the irix filebrowser", the file manager or its parts - icon/list views, list columns, sidebar, tabs, split view, terminal tabs, web tabs, Quick Look, tags, the thumbwheel, transparency - or asks to measure, profile or restart it.
---

# fm (irixfm)

Repository: `~/CODE/irixfm` (or an agent's worktree `~/CODE/irixfm-wt-<name>`). `CLAUDE.md` there has
every command; `docs/DEVELOPMENT.md` has the design, performance notes and gotchas.

## Workflow

1. **Read** `docs/DEVELOPMENT.md` (at least Architecture, the section for the part you change, and Gotchas).
   Features and their status: `README.md`, `docs/FINDER-FEATURES.md`, `docs/TAGS.md`.
2. **Change** the code. Find things with `grep -n` in `qml/Main.qml` (the window), `qml/*.qml`, `src/`.
   Keep the look: mockup A geometry (× `u`), system font sizes (`tu`), Nord, glass brightest at the top.
3. **Test**: add or extend a check in `tests/<area>_test.cpp`, then
   `flock ~/.cache/irixfm-test.lock make test` → count `PASS`, 0 `FAIL`.
4. **Look**: `IRIXFM_SNAPSHOT` or `build/shot` (off-screen, throwaway `XDG_CONFIG_HOME`), and Read the PNG.
5. **Measure** if it could cost time or memory: `tools/bench.sh`, `IRIXFM_TERM_BENCH`, `build/leak`.
6. **Commit** small commits on the branch (ending with the Co-Authored-By line). No `git stash`, no push.
7. **Install and restart** only when the user wants it running: `make install`, then
   `pkill -TERM -x irixfm`, `pgrep -ax irixfm` (all lines, must be empty),
   `systemd-run --user --collect ~/.local/bin/irixfm`. Never `irixfm &` from Claude's shell.
8. **Document**: README (features, keys), DEVELOPMENT.md (how and why, gotchas), CLAUDE.md (commands);
   on "dokument everything" also `~/CODE/macos-desktop-setup` (SESSION-NOTES, README, skill, install.sh).

## Commands

| What | Command |
|---|---|
| Build | `make` |
| All tests | `flock ~/.cache/irixfm-test.lock make test` |
| One test | `make build/x_test && XDG_CONFIG_HOME=$(mktemp -d) QT_QPA_PLATFORM=offscreen build/x_test` |
| Snapshot | `IRIXFM_ASSETS=$PWD XDG_CONFIG_HOME=$(mktemp -d) QT_QPA_PLATFORM=offscreen IRIXFM_SNAPSHOT=out.png build/irixfm <dir>` |
| Snapshot of a state | `make build/shot`; `XDG_CONFIG_HOME=$(mktemp -d) QT_QPA_PLATFORM=offscreen build/shot <dir> $PWD out.png 'toggleSplit()'` |
| Bench | `tools/bench.sh $PWD [runs] [split]` |
| Leak test | `make build/leak`; `XDG_CONFIG_HOME=$(mktemp -d) XDG_DATA_HOME=$(mktemp -d) QT_QPA_PLATFORM=offscreen build/leak $PWD nav\|tabs\|term\|web` |
| Logs / crashes | `journalctl --user _COMM=irixfm`; `coredumpctl list irixfm` |

## Team of agents

One worktree per agent (`git worktree add -b <branch> ../irixfm-wt-<name> <base>`), `flock` around
`make test`, commits on the agent's own branch, never `git stash`. The coordinator merges, runs the
full suite + bench + leak test, installs and restarts fm safely. Details: `CLAUDE.md`.

## Where knowledge lives

- `CLAUDE.md`: commands, safe restart, team rules, the user's preferences, top gotchas.
- `docs/DEVELOPMENT.md`: architecture per area, Performance (views / start-up / terminal), Gotchas.
- `git log`: why each change was made (commit messages explain the reason).
- `~/CODE/macos-desktop-setup/docs/SESSION-NOTES.md`: history, how fm is wired into the desktop.
