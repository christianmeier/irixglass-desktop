# macOS-style desktop for CachyOS (KDE Plasma 6)

This folder holds everything needed to recreate or continue the desktop setup built on 2026-10-20. It covers:

- **Desktop look:** a macOS Tahoe (Liquid Glass) look for KDE Plasma 6 on Wayland. Every window is styled like fm ("IrixGlass": title bar, frame, widgets and colours).
- **Terminal:** a Nord-themed tmux, Alacritty and LazyVim setup.
- **Apps:** Teams, WhatsApp and Firefox theming, AppImageUpdate and the storytold "Craft" apps.
- **fm:** an IRIX-style file manager with the Tahoe glass look, which replaces Dolphin. It has IRIX notebook tabs, a split view (each side with its own tabs, view and icon size), Finder tags and list columns, Quick Look, terminal / Neovim tabs with the Alacritty look, and web tabs; all windows run in one process. It lives in its own repo, `~/CODE/irixfm` (branch `perf` installed).

## Quick start

```bash
cd ~/CODE/macos-desktop-setup
scripts/install.sh packages     # lists the pacman packages to install with sudo
sudo pacman -S --needed kvantum teams-for-linux neovim lazygit tree-sitter-cli 7zip
scripts/install.sh all          # reapply everything (no sudo used)
```

Then log out and back in.

Each step can also be run on its own: `scripts/install.sh <step> [<step> ...]`.

| Step | What it does |
|---|---|
| `packages` | Lists the system packages, which have to be installed with sudo, and warns about any that are missing |
| `whitesur` | Installs the WhiteSur KDE theme (dark and Liquid), icons, cursors and prebuilt GTK theme, and fixes broken names in the Liquid global themes |
| `fonts` | Downloads SF Pro and SF Mono from Apple and unpacks them into `~/.local/share/fonts/apple` |
| `theme` | Builds the custom dock and menu bar background (`theme/gen-panel-background.py`: squircle corners, crisp 1-pixel edges), removes the grey squares behind dock icons (`theme/patch-tasks.py`), and turns off panel blur, which caused stair-step artifacts at the corners. Preview a change first with `theme/preview.py`. Also builds **IrixGlass**, fm's look for all windows: the Aurorae title bar and frame (`theme/gen-decoration.py`), the Kvantum widget style (`theme/gen-kvantum.py`), the GTK theme IrixGlass-Dark (`theme/gen-gtk.py`), the Plasma pop-ups/tooltips/OSDs (`theme/gen-plasma-dialogs.py`) and the colour scheme (`configs/color-schemes/IrixGlass.colors`) |
| `apply` | Applies the IrixGlass colours, Plasma style, IrixGlass-Dark GTK theme, IrixGlass title bars (macOS button order, Aurorae v2, border size Large), icons, cursor, Kvantum style, SF fonts, macOS-style text rendering and GTK settings |
| `panels` | **Replaces all panels** with the top menu bar and the floating dock (`scripts/panels.js`), then restarts plasmashell |
| `kwin` | Installs the KWin script that adds a "Move & Resize" menu to the window menu |
| `search` | Sets Meta+Space to open KRunner in a centered pop-up (Spotlight-style), shows apps then files, and turns on Baloo file-name indexing |
| `firefox` | Installs the WhiteSur Firefox theme (tabs in their own row) with the IrixGlass override (`configs/firefox/customChrome.css`, `userContent-irixglass.css`: fm's colours, pills, traffic lights) into each `*.default-release` profile and hides the built-in VPN button |
| `terminal` | Installs the tmux config (Ctrl-a prefix, Nord, mouse), Alacritty config (Nord, Noto Sans Mono 12) and LazyVim with Nord |
| `teams` | Installs the teams-for-linux themes (IrixGlass active, Nord kept) |
| `zapzap` | Installs WhatsApp (ZapZap AppImage, unpacked into `~/Applications/ZapZap`) and its launcher entry |
| `wallpaper` | Downloads the macOS 27 "Golden Gate Dark" wallpaper to `~/.local/share/wallpapers/macos/` and sets it |
| `appimageupdate` | Installs AppImageUpdate (GUI and `appimageupdatetool`) into `~/Applications/AppImageUpdate`, a menu entry, and `~/.local/bin` on fish's PATH |
| `craft` | Installs the 14 storytold Craft apps (PhotoCraft, WordCraft, …) as checksum-verified AppImages with menu entries (`scripts/install-craft-app.sh`), and keeps Firefox and Haruna as the default PDF and MP3 apps |
| `irixfm` | Checks the build packages (`libvterm`, `kpty`, `syntax-highlighting`, `qt6-webengine` and the rest of fm's README list), builds and installs fm from `~/CODE/irixfm` (`make install`) and makes it the default file manager, which the dock follows. A running fm keeps the old build until it's restarted: `pkill -TERM -x irixfm`, then `systemd-run --user --collect ~/.local/bin/irixfm` |

Before overwriting a file, the script copies it to `~/.config/appearance-backup-<timestamp>/`.

## Manual steps (need your password)

These can't be scripted without sudo, so run them yourself:

| What | How |
|---|---|
| Dark login screen | System Settings → Login Screen → **Apply Plasma Settings…** (copies the dark colours, fonts, icons and cursor). Re-run after changing the desktop theme |
| Login screen wallpaper | Same page → **Configure Appearance…** → pick `~/.local/share/wallpapers/macos/Golden-Gate-Dark.png` → **Apply** |
| Fingerprint login | `sudo cp configs/pam/plasmalogin /etc/pam.d/plasmalogin`. At login, type the password as usual, or leave it empty, press Enter and touch the reader. Undo with `sudo rm /etc/pam.d/plasmalogin` |

sudo already accepts the fingerprint (set up by CachyOS), and so does the lock screen.

## Layout

```
configs/         copies of every user config file that was changed (pam/plasmalogin: fingerprint login)
kwin-scripts/    macos-window-menu KWin script
theme/           generator + patch scripts for the Plasma theme, and generated output
scripts/         install.sh, panels.js, install-craft-app.sh
docs/            SESSION-NOTES.md: what was done, why, gotchas, open items
docs/mockups/    IRIX fm × Tahoe mockups (make_mockups.py); layout A is what fm builds
.claude/skills/  Claude Code skill for continuing this work
CLAUDE.md        context for a new Claude Code session
```

Related repo: `~/CODE/irixfm` (fm, the file manager). Its README covers features and keys, `docs/DEVELOPMENT.md` the design, and its `CLAUDE.md` + `irixfm` skill how to work on it with Claude Code.

## Restarting the work with Claude Code

```bash
cd ~/CODE/macos-desktop-setup && claude
```

Claude reads `CLAUDE.md` automatically, and the `macos-desktop` skill in `.claude/skills/` loads when it's relevant. Before your first request, have Claude read `docs/SESSION-NOTES.md`, which has the full history.

For work on fm, start Claude in its repo instead (`cd ~/CODE/irixfm && claude`): it reads fm's `CLAUDE.md`, and the `irixfm` skill loads there.
