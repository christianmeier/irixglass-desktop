#!/usr/bin/env bash
# Build the release assets of IrixGlass desktop, locally or in .github/workflows/release.yml.
#
# Usage:
#   packaging/make-release.sh [--binary <irixfm>] [--ref <git ref>] <version> <outdir>
#   packaging/make-release.sh --check [--ref <git ref>] <version>    # only validate the version
#
#   <version>   semver without "v" (0.1.0, 0.2.0-rc.1); must match the VERSION file at <ref>
#   <outdir>    created if missing; must be empty
#   --binary    a built irixfm to bundle (CI passes the one from the fm-build-x86_64 artifact).
#               Default: fm/build/irixfm, built with `make -C fm` if it's missing. Nothing is installed.
#   --ref       the commit to package (default HEAD); qml/, icons/, generators and the source
#               tarball all come from this commit, not from the working tree
#
# Writes to <outdir>:
#   irixglass-desktop-<v>.tar.gz          source (git archive of <ref>)
#   irixfm-<v>-x86_64.tar.zst             fm bundle: bin/, share/irixfm/{qml,icons}, .desktop, D-Bus service, install.sh
#   irixglass-theme-<v>.tar.zst           theme parts that don't need WhiteSur: Aurorae IrixGlass, IrixGlass.colors
#   PKGBUILD                              Arch package of fm from the source tarball (checksum filled in)
#   SHA256SUMS                            sha256 of all of the above
#   release-notes.md                      not an asset: the GitHub release body
#
# Archives are reproducible: sorted, owner root, mtime = commit time.
set -euo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"
BINARY="" REF="HEAD" CHECK=""
while [[ $# -gt 0 ]]; do
    case "$1" in
        --binary) BINARY="$(realpath "$2")"; shift 2 ;;
        --ref)    REF="$2"; shift 2 ;;
        --check)  CHECK=1; shift ;;
        -h|--help) sed -n '2,25p' "$0"; exit 0 ;;
        -*) echo "unknown option: $1" >&2; exit 2 ;;
        *) break ;;
    esac
done
[[ $# -eq 2 || ( -n "$CHECK" && $# -eq 1 ) ]] || {
    echo "usage: $0 [--binary <irixfm>] [--ref <ref>] <version> <outdir>  |  $0 --check [--ref <ref>] <version>" >&2; exit 2; }
V="$1" OUT="${2:-}"

log() { printf '\033[1;34m==>\033[0m %s\n' "$*"; }
die() { printf '\033[1;31merror:\033[0m %s\n' "$*" >&2; exit 1; }

# semver.org 2.0.0 (no leading zeros; optional -prerelease and +build)
SEMVER='^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(-((0|[1-9][0-9]*|[0-9]*[a-zA-Z-][0-9a-zA-Z-]*)(\.(0|[1-9][0-9]*|[0-9]*[a-zA-Z-][0-9a-zA-Z-]*))*))?(\+[0-9a-zA-Z-]+(\.[0-9a-zA-Z-]+)*)?$'
[[ "$V" =~ $SEMVER ]] || die "'$V' is not a semver version (like 0.1.0 or 0.2.0-rc.1)"

cd "$REPO"
SHA="$(git rev-parse --verify "$REF^{commit}")" || die "unknown ref $REF"
FILE_V="$(git show "$SHA:VERSION" 2>/dev/null | tr -d '[:space:]')" || die "no VERSION file at $REF"
[[ "$FILE_V" == "$V" ]] || die "VERSION at $REF is '$FILE_V', not '$V': bump VERSION (and CHANGELOG.md) in a commit first"
if [[ -n "$CHECK" ]]; then echo "ok: $V matches VERSION at ${SHA:0:12}"; exit 0; fi

mkdir -p "$OUT"; OUT="$(realpath "$OUT")"
[[ -z "$(ls -A "$OUT")" ]] || die "$OUT is not empty"
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT

EPOCH="$(git log -1 --format=%ct "$SHA")"
TAR=(tar --sort=name --owner=0 --group=0 --numeric-owner --mtime="@$EPOCH" --format=gnu)
zst() { zstd -q -19 -T0 -o "$1"; }

# 1. Source tarball -------------------------------------------------------------------------
log "source: irixglass-desktop-$V.tar.gz ($SHA)"
git archive --format=tar --prefix="irixglass-desktop-$V/" "$SHA" | gzip -9n > "$OUT/irixglass-desktop-$V.tar.gz"
mkdir "$TMP/src" && tar -xzf "$OUT/irixglass-desktop-$V.tar.gz" -C "$TMP/src"
SRC="$TMP/src/irixglass-desktop-$V"

# 2. fm bundle ------------------------------------------------------------------------------
if [[ -z "$BINARY" ]]; then
    BINARY="$REPO/fm/build/irixfm"
    [[ -x "$BINARY" ]] || { log "building fm (make -C fm)"; make -C "$REPO/fm" >/dev/null; }
fi
[[ -x "$BINARY" ]] || die "no irixfm binary at $BINARY"
file -b "$BINARY" 2>/dev/null | grep -q 'ELF 64-bit.*x86-64' || die "$BINARY is not an x86_64 ELF binary"

B="irixfm-$V-x86_64"
log "fm bundle: $B.tar.zst"
mkdir -p "$TMP/$B/bin" "$TMP/$B/share/irixfm/qml" "$TMP/$B/share/irixfm/icons" \
         "$TMP/$B/share/applications" "$TMP/$B/share/dbus-1/services"
install -m755 "$BINARY" "$TMP/$B/bin/irixfm"
strip --strip-unneeded "$TMP/$B/bin/irixfm" 2>/dev/null || true
install -m644 "$SRC"/fm/qml/*.qml "$TMP/$B/share/irixfm/qml/"
install -m644 "$SRC"/fm/icons/*.svg "$TMP/$B/share/irixfm/icons/"
install -m644 "$SRC/fm/README.md" "$TMP/$B/share/irixfm/"
install -m644 "$SRC/fm/irixfm.desktop" "$TMP/$B/share/applications/"
install -m644 "$SRC/fm/org.freedesktop.FileManager1.service" "$TMP/$B/share/dbus-1/services/"
install -m644 "$SRC/LICENSE" "$TMP/$B/LICENSE"
cat > "$TMP/$B/install.sh" <<'SH'
#!/bin/sh
# Install this fm bundle into PREFIX (default ~/.local), like `make -C fm install`. No sudo.
set -eu
here="$(cd "$(dirname "$0")" && pwd)"
PREFIX="${PREFIX:-$HOME/.local}"
install -Dm755 "$here/bin/irixfm" "$PREFIX/bin/irixfm"
mkdir -p "$PREFIX/share/irixfm" "$PREFIX/share/applications" "$PREFIX/share/dbus-1/services"
cp -r "$here/share/irixfm/." "$PREFIX/share/irixfm/"
sed "s|^Exec=irixfm|Exec=$PREFIX/bin/irixfm|" "$here/share/applications/irixfm.desktop" > "$PREFIX/share/applications/irixfm.desktop"
sed "s|^Exec=irixfm|Exec=$PREFIX/bin/irixfm|" "$here/share/dbus-1/services/org.freedesktop.FileManager1.service" \
    > "$PREFIX/share/dbus-1/services/org.freedesktop.FileManager1.service"
update-desktop-database "$PREFIX/share/applications" 2>/dev/null || true
kbuildsycoca6 >/dev/null 2>&1 || true
echo "fm installed into $PREFIX (start: $PREFIX/bin/irixfm)."
[ "$PREFIX" = "$HOME/.local" ] || echo "Outside ~/.local fm needs IRIXFM_ASSETS=$PREFIX/share/irixfm to find its QML."
SH
chmod 755 "$TMP/$B/install.sh"
cat > "$TMP/$B/INSTALL" <<EOF
fm (irixfm) $V for x86_64 Arch Linux / CachyOS, KDE Plasma 6 on Wayland.
Built from https://github.com/christianmeier/irixglass-desktop at ${SHA:0:12}.

Runtime packages:
  sudo pacman -S --needed qt6-declarative qt6-multimedia qt6-webengine kio kwindowsystem kirigami \\
      karchive solid syntax-highlighting kpty libvterm plasma-integration

Install into ~/.local (bin, share/irixfm, menu entry "fm", "Show in folder" D-Bus service):
  ./install.sh                    # or PREFIX=/somewhere ./install.sh

Run without installing:
  IRIXFM_ASSETS=\$PWD/share/irixfm ./bin/irixfm

Make it the default file manager:
  xdg-mime default irixfm.desktop inode/directory

Uninstall: rm -rf ~/.local/bin/irixfm ~/.local/share/irixfm ~/.local/share/applications/irixfm.desktop \\
  ~/.local/share/dbus-1/services/org.freedesktop.FileManager1.service
EOF
"${TAR[@]}" -C "$TMP" -cf - "$B" | zst "$OUT/$B.tar.zst"

# 3. Theme parts that don't need WhiteSur -------------------------------------------------
T="irixglass-theme-$V"
log "theme: $T.tar.zst"
mkdir -p "$TMP/$T/aurorae/themes" "$TMP/$T/color-schemes"
python3 -I "$SRC/theme/gen-decoration.py" "$TMP/$T/aurorae/themes/IrixGlass" >/dev/null
install -m644 "$SRC/configs/color-schemes/IrixGlass.colors" "$TMP/$T/color-schemes/"
install -m644 "$SRC/LICENSE" "$TMP/$T/LICENSE"
cat > "$TMP/$T/INSTALL" <<EOF
IrixGlass $V: the parts of the theme that don't need WhiteSur.
The full desktop (Kvantum, GTK, Plasma pop-ups, dock and menu bar) is built from WhiteSur by
scripts/install.sh in the source tarball; see its README.

  mkdir -p ~/.local/share/aurorae/themes ~/.local/share/color-schemes
  cp -r aurorae/themes/IrixGlass ~/.local/share/aurorae/themes/
  cp color-schemes/IrixGlass.colors ~/.local/share/color-schemes/
  plasma-apply-colorscheme IrixGlass

Title bars: System Settings > Colours & Themes > Window Decorations > IrixGlass
(border size Large; buttons on the left look most like macOS).
EOF
"${TAR[@]}" -C "$TMP" -cf - "$T" | zst "$OUT/$T.tar.zst"

# 4. PKGBUILD with the source checksum ------------------------------------------------------
log "PKGBUILD"
sum="$(sha256sum "$OUT/irixglass-desktop-$V.tar.gz" | cut -d' ' -f1)"
pkgver="${V//-/_}"   # pacman versions can't contain "-"
sed -e "s/^pkgver=.*/pkgver=$pkgver/" -e "s/^sha256sums=.*/sha256sums=('$sum')/" \
    -e "s|^_src=.*|_src=\"irixglass-desktop-$V\"|" \
    -e "s|releases/download/v\${pkgver}/|releases/download/v$V/|" \
    "$SRC/packaging/arch/PKGBUILD" > "$OUT/PKGBUILD"

# 5. Checksums ------------------------------------------------------------------------------
(cd "$OUT" && sha256sum irixglass-desktop-"$V".tar.gz "$B.tar.zst" "$T.tar.zst" PKGBUILD > SHA256SUMS)

# 6. Release notes --------------------------------------------------------------------------
log "release-notes.md"
PREV="$(git describe --tags --abbrev=0 --match 'v[0-9]*' "$SHA^" 2>/dev/null || true)"
{
    # The CHANGELOG.md section of this version, if there is one
    git show "$SHA:CHANGELOG.md" 2>/dev/null | awk -v v="$V" '
        $0 ~ "^## \\[?" v "\\]?([ ]|$)" { on = 1; next }
        on && /^## / { exit }
        on { print }' | sed -e '/./,$!d'
    echo
    if [[ -n "$PREV" ]]; then
        echo "## Changes since $PREV"
        range="$PREV..$SHA"
    else
        echo "## Changes"
        range="$SHA"
    fi
    echo
    n="$(git rev-list --no-merges --count "$range")"
    git log --no-merges --format='- %s (%h)' "$range" | head -100
    if (( n > 100 )); then echo "- … and $((n - 100)) older commits"; fi
    echo
    cat <<EOF
## Assets

| File | What |
|---|---|
| \`irixglass-desktop-$V.tar.gz\` | Source of this release; run \`scripts/install.sh\` from it (see README) |
| \`$B.tar.zst\` | fm built for x86_64 Arch/CachyOS: \`tar -xf $B.tar.zst && $B/install.sh\` (see INSTALL inside) |
| \`$T.tar.zst\` | IrixGlass title bars (Aurorae) and colour scheme, without WhiteSur |
| \`PKGBUILD\` | Arch package of fm built from the source tarball: \`makepkg -si\` |
| \`SHA256SUMS\` | \`sha256sum -c SHA256SUMS\` |
EOF
} > "$OUT/release-notes.md"

log "done: $OUT"
(cd "$OUT" && ls -l && cat SHA256SUMS)
