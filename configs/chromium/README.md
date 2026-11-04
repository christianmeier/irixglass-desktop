# Chromium in the IrixGlass look

- `irixglass-theme/`: a Chromium theme extension (manifest v3, colours only, plus a 1-2 px white
  highlight at the top edge of the frame, `images/frame_overlay.png`). The tab strip uses glass
  #1e2029 (inactive #1b1d25). The toolbar and active tab are white .035 over it (#262831), the
  address capsule is white .07 (#30323b), text is white .9 / .55, the new-tab page is #181a22
  and links are #0a84ff.
- `chromium-flags.conf`: copy it to `~/.config/chromium-flags.conf`. It loads the theme, turns off
  Wayland colour management (which lifts dark tones) and turns on overlay scrollbars.

Install (Chromium closed):

    mkdir -p ~/.local/share/irixglass
    rm -rf ~/.local/share/irixglass/chromium-theme
    cp -r configs/chromium/irixglass-theme ~/.local/share/irixglass/chromium-theme
    cp configs/chromium/chromium-flags.conf ~/.config/chromium-flags.conf

Don't load it from the repo: Chromium writes `Cached Theme.pak` into the folder it loads. To update
the theme, copy it again (this removes the cache). Chromium keeps a stale cache even after a version bump.

Installing the theme sets `extensions.theme.system_theme` to 0, so GTK/Qt mode is off. Caption buttons
are then Chromium's own (× − □, placed according to the GTK decoration layout), not GTK-drawn.
Choosing "Use GTK"/"Use QT" in settings drops the theme until the next start (`--load-extension` re-applies it).

Alternative without flags (tested): pack a .crx (`chromium --pack-extension=<dir>`, keep the .pem so
the ID stays the same) and put `{"external_crx": "<abs path>.crx", "external_version": "1.0"}` in
`~/.config/chromium/External Extensions/<id>.json`. Chromium installs it on the next start, enabled
and applied, and copies it into the profile.
