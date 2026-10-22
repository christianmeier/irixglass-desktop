#!/usr/bin/env bash
# Install a storytold Craft app AppImage: download, verify, register menu entry + icons.
# usage: install-craft-app.sh <repo> <tag> <asset> <AppDirName>
set -euo pipefail
repo=$1 tag=$2 asset=$3 name=$4
D=$HOME/Applications/$name; mkdir -p "$D"
B=https://github.com/storytold/$repo/releases/download/$tag
cd "$D"
[ -f "$asset" ] || curl -fsSL -o "$asset" "$B/$asset"
sums=$(curl -fsSL "$B/SHA256SUMS.txt" 2>/dev/null || true)
if [ -n "$sums" ]; then echo "$sums" | grep " \*\?$asset\$" | sed 's/\*//' | sha256sum -c - >/dev/null && echo "$name: checksum OK" || { echo "$name: CHECKSUM FAIL"; exit 1; }
else echo "$name: no SHA256SUMS"; fi
chmod +x "$asset"
X=$(mktemp -d); cd "$X"
"$D/$asset" --appimage-extract '*.desktop' >/dev/null 2>&1 || true
"$D/$asset" --appimage-extract 'usr/share/icons/*' >/dev/null 2>&1 || true
desk=$(ls squashfs-root/*.desktop | head -1)
id=$(basename "$desk")
cp -r squashfs-root/usr/share/icons/hicolor/. "$HOME/.local/share/icons/hicolor/" 2>/dev/null || true
sed -e "s|^Exec=[^ ]*|Exec=\"$D/$asset\"|" -e '/^TryExec=/d' "$desk" > "$HOME/.local/share/applications/$id"
echo "$name: $id -> $(grep -m1 ^Name= "$desk")"
rm -rf "$X"
# Remove older versions of this app (a running one keeps working until it quits).
find "$D" -maxdepth 1 -name '*.AppImage' ! -name "$asset" -delete
