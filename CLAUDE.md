# CLAUDE.md

This repo recreates and documents a macOS Tahoe-style KDE Plasma 6 desktop on CachyOS, plus a Nord-themed terminal setup. Read `docs/SESSION-NOTES.md` before making changes: it has the full history, the system facts and the gotchas.

## Working rules for this setup

- **No sudo.** sudo needs a password, so don't run it. Give the user a one-line `! sudo …` command to run in the prompt instead.
- **Keep the terminal font.** Alacritty must stay on Noto Sans Mono 12. Don't apply UI font changes to the terminal.
- **Check changes visually.** After a visual change, take a screenshot (`spectacle -b -n -f -o <file>`) and look at it before reporting.
- **Back up before editing files you didn't create.** Theme originals are kept as `*.orig`.
- **Edit the repo, not just the system.** Change the generators in `theme/` or the files in `configs/`, then apply them with `scripts/install.sh <step>`, so the repo stays the source of truth. Copy anything changed directly on the system back into `configs/`.
- **Don't apply global themes or `panels.js` casually.** `plasma-apply-lookandfeel` and `scripts/panels.js` replace user-visible state. `panels.js` removes all panels.
- **Restarting plasmashell** (`systemctl --user restart plasma-plasmashell`) keeps windows open, but it resets the top bar to 36px. Set it back to 24.

## fm (in `fm/`)

`fm/` is fm, the IRIX-style file manager with the Tahoe look that replaces Dolphin here. For any work on fm, read its `CLAUDE.md` first (commands, tests, snapshots, team rules); its project skill is `.claude/skills/irixfm/SKILL.md`, and `docs/DEVELOPMENT.md` explains the code. Build, test and install it from `fm/` (`make -C fm …`).

- Test with `flock ~/.cache/irixfm-test.lock make test` (0 FAIL), then `make install`.
- **Restart fm safely:** `pkill -TERM -x irixfm`, check `pgrep -ax irixfm` is empty (read all lines), then `systemd-run --user --collect ~/.local/bin/irixfm`. Never start it straight from Claude's shell: Claude's environment (`SHELL=/usr/bin/zsh`) would reach fm's terminal tabs and any tmux server started there.
- **Teams of agents:** one git worktree per agent (`git worktree add -b <branch> ../irixglass-wt-<name> main`, work in its `fm/`), never `git stash` (shared between worktrees), `flock` around `make test`; the coordinator merges, tests, benchmarks, installs and restarts.

## The user

- They write short, informal requests, often with typos. Their words map to things on screen like this:
  - "finder bar" means the top menu bar.
  - "button/buttom finder bar" means the bottom dock.
  - "the osx" means the macOS look.
  - "irixfm" / "the irix filebrowser" means fm in `fm/`.
- They want the newest macOS look (Tahoe, Liquid Glass, "Clear" style): see-through rather than grey, and a dock edge that's brightest at the top.
- They prefer that things just get done, with a short summary afterwards.
- "dokument everything" means: update `docs/SESSION-NOTES.md`, the README, the skill and `install.sh` here, and in `fm/` its `README.md`, `docs/DEVELOPMENT.md` and `CLAUDE.md`.
