# Changelog

Notable changes per release. The release notes on GitHub add the full commit list since the
previous tag. Versions follow [semver](https://semver.org); `VERSION` holds the current one.

## [0.1.0]

First release: the whole desktop (theme + fm) in one repo, installable with `scripts/install.sh all`.

### fm, the file manager
- IRIX Indigo Magic–style file manager with the macOS Tahoe glass look: notebook tabs, split view,
  Finder tags and list columns, Quick Look, terminal / Neovim / web tabs, KIO network folders,
  the SGI thumbwheel; replaces Dolphin (default file manager, dock Finder slot).
- Sidebar in two styles (View ▸ Sidebar Style): **Dolphin** (flush, divider, white icons, titles like
  Dolphin's) or **Finder** (floating glass card, coloured icons); drag its divider to resize it.
- The machine section is named after the computer (host name); Icon Catalog is now Applications.
- Window buttons are grey circles, the same in every window on the desktop.
- Security fixes: folder names can't run commands in terminal tabs (fish quoting, control
  characters); local pages in web tabs can't read other local files; download names stay in Downloads.

### IrixGlass: fm's look for everything else
- Aurorae title bars and frame: fm's glass, 22 px corners, top-lit edge, grey traffic lights placed
  exactly like fm's (first centre 30, 31 px; 26 px apart).
- Kvantum widget style (pills, edge rings, #0a84ff), GTK 3/4 theme IrixGlass-Dark, Plasma pop-ups,
  tooltips and OSDs, colour scheme, Firefox and Teams CSS.
- Menu bar in fm's glass colour; floating Liquid Glass dock.

### Desktop and tooling
- macOS-style menu bar and dock, Spotlight-style KRunner, KWin window menu, SF fonts, WhiteSur icons.
- CI on every push: fm build + tests on Arch, lint (shellcheck, ruff, qmllint, clang-tidy,
  actionlint, yamllint), security (CodeQL, gitleaks, zizmor, dependency review); releases only from
  a green pipeline.

### Known issues
- `network_test` has two KIO timing checks that fail now and then; CI retries it once.
