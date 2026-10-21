# Finder features in fm

Everything macOS Finder (Tahoe) does, mapped to fm: what's there, what's missing, and how it would be
built on KDE. ⌘ maps to Ctrl, ⌥ to Alt, as KDE apps do.

Status: ✅ done · 🟡 partial · ⬜ to do · ➖ not applicable on Linux
Phase: when it gets built (1 = this branch first, 2 = next, 3 = later).

## Windows, tabs and navigation

| Finder | fm | Phase | How |
|---|---|---|---|
| Back / Forward (⌘[ ⌘]) | ✅ Ctrl+[ / Ctrl+], Alt+←/→ | | history stacks |
| Enclosing folder (⌘↑), open selection (⌘↓) | ✅ Ctrl+↑ / Ctrl+↓ | | |
| Go menu: Recents, Documents, Desktop, Downloads, Home, Computer, Network, Applications, Utilities (⇧⌘ shortcuts) | ✅ Ctrl+Shift+D/O/A/C/H/K, Ctrl+Alt+L (no Recents/Utilities yet) | | |
| Go to Folder (⇧⌘G) with completion | ✅ | | path bar |
| Recent folders (Go ▸ Recent Folders) | ✅ Go ▸ Recent Folders + ⌄ menu, saved, Clear Menu | | |
| Path bar, proxy icon drag (title bar) | ✅ crumbs + drop pocket | | |
| New window (⌘N), close (⌘W) | ✅ | | |
| Tabs (⌘T, ⇧⌘[ ], drag tabs out, Merge All Windows) | ✅ IRIX notebook tabs at the bottom; folders and places open in tabs; reopen, move, to new window; session restore (no drag-out, no merge) | | active tab = live window state, others are snapshots |
| Open folder in new tab/window (⌘-double-click) | ✅ opening a folder gives it a tab; middle-click = background tab; Ctrl+double-click = same tab | | |
| Spring-loaded folders (hover while dragging opens) | ✅ icons, list rows and sidebar (0.9 s) | | DropArea + timer |
| New window opens at… (Settings) | ⬜ | 3 | settings window |

## Sidebar

| Finder | fm | Phase | How |
|---|---|---|---|
| Favorites / Locations, add / remove / reorder | ✅ (plus Indigo Magic) | | editable `places` |
| Show / hide sidebar (⌃⌘S) | ✅ Ctrl+Alt+S and the sidebar button, saved | | content slides left |
| Recents | ⬜ | 3 | `~/.local/share/recently-used.xbel` |
| Trash with Empty Trash / Put Back | ✅ sidebar, Empty (button, menu, Ctrl+Shift+Del), Put Back, Delete Immediately | | home trash folder + KIO restore / emptyTrash |
| Removable drives and mounted volumes, Eject | ✅ in Locations, mount on click, ⏏ | | Solid |
| Network / servers (Connect to Server ⌘K) | 🟡 opens Dolphin | 3 | needs a KIO-based model (`KDirModel`) |
| Tags section | ✅ see [TAGS.md](TAGS.md) | | `user.xdg.tags` xattr (Baloo-compatible) |
| iCloud, AirDrop, Shared | ➖ | | (KDE Connect could stand in for AirDrop) |

## Views

| Finder | fm | Phase | How |
|---|---|---|---|
| Icon view (⌘1), icon size slider | ✅ thumbwheel | | |
| List view (⌘2) | ✅ | | |
| Sort by clicking a list column, ascending / descending | ✅ | | `FolderListModel.sortField` / `sortReversed` |
| View ▸ Sort By (Name, Kind, Date, Size), also in icon view | ✅ View menu + right-click, saved | | |
| Disclosure triangles in list view (expand folders inline) | ⬜ | 3 | tree model |
| Column view (⌘3) | ⬜ | 3 | row of ListViews, one per level, with preview column |
| Gallery view (⌘4) | 🟡 "previews" | 3 | big preview + thumbnail strip + metadata |
| Group By / Use Stacks | ⬜ | 3 | sections in the view |
| Show View Options (⌘J): text size, grid spacing, label position, item info | ⬜ | 3 | panel; icon size already on the thumbwheel |
| Calculate all sizes (folder sizes in list) | ✅ View ▸ Calculate All Sizes (on by default) | | `KIO::DirectorySizeJob`, 3 at a time |
| Show hidden files (⇧⌘.) | ✅ Ctrl+Shift+. and Ctrl+H | | |
| Show / hide toolbar, status bar, path bar | 🟡 always on | 3 | |
| Preview pane (⇧⌘P) with metadata and quick actions | ✅ Ctrl+Shift+P: picture / PDF page / text / icon, details, Open, Get Info | | |

## Selecting and opening

| Finder | fm | Phase | How |
|---|---|---|---|
| Click, ⌘-click, ⇧-click, drag a box, ⌘A | ✅ | | |
| Arrow keys | ✅ | | |
| Type to select (type a name to jump to it) | ✅ | | prefix buffer with a 1 s timeout; Space continues a name |
| Open (⌘O / ⌘↓ / double-click) | ✅ | | KIO `OpenUrlJob` |
| Open With ▸ (apps for the type, Other…) | ✅ | | `KApplicationTrader` + `ApplicationLauncherJob` |
| Quick Look (Space): images, text, PDF, audio/video, arrows to step, full screen | ✅ (no full screen yet) | | glass overlay: Image / text / Qt PDF / Qt Multimedia / folder size |
| Show Original of an alias (⌘R) | ✅ Ctrl+R | | follow the symlink, select the target |

## File operations

| Finder | fm | Phase | How |
|---|---|---|---|
| New Folder (⇧⌘N) | ✅ | | |
| New Folder with Selection (⌃⌘N) | ⬜ | 3 | mkdir + move |
| Rename (Return in Finder, F2 here) | ✅ | | |
| Rename several items (find & replace, add text, numbering) | ⬜ | 3 | dialog + `KIO::moveAs` per item |
| Duplicate (⌘D) | ✅ Ctrl+D | | `KIO::copyAs` to "name copy", "name copy 2" |
| Make Alias | ✅ menu (Ctrl+L stays the path bar, as in Dolphin) | | `KIO::linkAs` to "name alias" |
| Copy / Paste (⌘C / ⌘V), Move (⌥⌘V) | ✅ Ctrl+C / Ctrl+V / Ctrl+Alt+V | | |
| Drag: move / copy (⌥) / alias (⌥⌘) | 🟡 copy, Shift moves | 3 | Finder semantics: same disk moves |
| Move to Trash (⌘⌫) | ✅ Del, Ctrl+Backspace | | |
| Undo / Redo (⌘Z / ⇧⌘Z) for move, copy, rename, trash, new folder | 🟡 Undo (Ctrl+Z); no Redo | 3 | `KIO::FileUndoManager` (it has no redo) |
| Get Info (⌘I): kind, size (folders counted), where, dates, permissions, Open with | ✅ Ctrl+I, one window per item (read-only for now) | 2 | editing name/permissions/Open with later |
| Compress (zip) | ✅ "name.zip" / "Archive.zip", plus Expand Here (zip, tar.*, 7z) | | KArchive in a background thread |
| Copy path (⌥⌘C) | ✅ Ctrl+Alt+C | | |
| Share ▸ | ⬜ | 3 | KDE Purpose |
| Quick Actions (rotate, markup, PDF) | ⬜ | 3 | |
| Tags (colour dots on items, tag menu) | ✅ Nord colours, dots + tinted carpets, Ctrl+Alt+1…7 | | xattr `user.xdg.tags` |
| Open in Terminal (service) | ✅ | | |
| Eject, Burn | ✅ Eject / ➖ Burn | | |

## Search

| Finder | fm | Phase | How |
|---|---|---|---|
| Filter the current folder as you type | ✅ | | |
| Search "This Mac" / current folder recursively, scope bar | ✅ scope bar: this folder / with subfolders / Home; results with their folder | 3 | Baloo for content search later |
| Search tokens (Kind:, Date:, Tags:) | 🟡 `tag:Name` | 3 | Kind:, Date: later |
| Saved searches / Smart Folders | ⬜ | 3 | |

## Phase 1 (built on branch `finder-features`): done

1. Quick Look (Space).
2. Sort By (list column headers + View menu), saved.
3. Get Info (Ctrl+I).
4. Duplicate (Ctrl+D), Make Alias, Show Original (Ctrl+R), Move clipboard items (Ctrl+Alt+V).
5. Undo / Redo (Ctrl+Z / Ctrl+Shift+Z).
6. Type to select.
7. Finder shortcuts: Ctrl+↑/↓, Ctrl+[ / ], Ctrl+Backspace, Ctrl+Shift+. , Ctrl+Alt+C, Go shortcuts.

All of it is checked by `tests/finder_test.cpp` in `make test` (21 checks, real key presses).

## Phase 2 (branch `finder-phase2`): done

Trash, Open With, removable drives with Eject, spring-loaded folders, hide sidebar, preview pane,
search through subfolders (this also fixed search: folders are filtered now), folder sizes in the
list, Compress / Expand, Recent Folders. Checked by `tests/phase2_test.cpp` (34 checks).

## Phase 3 (later)

Column view, gallery view, Group By, Recents (files), New Folder with Selection,
Finder drag semantics, batch rename, Get Info editing, Share, content search (Baloo), settings window.
