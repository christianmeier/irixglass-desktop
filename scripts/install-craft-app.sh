#!/usr/bin/env bash
# Install a storytold Craft app AppImage: download, verify, register menu entry + icons.
# usage: install-craft-app.sh <repo> <tag> <asset> <AppDirName>
set -euo pipefail
repo=$1 tag=$2 asset=$3 name=$4
D=$HOME/Applications/$name; mkdir -p "$D"
B=https://github.com/storytold/$repo/releases/download/$tag
cd "$D"
[ -f "$asset" ] || curl -fsSL -o "$asset" "$B/$asset"
# Fail closed: no checksum list, no line for the asset or a wrong sum stops the install (and
# removes the download, so the next run fetches it again instead of trusting a bad file).
sums=$(curl -fsSL "$B/SHA256SUMS.txt") || { echo "$name: no SHA256SUMS.txt, not installing"; exit 1; }
sum=$(printf '%s\n' "$sums" | awk -v a="$asset" '{f=$2; sub(/^\*/, "", f)} f == a {print $1; exit}')
if [ -n "$sum" ] && printf '%s  %s\n' "$sum" "$asset" | sha256sum -c --status -; then echo "$name: checksum OK"
else echo "$name: CHECKSUM FAIL"; rm -f "$asset"; exit 1; fi
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
