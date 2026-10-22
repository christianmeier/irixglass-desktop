---
name: macos-desktop
description: Change, fix or reapply the macOS-style KDE Plasma 6 desktop on this CachyOS machine (menu bar, dock, Liquid Glass theme, WhiteSur, SF fonts, KRunner Spotlight, KWin window menu, Firefox/Teams/terminal theming). Use when the user mentions the top bar ("finder bar"), the dock ("button finder bar"), the macOS/OSX look, themes, transparency, fonts or window arrangement.
---

# macOS desktop setup

Repository: `~/CODE/macos-desktop-setup`. Read `docs/SESSION-NOTES.md` before the first change in a session.

## Where things live

| Part | Repo source | Applied to | Apply with |
|---|---|---|---|
| Dock and menu bar background | `theme/gen-panel-background.py` | `~/.local/share/plasma/desktoptheme/WhiteSurLiquid-dark/widgets/panel-background.svgz` | `scripts/install.sh theme`, then reload (below) |
| Dock icon backgrounds and dots | `theme/patch-tasks.py` | `…/widgets/tasks.svgz` (original kept as `tasks.svgz.orig`) | `scripts/install.sh theme` |
| Panel layout | `scripts/panels.js` | plasmashell | `scripts/install.sh panels` (**replaces all panels**) |
| Window title bars and frame (fm look) | `theme/gen-decoration.py` | `~/.local/share/aurorae/themes/IrixGlass/` | `scripts/install.sh theme apply` |
| Widget style (fm look) | `theme/gen-kvantum.py` (recolours WhiteSur Liquid Dark) | `~/.config/Kvantum/IrixGlass/` | `scripts/install.sh theme` (Kvantum picks it up on app start) |
| GTK theme (fm look) | `theme/gen-gtk.py` (recolours WhiteSur-Dark) | `~/.themes/IrixGlass-Dark/` | `scripts/install.sh theme apply` |
| Plasma pop-ups, tooltips, OSDs (fm look) | `theme/gen-plasma-dialogs.py` | `…/desktoptheme/WhiteSurLiquid-dark/{dialogs,widgets,solid}` (originals `*.orig`) | `scripts/install.sh theme`, then reload the Plasma style |
| Firefox (fm look) | `configs/firefox/customChrome.css`, `userContent-irixglass.css` | profile `chrome/` | `scripts/install.sh firefox`, restart Firefox |
| Colour scheme (fm palette) | `configs/color-schemes/IrixGlass.colors` | `~/.local/share/color-schemes/` | `scripts/install.sh theme`, `plasma-apply-colorscheme IrixGlass` |
| Window menu | `kwin-scripts/macos-window-menu/` | `~/.local/share/kwin/scripts/` | `scripts/install.sh kwin` |
| Global theme fixes | `configs/look-and-feel/*/defaults` | `~/.local/share/plasma/look-and-feel/com.github.vinceliuice.*/contents/defaults` | `scripts/install.sh whitesur` |
| Wallpaper | `WALLPAPER_URL` in `scripts/install.sh` | `~/.local/share/wallpapers/macos/` | `scripts/install.sh wallpaper` |
| AppImageUpdate, Craft apps | `AIU_VERSION`, `CRAFT_APPS` in `scripts/install.sh`; `scripts/install-craft-app.sh` | `~/Applications/…`, menu entries | `scripts/install.sh appimageupdate craft` |
| fm file manager | separate repo `~/CODE/irixfm` | `~/.local/bin/irixfm`, default for folders | `scripts/install.sh irixfm` (or `make -C ~/CODE/irixfm install`) |
| Fingerprint login | `configs/pam/plasmalogin` | `/etc/pam.d/plasmalogin` | user runs the sudo copy (README "Manual steps") |
| Other configs | `configs/…` | see `scripts/install.sh` | the matching step |

## Tuning the dock and menu bar look

Edit the constants at the top of `theme/gen-panel-background.py`:

| Constant | Controls |
|---|---|
| `R` | Corner size: where the curve meets the straight edge. Keep `R × DEVICE_SCALE` a whole number |
| `N` | Corner curve: 2 = circle, about 5 = Apple continuous corner (squircle) |
| `BODY` | How clear the glass is |
| `RIM_TOP`, `RIM_SIDE`, `RIM_BOTTOM` | Edge highlight strength at the top, sides and bottom |
| `OUTER` | Dark outline |
| `NORTH`, `NORTH_TINT`, `NORTH_EDGE` | Menu bar glass: tint opacity, tint colour (fm blue-grey), bottom light line |
| `MARGIN` | Dock icon padding (bigger value, smaller icons) |
| `NORTH_MARGIN` | Menu bar padding |

Then reload:

```bash
scripts/install.sh theme
plasma-apply-desktoptheme WhiteSur-dark >/dev/null && plasma-apply-desktoptheme WhiteSurLiquid-dark
```

To preview the SVG before installing, run `rsvg-convert -z 3 -b '#556' file.svg -o out.png` and read the PNG.

## Live panel tweaks (Plasma scripting)

```bash
qdbus6 org.kde.plasmashell /PlasmaShell evaluateScript '
var top  = panels().filter(p => p.location == "top")[0];
var dock = panels().filter(p => p.location == "bottom")[0];
dock.height = 50;   // icon size
'
```

Copy any lasting change into `scripts/panels.js` too.

## fm (the file manager)

fm replaces Dolphin (dock Finder slot + default for folders). It has its own repo, `~/CODE/irixfm`, with a `CLAUDE.md` and the **`irixfm` skill** (`~/CODE/irixfm/.claude/skills/irixfm/SKILL.md`): read those for any change to fm (views, sidebar, tabs, split view, terminal and web tabs). Rules that touch this desktop:

- Install: `scripts/install.sh irixfm` (checks the build packages, `make install`, sets fm as default for folders).
- **Restart fm with `pkill -TERM -x irixfm`, check `pgrep -ax irixfm` is empty, then `systemd-run --user --collect ~/.local/bin/irixfm`**, never straight from Claude's shell: that passes Claude's environment (`SHELL=zsh`) to fm, its terminals and any tmux server they start.
- fm keeps the dock's height free when maximized (it reads the dock's settings), so a dock change shows up in fm too.

## IrixGlass: all windows look like fm

The values (tint `rgba(20,22,29,.62)`, 22 px corners, top-lit 1.2 px edge ring, traffic lights, `#0a84ff`) come from fm's QML (`qml/Main.qml`, `EdgeRing.qml`, `TrafficLights.qml`). If fm's look changes, change the constants in `theme/gen-decoration.py`, `theme/gen-kvantum.py` and `IrixGlass.colors` to match.

- Reload the decoration: `python3 theme/gen-decoration.py ~/.local/share/aurorae/themes/IrixGlass && qdbus6 org.kde.KWin /KWin reconfigure`.
- **Borders:** Aurorae v2 clamps `BorderLeft/Right/Bottom` to KWin's border-size range (Large = 6–8 px; the theme uses 8). Maximized windows use only `decoration-maximized[-inactive]-center`. Blur comes from the `mask-*` pieces. KWin keeps a theme's rc while windows use it: switch to a copy under another name, `reconfigure`, switch back, `reconfigure`.
- **Kvantum:** rounded frames need `expand-*` elements; menu shadows stay off (square menus otherwise). **Plasma pop-ups** have no blur (panel blur is off), so their tint is dense.
- **Testing without disturbing the user:** own test windows only; Firefox in a throwaway profile with Marionette; widgets in an off-screen Qt gallery. Opening KRunner or the clipboard pop-up steals keyboard focus.
- **Traffic lights:** fm is the master: first centre (30, 31), 26 px apart, 13.3 px, in the decoration (TITLE 62, LIGHT_X 30), GTK (header bar 62 px) and Firefox (nav-bar 62 px). Check with a full screenshot and the frame geometry from a KWin script.
- KWin doesn't clip clients to the rounded shape, which is why there's an 8 px glass frame: the client's square corners must sit inside the 22 px curve.

## Checking the result

Take a screenshot with `spectacle -b -n -f -o /tmp/…/shot.png` and read the image. Never report a visual change as done without checking it.

## Pitfalls

- **Quick Settings theme tiles apply a global theme.** If everything suddenly looks like Breeze, the user clicked a tile. Run `scripts/install.sh whitesur apply` to restore.
- **The dock stops floating** when its visibility is "always visible". It has to be `windowsgobelow` (`panelVisibility=3`), followed by a plasmashell restart.
- **A plasmashell restart resets the top bar to 36px.** Set it back to 24.
- **zsh is the Bash tool's shell.** An unmatched glob like `rm ~/.cache/foo*` aborts the whole command, so use `find … -delete`.
- **No sudo.** Ask the user to run `! sudo …` themselves.
- **Panel blur is off on purpose** (`[BlurBehindEffect] enabled=false` in the theme's `plasmarc`). KWin computes the panel blur area at layout resolution and leaves stair-step artifacts outside the rounded corners at 150%. The theme's `mask-*` elements don't change that.
- **The user may be using the screen.** A full-screen screenshot can show their browser instead of your window; check `pgrep`, use `spectacle -a` (active window) right after launching, or fm's snapshot mode.
- **Qt apps log to the journal**, not the terminal; set `QT_FORCE_STDERR_LOGGING=1` to see QML errors.
- **Keep edge widths in device pixels** (`PX = 1/DEVICE_SCALE`) and `R × DEVICE_SCALE` whole, or the edges go soft again.
