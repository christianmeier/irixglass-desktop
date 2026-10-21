# Tags (colour labels)

## How macOS Finder does it

- **Seven default tags**, named after their colour: Red, Orange, Yellow, Green, Blue, Purple, Gray.
  Their names can be changed; any number of extra tags (with or without a colour) can be added.
- **Assigning**: right-click an item → a row of colour dots at the bottom of the menu (click a dot
  to add / remove that tag), plus **Tags…** for a field with all tags and completion. Also from
  the toolbar's Tags button, in Get Info, in the Save dialog, and by dragging items onto a tag in
  the sidebar.
- **Several tags per item.** Items show small overlapping colour dots next to their name (icon,
  list and column view). In list view there's an optional Tags column.
- **Sidebar "Tags" section**: one entry per tag (dot + name). Clicking one shows every item with
  that tag, from anywhere on the Mac (a Spotlight query). Right-click a tag: Rename, Delete Tag,
  Remove from Sidebar, Open in New Tab, and a colour picker. Finder Settings ▸ Tags chooses which tags
  appear in the sidebar and their order (drag to reorder) and which ones appear in the menu.
- **Search** understands tags (`tag:red`), and tags can be the subject of a Smart Folder.
- **Storage**: an extended attribute on the file (`com.apple.metadata:_kMDItemUserTags`), so tags
  stay with the file when it's moved or copied; Spotlight indexes them.
- **Keyboard**: none by default (third-party tools add them).

## How fm does it — and what it does better

| | fm |
|---|---|
| Colours | The **Nord** palette: Red `#BF616A`, Orange `#D08770`, Yellow `#EBCB8B`, Green `#A3BE8C`, Blue `#5E81AC`, Purple `#B48EAD`, Gray `#4C566A` by default, plus the Frost colours Teal `#8FBCBB`, Cyan `#88C0D0` and Light Blue `#81A1C1` to choose from |
| Storage | The freedesktop / KDE standard xattr **`user.xdg.tags`** (comma-separated). Dolphin, Baloo and `tags:/` see the same tags; KIO keeps them on copy and move |
| Assigning | Right-click ▸ **Tags** (checkable colour entries, New Tag…, Remove All Tags); drag items onto a tag in the sidebar; **Ctrl+Alt+1…7** toggle the first seven tags, **Ctrl+Alt+0** removes all (Finder has no shortcuts) |
| Showing | Colour dots after the name in icon, list and search views; **better**: the IRIX "magic carpet" under a tagged icon takes the tag's colour, so tagged items stand out at any icon size |
| Sidebar | **Tags** section under Locations: click shows everything with that tag; hover ⊖ hides it from the sidebar; double-click renames; right-click: Rename, Change Colour ▸, Move Up / Down, Remove from Sidebar, Delete Tag…, Show All Tags; **+** on the header makes a new tag |
| Finding | `tag:Name` in the search field, or a click in the sidebar. Results come from **Baloo** (which indexes `user.xdg.tags`) **plus fm's own tag index** (`$XDG_DATA_HOME/irixfm/tags.json`), so tagged files are found even in hidden or unindexed folders; every hit is re-checked against the file |
| Renaming / deleting a tag | Applied to every file that has it (Finder does the same) |
| Get Info, preview pane | Show the item's tags |

Tag names can't contain commas (the xattr is comma-separated).
