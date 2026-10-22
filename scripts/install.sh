#!/usr/bin/env bash
# Reapply the macOS-style KDE Plasma desktop setup (CachyOS / Arch, Plasma 6, Wayland).
#
# Usage:
#   scripts/install.sh all            # every step below, in order
#   scripts/install.sh <step> ...     # selected steps only
#   scripts/install.sh list           # show steps
#
# Nothing here uses sudo. System packages are listed by the `packages` step;
# install them yourself first:  sudo pacman -S --needed <list>
set -euo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"
CFG="$REPO/configs"
WORK="${XDG_CACHE_HOME:-$HOME/.cache}/macos-desktop-setup"
BACKUP="$HOME/.config/appearance-backup-$(date +%Y%m%d-%H%M%S)"
FF_PROFILE_GLOB="$HOME/.config/mozilla/firefox/*.default-release"
ATB_VERSION="v0.10.0"
ZAPZAP_VERSION="7.5.1"
AIU_VERSION="2.0.0-alpha-1-20251018"
WALLPAPER_URL="https://media.512pixels.net/downloads/macos-wallpapers-6k/27-Golden-Gate-Dark.png"
IRIXFM_REPO="$HOME/CODE/irixfm"
# storytold "Craft" apps: repo tag asset install-dir (see scripts/install-craft-app.sh)
CRAFT_APPS=(
    "photocraft v0.12.0 photocraft-0.12.0-linux-x86_64.AppImage PhotoCraft"
    "lightcraft v0.11.0 lightcraft-0.11.0-linux-x86_64.AppImage LightCraft"
    "vectorcraft v0.14.0 vectorcraft-0.14.0-linux-x86_64.AppImage VectorCraft"
    "designcraft v0.11.0 designcraft-0.11.0-linux-x86_64.AppImage DesignCraft"
    "figcraft v0.9.0 figcraft-0.9.0-linux-x86_64.AppImage FigCraft"
    "effectcraft v0.14.0 effectcraft-0.14.0-linux-x86_64.AppImage EffectCraft"
    "filmcraft v0.10.0 filmcraft-0.10.0-linux-x86_64.AppImage FilmCraft"
    "soundcraft v0.11.0 soundcraft-0.11.0-linux-x86_64.AppImage SoundCraft"
    "cadcraft v0.10.0 cadcraft-0.10.0-linux-x86_64.AppImage CADCraft"
    "wordcraft v0.9.0 wordcraft-0.9.0-linux-x86_64.AppImage WordCraft"
    "gridcraft v0.10.0 gridcraft-0.10.0-linux-x86_64.AppImage GridCraft"
    "deckcraft v0.9.0 deckcraft-0.9.0-linux-x86_64.AppImage DeckCraft"
    "pdfcraft v0.11.0 pdfcraft-0.11.0-linux-x86_64.AppImage PdfCraft"
    "craft-launcher v0.6.1 artcraft-launcher-0.6.1-linux-x86_64.AppImage ArtCraftLauncher"
)

STEPS=(packages whitesur fonts theme apply panels kwin search firefox terminal teams zapzap wallpaper appimageupdate craft irixfm)

log()  { printf '\033[1;34m==>\033[0m %s\n' "$*"; }
warn() { printf '\033[1;33m!!\033[0m %s\n' "$*" >&2; }

# Copy $1 to $2, backing up an existing different $2 first.
install_file() {
    local src="$1" dst="$2"
    mkdir -p "$(dirname "$dst")"
    if [[ -e "$dst" ]] && ! cmp -s "$src" "$dst"; then
        mkdir -p "$BACKUP"
        cp -a "$dst" "$BACKUP/$(echo "${dst#$HOME/}" | tr '/' '_')"
    fi
    cp "$src" "$dst"
}

clone() {  # clone <github owner/repo> into $WORK/<repo>
    local name="${1#*/}"
    mkdir -p "$WORK"
    [[ -d "$WORK/$name" ]] || git clone -q --depth 1 "https://github.com/$1.git" "$WORK/$name"
    echo "$WORK/$name"
}

# --------------------------------------------------------------------------
step_packages() {
    log "System packages (install manually with sudo):"
    echo "  sudo pacman -S --needed kvantum teams-for-linux neovim lazygit tree-sitter-cli 7zip"
    for p in kvantum teams-for-linux neovim 7zip; do
        pacman -Q "$p" &>/dev/null || warn "missing package: $p"
    done
}

step_whitesur() {
    log "WhiteSur KDE (dark), icons, cursors, GTK theme"
    local kde icons cursors gtk
    kde=$(clone vinceliuice/WhiteSur-kde)
    icons=$(clone vinceliuice/WhiteSur-icon-theme)
    cursors=$(clone vinceliuice/WhiteSur-cursors)
    gtk=$(clone vinceliuice/WhiteSur-gtk-theme)
    bash "$kde/install.sh" -c dark
    (cd "$icons" && bash install.sh)
    (cd "$cursors" && bash install.sh)
    mkdir -p "$HOME/.icons" && ln -sfn "$HOME/.local/share/icons/WhiteSur-cursors" "$HOME/.icons/WhiteSur-cursors"
    # Prebuilt GTK theme; WhiteSur's GTK installer runs sudo and may reset the clock.
    mkdir -p "$HOME/.themes" && tar -xJf "$gtk/release/WhiteSur-Dark.tar.xz" -C "$HOME/.themes"
    # Fix wrong theme names in the WhiteSur Liquid global themes
    for lnf in WhiteSurLiquid-dark WhiteSurLiquid; do
        install_file "$CFG/look-and-feel/$lnf/defaults" \
            "$HOME/.local/share/plasma/look-and-feel/com.github.vinceliuice.$lnf/contents/defaults"
    done
}

step_fonts() {
    log "Apple SF Pro / SF Mono fonts (downloaded from Apple; license: Apple's terms)"
    local dl="$WORK/fonts" z
    mkdir -p "$dl"
    if command -v 7z &>/dev/null; then
        z=7z
    else
        warn "7z not installed: sudo pacman -S 7zip"; return 1
    fi
    for f in SF-Pro SF-Mono; do
        [[ -f "$dl/$f.dmg" ]] || curl -fsSL -o "$dl/$f.dmg" "https://devimages-cdn.apple.com/design/resources/download/$f.dmg"
        rm -rf "$dl/$f" && mkdir -p "$dl/$f" && (cd "$dl/$f" && $z x -y "../$f.dmg" >/dev/null)
        local pkg; pkg=$(find "$dl/$f" -name '*.pkg' | head -1)
        mkdir -p "$dl/$f/p" && (cd "$dl/$f/p" && $z x -y "$pkg" >/dev/null && $z x -y 'Payload~' -ofonts >/dev/null)
    done
    mkdir -p "$HOME/.local/share/fonts/apple"
    find "$dl" -path '*/fonts/*' -type f \( -name '*.otf' -o -name '*.ttf' \) -exec cp {} "$HOME/.local/share/fonts/apple/" \;
    fc-cache -f "$HOME/.local/share/fonts"
}

step_theme() {
    log "Custom Liquid Glass dock/menu bar + no grey squares behind dock icons"
    local w="$HOME/.local/share/plasma/desktoptheme/WhiteSurLiquid-dark/widgets"
    [[ -d "$w" ]] || { warn "WhiteSurLiquid-dark not installed; run step whitesur"; return 1; }
    for f in panel-background tasks; do
        [[ -f "$w/$f.svgz.orig" ]] || cp "$w/$f.svgz" "$w/$f.svgz.orig"
    done
    python3 -I "$REPO/theme/gen-panel-background.py" | gzip -9 > "$w/panel-background.svgz"
    zcat "$w/tasks.svgz.orig" > "$WORK/tasks.svg"
    python3 -I "$REPO/theme/patch-tasks.py" "$WORK/tasks.svg" "$WORK/tasks-new.svg"
    gzip -9c "$WORK/tasks-new.svg" > "$w/tasks.svgz"
    # KWin computes the panel blur region at logical resolution, which leaves
    # stair-step artifacts outside rounded corners at 150%; disable panel blur.
    local rc="$w/../plasmarc"
    [[ -f "$rc.orig" ]] || cp "$rc" "$rc.orig"
    kwriteconfig6 --file "$rc" --group BlurBehindEffect --key enabled false
    # IrixGlass pop-ups, tooltips, buttons, fields, highlights (fm's glass); colours from IrixGlass.colors.
    python3 -I "$REPO/theme/gen-plasma-dialogs.py" \
        "$HOME/.local/share/plasma/desktoptheme/WhiteSurLiquid-dark" "$CFG/color-schemes/IrixGlass.colors"
    find "$HOME/.cache" -maxdepth 1 -name 'plasma_theme_WhiteSurLiquid*' -delete

    log "IrixGlass: windows with fm's look (decoration, Kvantum style, colour scheme)"
    local deco="$HOME/.local/share/aurorae/themes/IrixGlass"
    rm -rf "$deco" && python3 -I "$REPO/theme/gen-decoration.py" "$deco"
    [[ -d "$HOME/.config/Kvantum/WhiteSur" ]] || { warn "WhiteSur Kvantum theme missing; run step whitesur"; return 1; }
    python3 -I "$REPO/theme/gen-kvantum.py" "$HOME/.config/Kvantum/WhiteSur" "$HOME/.config/Kvantum/IrixGlass"
    install_file "$CFG/color-schemes/IrixGlass.colors" "$HOME/.local/share/color-schemes/IrixGlass.colors"
    # GTK 3/4 (needs python-gobject, rsvg-convert): WhiteSur-Dark in fm's colours, pills, traffic lights.
    python3 -I "$REPO/theme/gen-gtk.py" "$HOME/.themes/WhiteSur-Dark" "$HOME/.themes/IrixGlass-Dark"
}

step_apply() {
    log "Apply theme, icons, cursor, widget style, fonts, rendering"
    install_file "$CFG/kvantum/kvantum.kvconfig" "$HOME/.config/Kvantum/kvantum.kvconfig"
    install_file "$CFG/environment.d/freetype.conf" "$HOME/.config/environment.d/freetype.conf"
    plasma-apply-colorscheme IrixGlass
    plasma-apply-desktoptheme WhiteSur-dark >/dev/null; plasma-apply-desktoptheme WhiteSurLiquid-dark
    plasma-apply-cursortheme WhiteSur-cursors || kwriteconfig6 --file kcminputrc --group Mouse --key cursorTheme WhiteSur-cursors
    /usr/lib/plasma-changeicons WhiteSur-dark
    kwriteconfig6 --group KDE --key widgetStyle kvantum-dark
    kwriteconfig6 --group KDE --key DefaultDarkLookAndFeel com.github.vinceliuice.WhiteSurLiquid-dark
    # IrixGlass (theme/gen-decoration.py) needs Aurorae v2 and border size Large (= its 8 px glass frame).
    kwriteconfig6 --file kwinrc --group org.kde.kdecoration2 --key library org.kde.kwin.aurorae.v2
    kwriteconfig6 --file kwinrc --group org.kde.kdecoration2 --key theme __aurorae__svg__IrixGlass
    kwriteconfig6 --file kwinrc --group org.kde.kdecoration2 --key BorderSize Large
    kwriteconfig6 --file kwinrc --group org.kde.kdecoration2 --key BorderSizeAuto --type bool false
    kwriteconfig6 --file kwinrc --group org.kde.kdecoration2 --key ButtonsOnLeft XIA
    kwriteconfig6 --file kwinrc --group org.kde.kdecoration2 --key ButtonsOnRight ""
    kwriteconfig6 --file kwinrc --group Plugins --key blurEnabled true
    kwriteconfig6 --file kwinrc --group Plugins --key contrastEnabled true
    kwriteconfig6 --file kwinrc --group Effect-blur --key NoiseStrength 0   # grain made dock corners look rough
    local ui="SF Pro Text,10,-1,5,400,0,0,0,0,0,0,0,0,0,0,1"
    for k in font menuFont toolBarFont; do kwriteconfig6 --group General --key $k "$ui"; done
    kwriteconfig6 --group General --key smallestReadableFont "SF Pro Text,8,-1,5,400,0,0,0,0,0,0,0,0,0,0,1"
    kwriteconfig6 --group General --key fixed "SF Mono,10,-1,5,400,0,0,0,0,0,0,0,0,0,0,1"
    kwriteconfig6 --group WM --key activeFont "SF Pro Text,10,-1,5,600,0,0,0,0,0,0,0,0,0,0,1"
    kwriteconfig6 --group General --key XftAntialias true
    kwriteconfig6 --group General --key XftHintStyle hintnone
    kwriteconfig6 --group General --key XftSubPixel none --notify
    for v in 3.0 4.0; do
        local f="$HOME/.config/gtk-$v/settings.ini"
        [[ -f "$f" ]] && sed -i -e 's/^gtk-theme-name=.*/gtk-theme-name=IrixGlass-Dark/' \
            -e 's/^gtk-icon-theme-name=.*/gtk-icon-theme-name=WhiteSur-dark/' \
            -e 's/^gtk-cursor-theme-name=.*/gtk-cursor-theme-name=WhiteSur-cursors/' \
            -e 's/^gtk-font-name=.*/gtk-font-name=SF Pro Text,  10/' "$f"
    done
    # GTK 4 has no gtk-theme-name by default; it reads gsettings through the portal.
    local g4="$HOME/.config/gtk-4.0/settings.ini"
    [[ -f "$g4" ]] && { grep -q '^gtk-theme-name=' "$g4" || sed -i '/^\[Settings\]/a gtk-theme-name=IrixGlass-Dark' "$g4"; }
    if command -v gsettings &>/dev/null; then
        gsettings set org.gnome.desktop.interface gtk-theme IrixGlass-Dark
        gsettings set org.gnome.desktop.interface icon-theme WhiteSur-dark
        gsettings set org.gnome.desktop.interface cursor-theme WhiteSur-cursors
        gsettings set org.gnome.desktop.interface color-scheme prefer-dark
        gsettings set org.gnome.desktop.interface font-name 'SF Pro Text 10'
        gsettings set org.gnome.desktop.interface monospace-font-name 'SF Mono 10'
        gsettings set org.gnome.desktop.interface font-hinting none
        gsettings set org.gnome.desktop.interface font-antialiasing grayscale
    fi
    qdbus6 org.kde.KWin /KWin reconfigure
}

step_panels() {
    log "Menu bar + dock (replaces existing panels)"
    install_file "$CFG/icons/menubar-tux.svg" "$HOME/.local/share/icons/menubar-tux.svg"
    sed "s#@HOME@#$HOME#g" "$CFG/applications/spotlight-search.desktop" > "$WORK/spotlight-search.desktop"
    install_file "$WORK/spotlight-search.desktop" "$HOME/.local/share/applications/spotlight-search.desktop"
    # Trash as last dock launcher with WhiteSur's light (macOS) bin icon
    sed "s#@HOME@#$HOME#g" "$CFG/applications/dock-trash.desktop" > "$WORK/dock-trash.desktop"
    install_file "$WORK/dock-trash.desktop" "$HOME/.local/share/applications/dock-trash.desktop"
    kbuildsycoca6 >/dev/null 2>&1 || true
    if ! kpackagetool6 -t Plasma/Applet -l 2>/dev/null | grep -q com.github.antroids.application-title-bar; then
        curl -fsSL -o "$WORK/atb.plasmoid" \
            "https://github.com/antroids/application-title-bar/releases/download/$ATB_VERSION/application-title-bar.plasmoid"
        kpackagetool6 -t Plasma/Applet -i "$WORK/atb.plasmoid"
    fi
    mkdir -p "$BACKUP" && cp -a "$HOME/.config/plasma-org.kde.plasma.desktop-appletsrc" "$HOME/.config/plasmashellrc" "$BACKUP/" 2>/dev/null || true
    qdbus6 org.kde.plasmashell /PlasmaShell evaluateScript "$(sed "s#@HOME@#$HOME#g" "$REPO/scripts/panels.js")"
    # Plasma only honours "windows go below" (always floating) after a shell restart
    systemctl --user restart plasma-plasmashell.service
    sleep 6
    qdbus6 org.kde.plasmashell /PlasmaShell evaluateScript \
        'var t=panels().filter(p=>p.location=="top")[0]; if (t) t.height=24;'
}

step_kwin() {
    log "KWin script: macOS Move & Resize menu (title bar right-click / Alt+F3)"
    local dst="$HOME/.local/share/kwin/scripts/macos-window-menu"
    mkdir -p "$dst" && cp -r "$REPO/kwin-scripts/macos-window-menu/." "$dst/"
    kwriteconfig6 --file kwinrc --group Plugins --key macos-window-menuEnabled true
    qdbus6 org.kde.KWin /KWin reconfigure
}

step_search() {
    log "Spotlight: Meta+Space KRunner, centred, apps then files, Baloo file-name index"
    install_file "$CFG/krunner/krunnerrc" "$HOME/.config/krunnerrc"
    kwriteconfig6 --file kglobalshortcutsrc --group services --group org.kde.krunner.desktop \
        --key _launch "Meta+Space	Alt+Space	Alt+F2"
    kwriteconfig6 --file baloofilerc --group General --key "only basic indexing" true
    kwriteconfig6 --file baloofilerc --group General --key folders "$HOME/"
    kwriteconfig6 --file baloofilerc --group General --key "exclude folders" "$HOME/.cache/,$HOME/.local/share/Trash/,$HOME/aur/"
    timeout 20 balooctl6 enable || true
    timeout 10 kquitapp6 krunner || true
    warn "Meta+Space becomes active after the next login"
}

step_firefox() {
    # WhiteSur (two rows: toolbar, then tabs). The single-row "Monterey" variant
    # overlaps tabs with the URL bar on Firefox 158.
    log "Firefox WhiteSur theme + hide built-in VPN button (restart Firefox afterwards)"
    local gtk src profile
    gtk=$(clone vinceliuice/WhiteSur-gtk-theme); src="$gtk/other/firefox"
    for profile in $FF_PROFILE_GLOB; do
        [[ -d "$profile" ]] || continue
        local c="$profile/chrome"
        [[ -e "$c" ]] && { mkdir -p "$BACKUP"; cp -a "$c" "$BACKUP/firefox-chrome-$(basename "$profile")"; rm -rf "$c"; }
        mkdir -p "$c"
        cp -r "$src/WhiteSur" "$src/customChrome.css" "$c/"
        cp -r "$src/common/icons" "$src/common/pages" "$src/common/titlebuttons" "$c/WhiteSur/"
        cp "$src"/common/*.css "$c/WhiteSur/"
        mkdir -p "$c/WhiteSur/parts" && cp "$src"/common/parts/*.css "$c/WhiteSur/parts/"
        cp "$src/userChrome-WhiteSur.css" "$c/userChrome.css"
        cp "$src/userContent-WhiteSur.css" "$c/userContent.css"
        cp "$CFG/firefox/customChrome.css" "$c/customChrome.css"   # IrixGlass: fm's look
        cp "$CFG/firefox/userContent-irixglass.css" "$c/"           # fm's look for about: pages
        echo '@import "userContent-irixglass.css";' >> "$c/userContent.css"
        install_file "$CFG/firefox/user.js" "$profile/user.js"
    done
}

step_terminal() {
    log "tmux, Alacritty (Nord), LazyVim (Nord)"
    install_file "$CFG/tmux/tmux.conf" "$HOME/.tmux.conf"
    install_file "$CFG/alacritty/alacritty.toml" "$HOME/.config/alacritty/alacritty.toml"
    if [[ ! -d "$HOME/.config/nvim" ]]; then
        git clone -q --depth 1 https://github.com/LazyVim/starter "$HOME/.config/nvim"
        rm -rf "$HOME/.config/nvim/.git"
    fi
    install_file "$CFG/nvim/colorscheme.lua" "$HOME/.config/nvim/lua/plugins/colorscheme.lua"
    tmux source-file "$HOME/.tmux.conf" 2>/dev/null || true
}

step_teams() {
    log "teams-for-linux IrixGlass theme (fm's look; the Nord theme stays installed)"
    install_file "$CFG/teams-for-linux/config.json" "$HOME/.config/teams-for-linux/config.json"
    mkdir -p "$HOME/.config/teams-for-linux/themes"
    cp -r "$CFG/teams-for-linux/themes/nord" "$CFG/teams-for-linux/themes/irixglass" "$HOME/.config/teams-for-linux/themes/"
}

step_zapzap() {
    log "WhatsApp (ZapZap $ZAPZAP_VERSION, extracted AppImage, no FUSE needed)"
    local dir="$HOME/Applications/ZapZap"
    if [[ ! -x "$dir/AppRun" ]]; then
        mkdir -p "$HOME/Applications"
        curl -fsSL -o "$WORK/ZapZap.AppImage" \
            "https://github.com/rafatosta/zapzap/releases/download/$ZAPZAP_VERSION/ZapZap-$ZAPZAP_VERSION-linux-x86_64.AppImage"
        chmod +x "$WORK/ZapZap.AppImage"
        (cd "$WORK" && rm -rf squashfs-root && ./ZapZap.AppImage --appimage-extract >/dev/null)
        rm -rf "$dir" && mv "$WORK/squashfs-root" "$dir"
    fi
    mkdir -p "$HOME/.local/share/icons/hicolor/scalable/apps"
    cp "$dir/com.rtosta.zapzap.svg" "$HOME/.local/share/icons/hicolor/scalable/apps/"
    sed "s#^Exec=.*#Exec=$dir/AppRun %u#" "$CFG/applications/com.rtosta.zapzap.desktop" > "$WORK/zapzap.desktop"
    install_file "$WORK/zapzap.desktop" "$HOME/.local/share/applications/com.rtosta.zapzap.desktop"
    kbuildsycoca6 >/dev/null 2>&1 || true
}

step_wallpaper() {
    log "Wallpaper: macOS 27 Golden Gate Dark"
    local f="$HOME/.local/share/wallpapers/macos/Golden-Gate-Dark.png"
    [[ -f "$f" ]] || { mkdir -p "$(dirname "$f")"; curl -fsSL -o "$f" "$WALLPAPER_URL"; }
    plasma-apply-wallpaperimage "$f"
}

step_appimageupdate() {
    log "AppImageUpdate $AIU_VERSION (GUI + appimageupdatetool; static runtime, runs via fusermount3)"
    local dir="$HOME/Applications/AppImageUpdate" base="https://github.com/AppImageCommunity/AppImageUpdate/releases/download/$AIU_VERSION"
    mkdir -p "$dir" "$HOME/.local/bin" "$HOME/.local/share/icons/hicolor/128x128/apps"
    for f in AppImageUpdate-x86_64.AppImage appimageupdatetool-x86_64.AppImage; do
        [[ -f "$dir/$f" ]] || curl -fsSL -o "$dir/$f" "$base/$f"
        chmod +x "$dir/$f"
    done
    ln -sf "$dir/AppImageUpdate-x86_64.AppImage" "$HOME/.local/bin/AppImageUpdate"
    ln -sf "$dir/appimageupdatetool-x86_64.AppImage" "$HOME/.local/bin/appimageupdatetool"
    (cd "$WORK" && rm -rf squashfs-root && "$dir/AppImageUpdate-x86_64.AppImage" --appimage-extract 'usr/share/icons/*' >/dev/null \
        && cp squashfs-root/usr/share/icons/hicolor/128x128/apps/appimage.png "$HOME/.local/share/icons/hicolor/128x128/apps/appimageupdate.png")
    sed "s#^Exec=.*#Exec=$dir/AppImageUpdate-x86_64.AppImage %f#" "$CFG/applications/AppImageUpdate.desktop" > "$WORK/aiu.desktop"
    install_file "$WORK/aiu.desktop" "$HOME/.local/share/applications/AppImageUpdate.desktop"
    # fish_add_path returns 1 when the path is already there
    if command -v fish >/dev/null; then fish -c 'fish_add_path -U ~/.local/bin' || true; fi
    kbuildsycoca6 >/dev/null 2>&1 || true
}

step_craft() {
    log "storytold Craft apps (AppImages in ~/Applications, checksums verified)"
    local line
    for line in "${CRAFT_APPS[@]}"; do
        # shellcheck disable=SC2086
        "$REPO/scripts/install-craft-app.sh" $line
    done
    kbuildsycoca6 >/dev/null 2>&1 || true
    # Their menu entries claim PDF and MP3; keep Firefox and Haruna for those.
    xdg-mime default firefox.desktop application/pdf
    xdg-mime default org.kde.haruna.desktop audio/mpeg
}

step_irixfm() {
    log "fm (irixfm): IRIX file manager with the Tahoe look, as the default file manager"
    [[ -d "$IRIXFM_REPO" ]] || { warn "$IRIXFM_REPO missing (separate repo)"; return; }
    # Build needs (fm's README): Qt 6 (qt6-base has Qt6Network; WebEngine for web tabs), KF6 (KIO,
    # Kirigami, Solid, KArchive, SyntaxHighlighting, Pty) and libvterm for the terminal tabs.
    for p in base-devel qt6-declarative qt6-multimedia qt6-webengine kio kwindowsystem kirigami karchive solid syntax-highlighting kpty libvterm plasma-integration; do
        pacman -Q "$p" &>/dev/null || warn "missing package: $p (sudo pacman -S --needed $p)"
    done
    make -C "$IRIXFM_REPO" install
    # The dock's preferred://filemanager launcher follows this default.
    xdg-mime default irixfm.desktop inode/directory
    # fm is one process: a running fm keeps the old build. Restart it from the session (systemd-run),
    # not from a tool's shell, so its terminal tabs get the session's environment.
    if pgrep -x irixfm >/dev/null; then
        log "fm is running; to load the new build: pkill -TERM -x irixfm; then systemd-run --user --collect $HOME/.local/bin/irixfm"
    fi
}

# --------------------------------------------------------------------------
main() {
    mkdir -p "$WORK"
    [[ $# -gt 0 ]] || set -- list
    if [[ "$1" == list ]]; then printf '%s\n' "${STEPS[@]}"; exit 0; fi
    [[ "$1" == all ]] && set -- "${STEPS[@]}"
    for s in "$@"; do
        declare -F "step_$s" >/dev/null || { warn "unknown step: $s"; exit 1; }
        "step_$s"
    done
    [[ -d "$BACKUP" ]] && log "Backups of overwritten files: $BACKUP"
    log "Done. Log out and back in to pick up fonts, rendering and shortcuts."
}

main "$@"
