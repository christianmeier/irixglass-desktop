import QtQuick
import QtQuick.Window
import QtQuick.Shapes
import QtQuick.Controls
import QtCore
import Qt.labs.folderlistmodel
import Qt.labs.platform as P
import org.kde.kirigami as K

// fm: the IRIX Indigo Magic file manager, rebuilt with a macOS Tahoe glass skin.
// Geometry follows the mockup (docs/mockup.png) in mockup pixels times `u`.
Window {
    id: win

    // ---- state ---------------------------------------------------------------
    property real u: 0.85                       // mockup px -> logical px (layout)
    // Text, rows and buttons follow the system font so fm matches other windows.
    readonly property real systemTextPx: Qt.application.font.pixelSize > 0 ? Qt.application.font.pixelSize
                                                                          : Qt.application.font.pointSize * 96 / 72
    readonly property real tu: systemTextPx / 13.5      // mockup text px -> logical px
    readonly property real r: maximized ? 0 : 26 * u
    readonly property bool maximized: visibility === Window.Maximized || visibility === Window.FullScreen
    property string dir: Fm.startDir
    property var backStack: []
    property var fwdStack: []
    property var recent: []                     // Recent Folders (loaded from recentJson at start)
    property var selection: ({})                // path -> true
    property int anchorIndex: -1
    property string renaming: ""
    property bool editingPath: false
    // Sidebar: sections of places, saved as JSON in the settings. Paths may start with "~".
    // Items have a path (a folder) or an action (overview, find, network).
    property string placesJson: ""
    readonly property var places: placesJson ? JSON.parse(placesJson) : defaultPlaces()
    property string renamingPlace: ""           // "si/ii" of the sidebar entry being renamed
    property int sortField: FolderListModel.Name   // Name, Time, Size, Type
    property bool sortReversed: false
    property string quickLook: ""               // path shown in Quick Look ("" = closed)
    property var pendingSelect: []              // paths to select once they appear (duplicate, alias, typed file)
    property string typeAhead: ""               // type-to-select buffer
    // Phase 2
    property bool sidebarShown: true
    property bool previewShown: false
    property bool calcSizes: true               // folder sizes in the list view
    property var folderSizes: ({})              // path -> size text, filled by Fm.folderSizeReady
    property string recentJson: "[]"            // Recent Folders, saved
    property int searchScope: 1                 // 0 this folder, 1 with subfolders, 2 home
    property bool trashAdded: false             // one-time: Trash added to the saved sidebar
    property var searchResults: []              // [{name, path, where, isDir, modified, size}]
    property bool searching: false
    property bool searchTruncated: false
    property var volumes: Fm.volumes            // removable drives (Solid); a property so tests can fake one
    readonly property bool searchActive: search.text.length > 0
    // ---- tags (docs/TAGS.md): names and Nord colours here, the tags themselves on the files (user.xdg.tags)
    property string tagsJson: ""
    property int tagRev: 0                      // bumped when tags change, so views re-read them
    property string renamingTag: ""
    // ---- tabs: the active tab's state is the live window state (dir, history, selection, search);
    // the other tabs are snapshots in `tabs`, swapped in by switchTab().
    property var tabs: [{ dir: Fm.startDir, back: [], fwd: [], sel: [], anchor: -1, search: "", scrollY: 0 }]
    property int tabIndex: 0
    property bool tabBarShown: true
    property bool folderTabs: true              // opening a folder / place adds a tab (or switches to its tab)
    // ---- split view (F3): a second side with its own folder; drag between them moves (Ctrl copies)
    property bool splitView: false
    property string splitDir: ""
    property int activePane: 0                  // 0 = the main side, 1 = the split side (F5 / F6 go to the other)
    property bool internalDrag: false           // a drag that started inside fm (move by default)
    readonly property real contentAreaW: width - (240 * u - contentShift) - previewW - 10 * u
    property real splitW: splitView ? contentAreaW / 2 : 0
    readonly property bool compactList: list.width < 640 * u   // narrow list (split view): no Date column
    Behavior on splitW { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }
    property var closedTabs: []                 // for Reopen Closed Tab
    property real pendingScrollY: -1
    property bool waitForLoad: false            // the folder model still shows the previous folder
    property string tabsJson: ""                // saved: [{dir, back, fwd, search}]
    property int savedTabIndex: 0
    readonly property real tabBarH: tabBarShown ? 26 * tu : 0
    readonly property real bottomH: 52 * u + tabBarH   // tab bar + status bar
    readonly property var tabModel: tabs.map((t, i) => ({ title: tabTitle(i), dir: i === tabIndex ? dir : t.dir }))
    // Collapsed sidebar sections (by title), saved.
    property string collapsedJson: "[]"
    readonly property var collapsed: { try { return JSON.parse(collapsedJson) } catch (e) { return [] } }
    readonly property var nordColors: [["Red", "#BF616A"], ["Orange", "#D08770"], ["Yellow", "#EBCB8B"], ["Green", "#A3BE8C"],
                                       ["Teal", "#8FBCBB"], ["Cyan", "#88C0D0"], ["Light Blue", "#81A1C1"], ["Blue", "#5E81AC"],
                                       ["Purple", "#B48EAD"], ["Gray", "#4C566A"]]
    readonly property var tagDefs: tagsJson ? JSON.parse(tagsJson) : defaultTags()
    readonly property string searchTag: { const m = search.text.match(/^tag:\s*(.+)$/i); return m ? m[1].trim() : "" }
    readonly property bool inTrash: dir === Fm.trashDir || dir.startsWith(Fm.trashDir + "/")
    // Layout: the sidebar slides away (sideShift) and the preview pane takes room on the right.
    readonly property int springDelay: 900      // ms hovering with a drag before a folder opens
    readonly property real hiddenShift: 98 * u   // how far the toolbar/content move left without the sidebar
    property real sideShift: sidebarShown ? 0 : hiddenShift
    // Without the sidebar the files use the whole window width: content moves to the left edge
    // (the toolbar only moves by sideShift, to keep room for the traffic lights).
    property real contentShift: sidebarShown ? 0 : 230 * u
    Behavior on contentShift { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }
    Behavior on sideShift { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }
    property real previewW: previewShown ? 280 * u : 0
    Behavior on previewW { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }
    readonly property real scopeH: searchActive ? 34 * tu : 0
    readonly property real defaultWheel: .352      // the standard icon size (the IRIX home position)
    property real wheelValue: defaultWheel      // thumbwheel 0..1
    readonly property real iconScale: .55 + wheelValue * 1.05
    readonly property real k: u * iconScale     // px per icon unit (icons are drawn in a 100-unit box)
    // List and search results: the icon follows the thumbwheel too (26 px at the default size, at least
    // 16 px); rows grow with it, text stays at the system size.
    readonly property real listIcon: Math.max(16, 26 * u * iconScale / .92)
    readonly property real listRowH: Math.max(28 * tu, listIcon + 6 * u)
    readonly property real listTextX: listIcon + 14 * u
    property int viewMode: 0                    // 0 icons, 1 list, 2 previews
    property bool showHidden: false
    readonly property string fontFamily: "SF Pro Text"
    readonly property color sidebarTint: Qt.rgba(42 / 255, 45 / 255, 56 / 255, .35)
    readonly property color bodyTint: Qt.rgba(20 / 255, 22 / 255, 29 / 255, .62)

    width: 1300 * u
    height: 830 * u
    minimumWidth: 760 * u
    minimumHeight: 480 * u
    visible: true
    color: "transparent"
    flags: Qt.Window | Qt.FramelessWindowHint
    title: tabTitle(tabIndex) + " — fm"

    Settings {
        id: settings
        category: "fm"
        property alias wheelValue: win.wheelValue
        property alias viewMode: win.viewMode
        property alias showHidden: win.showHidden
        property alias places: win.placesJson
        property alias sortField: win.sortField
        property alias sortReversed: win.sortReversed
        property alias sidebarShown: win.sidebarShown
        property alias previewShown: win.previewShown
        property alias calcSizes: win.calcSizes
        property alias recent: win.recentJson
        property alias searchScope: win.searchScope
        property alias trashAdded: win.trashAdded
        property alias tags: win.tagsJson
        property alias collapsedSections: win.collapsedJson
        property alias tabs: win.tabsJson
        property alias activeTab: win.savedTabIndex
        property alias tabBarShown: win.tabBarShown
        property alias folderTabs: win.folderTabs
        property alias splitView: win.splitView
        property alias splitDir: win.splitDir
        property alias width: win.width
        property alias height: win.height
    }

    function asset(name) { return "file://" + Fm.assetDir + "/icons/" + name + ".svg" }
    function shape() { Fm.updateWindow(win, Math.round(r), !maximized) }
    onWidthChanged: shape()
    onHeightChanged: shape()
    onVisibilityChanged: shape()
    Component.onCompleted: {
        Qt.callLater(shape)
        // Started from the dock or menu (no folder given): bring back the last session's tabs.
        if (Fm.restoreSession && tabsJson) {
            let saved = []
            try { saved = JSON.parse(tabsJson) } catch (e) {}
            saved = saved.filter(t => t && t.dir && Fm.resolvePath(t.dir, "/").ok)
            if (saved.length) {
                tabs = saved.map(t => ({ dir: t.dir, back: t.back || [], fwd: t.fwd || [], sel: [], anchor: -1, search: t.search || "", scrollY: 0 }))
                tabIndex = -1
                loadTab(Math.max(0, Math.min(saved.length - 1, savedTabIndex)))
            }
        }
        // Older saved sidebars don't have Trash yet: add it once to Locations.
        if (!trashAdded) {
            const p = copyPlaces(), li = Math.max(0, p.findIndex(sec => sec.title === "Locations"))
            if (!p.some(sec => sec.items.some(it => it.action === "trash"))) { p[li].items.push({ label: "Trash", action: "trash" }); setPlaces(p) }
            trashAdded = true
        }
    }

    onRecentJsonChanged: if (!recent.length) { try { recent = JSON.parse(recentJson) } catch (e) { recent = [] } }
    function clearRecent() { recent = []; recentJson = "[]" }

    // ---- navigation ----------------------------------------------------------
    function go(path, fromHistory) {
        if (!path || path === dir) return
        pendingScrollY = -1; pendingSelect = []
        if (!fromHistory) { backStack = backStack.concat([dir]); fwdStack = [] }
        dir = path
        selection = ({}); anchorIndex = -1; renaming = ""
        search.text = ""
        recent = [path].concat(recent.filter(p => p !== path)).slice(0, 12)
        recentJson = JSON.stringify(recent)
        view().positionViewAtBeginning()
    }
    function goBack() { if (backStack.length) { fwdStack = [dir].concat(fwdStack); const p = backStack[backStack.length - 1]; backStack = backStack.slice(0, -1); go(p, true) } }
    function goForward() { if (fwdStack.length) { backStack = backStack.concat([dir]); const p = fwdStack[0]; fwdStack = fwdStack.slice(1); go(p, true) } }
    function goUp() { if (dir !== "/") go(dir.substring(0, dir.lastIndexOf("/")) || "/") }
    function activate(path, isDir) { isDir ? openFolder(path, false) : Fm.open(path) }
    // Opening a folder or a sidebar place gives it a tab: an existing tab showing it, or a new one.
    // Moving around inside a tab (back, path bar, crumbs, spring-loading) stays in the tab (go()).
    function openFolder(path, sameTab) {
        if (folderTabs && !sameTab) openTabFor(path, "")
        else go(path)
    }
    function findTab(path, searchText) {
        const tag = searchText.toLowerCase().startsWith("tag:")   // a tag view is the same anywhere
        for (let i = 0; i < tabs.length; ++i) {
            const t = i === tabIndex ? { dir: dir, search: search.text } : tabs[i]
            const sameSearch = (t.search || "").toLowerCase() === searchText.toLowerCase()
            if (sameSearch && (tag || t.dir === path)) return i
        }
        return -1
    }
    function openTabFor(path, searchText) {
        const i = findTab(path, searchText)
        if (i === tabIndex) return
        if (i >= 0) { switchTab(i); return }
        newTab(path, true)
        if (searchText) search.text = searchText
    }

    // ---- split view ------------------------------------------------------------------
    function toggleSplit() {
        if (!splitView && !(splitDir && Fm.resolvePath(splitDir, "/").ok)) { splitDir = dir; splitPane.dir = dir }
        splitView = !splitView
        activePane = 0
    }
    // Drops: drags from inside fm move on the same disk (Ctrl copies), others follow the drag's action.
    function dropFiles(urls, target, proposedMove) {
        if (inTrash && target === dir && activePane === 0) return
        if (internalDrag) Fm.dropInternal(urls, target)
        else Fm.drop(urls, target, proposedMove)
    }
    // F5 / F6: the active side's selection to the other side.
    function sendToOtherSide(move) {
        if (!splitView) return
        if (activePane === 0) { if (paths().length) Fm.copyOrMove(paths(), splitPane.dir, move) }
        else if (splitPane.paths().length) Fm.copyOrMove(splitPane.paths(), dir, move)
    }

    // ---- tabs ----------------------------------------------------------------------
    function tabTitleOf(t) {
        const s = t.search || ""
        const m = s.match(/^tag:\s*(.+)$/i)
        if (m) return tagDef(m[1].trim()) ? tagDef(m[1].trim()).name : m[1].trim()
        if (s) return "Search “" + s + "”"
        if (t.dir === Fm.trashDir || t.dir.startsWith(Fm.trashDir + "/")) return "Trash"
        if (t.dir === Fm.home) return Fm.userName
        if (t.dir === "/") return "cachyOS"
        return t.dir.split("/").pop()
    }
    function tabTitle(i) {
        if (i === tabIndex) return tabTitleOf({ dir: dir, search: search.text })
        return tabs[i] ? tabTitleOf(tabs[i]) : ""
    }
    function snapshot() {
        return { dir: dir, back: backStack.slice(-20), fwd: fwdStack.slice(0, 20), sel: paths(), anchor: anchorIndex,
                 search: search.text, scrollY: view().contentY }
    }
    function saveTab() {
        if (tabIndex < 0 || tabIndex >= tabs.length) return
        const t = tabs.slice(); t[tabIndex] = snapshot(); tabs = t
    }
    function persistTabs() {
        if (!Fm.restoreSession) return    // a window opened on a folder (xdg-open, Ctrl+N) leaves the saved tabs alone
        tabsJson = JSON.stringify(tabs.map((t, i) => {
            const x = i === tabIndex ? snapshot() : t
            return { dir: x.dir, back: x.back.slice(-20), fwd: x.fwd.slice(0, 20), search: x.search }
        }))
        savedTabIndex = tabIndex
        settings.sync()
    }
    // Makes tab i live: close transient things, restore its folder, history, search, selection, scroll.
    function loadTab(i) {
        const t = tabs[i]
        if (!t) return
        renaming = ""; quickLook = ""; typeAhead = ""; renamingPlace = ""; renamingTag = ""
        if (editingPath) cancelPath()
        pendingSelect = []; pendingScrollY = -1; selection = ({}); anchorIndex = -1
        tabIndex = i
        backStack = t.back || []; fwdStack = t.fwd || []
        // A different folder: the model reports Ready for the old one until it starts loading.
        waitForLoad = t.dir !== dir
        dir = t.dir
        search.text = t.search || ""
        searchDelay.stop()                // the text change would re-run (and clear the selection) later
        if (t.search) runSearch()         // its own folder: two tabs can share the same search text
        if (!t.search) {
            const sel = (t.sel || []).filter(p => Fm.resolvePath(p, "/").ok)
            if (sel.length) selectWhenReady(sel)
            pendingScrollY = t.scrollY || 0
            applyPendingScroll()
        }
        persistTabs()
        keys.forceActiveFocus()
    }
    function applyPendingScroll() {
        if (pendingScrollY < 0 || pendingSelect.length || waitForLoad) return
        if (folder.status !== FolderListModel.Ready) return
        const v = view(), y = pendingScrollY
        pendingScrollY = -1
        Qt.callLater(() => { v.contentY = Math.max(v.originY, Math.min(y, v.originY + v.contentHeight - v.height)) })
    }
    function switchTab(i) {
        if (i === tabIndex || i < 0 || i >= tabs.length) return
        saveTab()
        loadTab(i)
    }
    function cycleTab(delta) { if (tabs.length > 1) switchTab((tabIndex + delta + tabs.length) % tabs.length) }
    // New tabs go at the end, like Finder; `activate` switches to it.
    function newTab(path, activate) {
        saveTab()
        tabs = tabs.concat([{ dir: path, back: [], fwd: [], sel: [], anchor: -1, search: "", scrollY: 0 }])
        tabBarShown = true
        if (activate) loadTab(tabs.length - 1); else persistTabs()
    }
    // Each folder in `paths` opens in a new tab, in order (the last one becomes active).
    function openInNewTab(paths, activate) {
        const dirs = paths.filter(p => Fm.kind(p) === "folder")
        dirs.forEach((p, k) => newTab(p, activate && k === dirs.length - 1))
    }
    function closeTab(i) {
        if (tabs.length <= 1) { win.close(); return }
        saveTab()
        closedTabs = closedTabs.concat([tabs[i]]).slice(-20)
        const t = tabs.slice(); t.splice(i, 1)
        const wasActive = i === tabIndex
        let next = tabIndex > i ? tabIndex - 1 : tabIndex
        tabs = t
        if (wasActive) { tabIndex = -1; loadTab(Math.min(i, t.length - 1)) }
        else { tabIndex = next; persistTabs() }
    }
    function closeOtherTabs(i) {
        saveTab()
        const keep = tabs[i]
        closedTabs = closedTabs.concat(tabs.filter((t, k) => k !== i)).slice(-20)
        tabs = [keep]
        if (i === tabIndex) { tabIndex = 0; persistTabs() } else { tabIndex = -1; loadTab(0) }
    }
    function reopenClosedTab() {
        if (!closedTabs.length) return
        const t = closedTabs[closedTabs.length - 1]
        closedTabs = closedTabs.slice(0, -1)
        saveTab()
        const all = tabs.slice(); all.splice(tabIndex + 1, 0, t); tabs = all
        loadTab(tabIndex + 1)
    }
    function moveTab(from, to) {
        if (from === to || from < 0 || to < 0 || from >= tabs.length || to >= tabs.length) return
        saveTab()
        const t = tabs.slice(), [x] = t.splice(from, 1)
        t.splice(to, 0, x)
        tabIndex = tabIndex === from ? to : (from < tabIndex && to >= tabIndex ? tabIndex - 1 : from > tabIndex && to <= tabIndex ? tabIndex + 1 : tabIndex)
        tabs = t
        persistTabs()
    }
    // Navigating inside a tab keeps its snapshot (and the saved session) current.
    // (deferred: go() and loadTab() change dir before they reset the search and selection)
    function syncTab() { if (tabIndex >= 0 && tabIndex < tabs.length) { saveTab(); persistTabs() } }
    onDirChanged: Qt.callLater(syncTab)

    // Editable path bar (click the path, Ctrl+L, Ctrl+Shift+G, or type / or ~ in the file view).
    function editPath(initial) {
        const typed = typeof initial === "string"
        editingPath = true
        pathInput.text = typed ? initial : (dir === "/" ? "/" : dir + "/")
        pathInput.forceActiveFocus()
        if (!typed) pathInput.selectAll()
        else pathInput.cursorPosition = pathInput.text.length
    }
    function cancelPath() { editingPath = false; keys.forceActiveFocus() }
    // Returns false (and flashes the bar) when the path doesn't exist; the field stays open then.
    function submitPath(text) {
        const r = Fm.resolvePath(text, dir)
        if (!r.ok) { pathError.restart(); return false }
        editingPath = false
        keys.forceActiveFocus()
        go(r.dir)
        if (r.file) selectWhenReady([r.file])
        return true
    }
    // The views show the folder, or the search / tag results (same roles: fileName, filePath, …).
    readonly property int itemCount: searchActive ? resultsModel.count : folder.count
    function itemAt(i, role) {
        if (!searchActive) return folder.get(i, role)
        const r = resultsModel.get(i)
        return r ? r[role] : undefined
    }
    function indexOfPath(p) {
        if (!searchActive) return folder.indexOf("file://" + p)
        for (let i = 0; i < resultsModel.count; ++i) if (resultsModel.get(i).filePath === p) return i
        return -1
    }
    ListModel { id: resultsModel; objectName: "resultsModel" }

    function selectWhenReady(paths) { pendingSelect = paths; selectPending() }
    // Selects the pending paths as soon as they're all in the folder model.
    function selectPending() {
        if (!pendingSelect.length) return
        const idx = pendingSelect.map(p => indexOfPath(p))
        if (idx.some(i => i < 0)) return
        const s = ({})
        for (const p of pendingSelect) s[p] = true
        pendingSelect = []
        selection = s
        anchorIndex = idx[0]
        if (pendingScrollY >= 0) { applyPendingScroll(); return }   // a restored tab: its own scroll position
        view().currentIndex = idx[0]
        view().positionViewAtIndex(idx[0], ListView.Contain)
    }

    // ---- selection -----------------------------------------------------------
    function view() { return viewMode === 1 ? list : grid }
    function paths() { return Object.keys(selection) }
    function select(index, path, mods) {
        let s = Object.assign({}, selection)
        if (mods & Qt.ShiftModifier && anchorIndex >= 0) {
            s = ({})
            for (let i = Math.min(anchorIndex, index); i <= Math.max(anchorIndex, index); ++i) s[itemAt(i, "filePath")] = true
        } else if (mods & Qt.ControlModifier) {
            if (s[path]) delete s[path]; else s[path] = true
            anchorIndex = index
        } else {
            s = ({}); s[path] = true; anchorIndex = index
        }
        selection = s
        view().currentIndex = index
    }
    function selectAll() { const s = ({}); for (let i = 0; i < itemCount; ++i) s[itemAt(i, "filePath")] = true; selection = s }
    function moveCursor(delta) {
        if (!itemCount) return
        const i = Math.max(0, Math.min(itemCount - 1, (anchorIndex < 0 ? -delta : anchorIndex) + delta))
        select(i, itemAt(i, "filePath"), 0)
        view().positionViewAtIndex(i, ListView.Contain)
    }
    // Touchpads send pixel deltas (smooth 1:1 scrolling); mouse wheels send 120 per notch.
    function scroll(v, event) {
        const dy = event.pixelDelta.y !== 0 ? event.pixelDelta.y : event.angleDelta.y / 2
        const top = v.originY - v.topMargin
        const bottom = Math.max(top, v.originY + v.contentHeight + v.bottomMargin - v.height)
        v.contentY = Math.max(top, Math.min(bottom, v.contentY - dy))
    }
    // ---- sidebar places ----------------------------------------------------------
    function defaultPlaces() {
        return [
            { title: "Favorites", color: "#0a84ff", items: [
                { label: "", path: "~" }, { label: "Desktop", path: "~/Desktop" }, { label: "Documents", path: "~/Documents" },
                { label: "Downloads", path: "~/Downloads" }, { label: "Applications", path: "~/Applications" }] },
            { title: "Indigo Magic", color: "#bf5af2", items: [
                { label: "Icon Catalog", path: "/usr/share/applications" }, { label: "Desks Overview", action: "overview" },
                { label: "File QuickFind", action: "find" }] },
            { title: "Locations", color: "#98989d", items: [
                { label: "cachyOS", path: "/" }, { label: "Network", action: "network" }, { label: "Trash", action: "trash" }] }
        ]
    }
    function expand(p) { return p === "~" ? Fm.home : p.startsWith("~/") ? Fm.home + p.substring(1) : p }
    function shorten(p) { return p === Fm.home ? "~" : p.startsWith(Fm.home + "/") ? "~" + p.substring(Fm.home.length) : p }
    function placeLabel(item) { return item.label || (item.path === "~" ? Fm.userName : item.path.split("/").pop() || "/") }
    function setPlaces(p) { placesJson = JSON.stringify(p); settings.sync() }   // saved right away
    function copyPlaces() { return JSON.parse(JSON.stringify(places)) }
    // Adds folder `path` to section `si` at position `at` (default: end). Returns false for duplicates.
    function placeAdd(si, path, at) {
        const p = copyPlaces(), short = shorten(path)
        if (p[si].items.some(it => it.path && expand(it.path) === path)) return false
        const item = { label: "", path: short }
        p[si].items.splice(at === undefined || at < 0 ? p[si].items.length : at, 0, item)
        setPlaces(p)
        return true
    }
    function placeRemove(si, ii) { const p = copyPlaces(); p[si].items.splice(ii, 1); setPlaces(p) }
    function placeRename(si, ii, label) { const p = copyPlaces(); p[si].items[ii].label = label.trim(); setPlaces(p) }
    function placeMove(si, ii, delta) {
        const p = copyPlaces(), items = p[si].items, j = ii + delta
        if (j < 0 || j >= items.length) return
        const [it] = items.splice(ii, 1); items.splice(j, 0, it); setPlaces(p)
    }
    function placeMoveTo(si, ii, sj) { const p = copyPlaces(); const [it] = p[si].items.splice(ii, 1); p[sj].items.push(it); setPlaces(p) }
    function resetPlaces() { placesJson = ""; settings.sync() }
    // Sections are user data too: new, rename, move, remove (the Tags section is fixed).
    property int renamingSection: -1
    function newSection() {
        const p = copyPlaces()
        let t = "New Section", i = 2
        while (p.some(sec => sec.title === t) || t === "Tags") t = "New Section " + i++
        const colors = ["#88C0D0", "#8FBCBB", "#A3BE8C", "#EBCB8B", "#D08770"]
        p.push({ title: t, color: colors[p.length % colors.length], items: [] })
        setPlaces(p)
        sidebarShown = true
        renamingSection = p.length - 1
        return t
    }
    function renameSection(si, title) {
        title = title.trim()
        const p = copyPlaces()
        if (!title || !p[si] || title === "Tags" || p.some((sec, k) => k !== si && sec.title === title)) return
        const old = p[si].title
        p[si].title = title
        if (isCollapsed(old)) collapsedJson = JSON.stringify(collapsed.map(t => t === old ? title : t))
        setPlaces(p)
    }
    function moveSection(si, delta) {
        const p = copyPlaces(), j = si + delta
        if (j < 0 || j >= p.length) return
        const [x] = p.splice(si, 1); p.splice(j, 0, x); setPlaces(p)
    }
    function removeSection(si) {
        const p = copyPlaces(), title = p[si] ? p[si].title : ""
        p.splice(si, 1)
        collapsedJson = JSON.stringify(collapsed.filter(t => t !== title))
        setPlaces(p)
    }
    function activatePlace(item) {
        if (item.udi !== undefined) { item.mounted ? openFolder(item.path, false) : Fm.mountVolume(item.udi); return }
        if (item.action === "trash") { openFolder(Fm.trashDir, false); return }
        if (item.path) openFolder(expand(item.path), false)
        else if (item.action === "overview") Fm.overview()
        else if (item.action === "find") search.forceActiveFocus()
        else if (item.action === "network") Fm.openInDolphin("remote:/")
    }
    function dropOnPlaces(urls, si, at) {
        let pos = at
        for (const u of urls) {
            const path = decodeURIComponent(u.toString().replace(/^file:\/\//, ""))
            if (Fm.kind(path) === "folder" && placeAdd(si, path, pos) && pos !== undefined && pos >= 0) ++pos
        }
    }
    // Rows for the sidebar list: a header per section, its items, or a drop hint when it's empty.
    readonly property var placeRows: {
        const rows = []
        places.forEach((sec, si) => {
            rows.push({ kind: "head", si: si, title: sec.title, first: si === 0, collapsed: isCollapsed(sec.title),
                        afterFolded: si > 0 && isCollapsed(places[si - 1].title) })
            if (isCollapsed(sec.title)) return   // folded: only the title shows
            sec.items.forEach((it, ii) => rows.push({ kind: "item", si: si, ii: ii, item: it, color: sec.color }))
            if (!sec.items.length) rows.push({ kind: "empty", si: si })
            // Removable drives appear at the end of Locations (not saved; they come and go).
            if (sec.title === "Locations" || (si === places.length - 1 && !places.some(x => x.title === "Locations")))
                volumes.forEach(v => rows.push({ kind: "volume", si: si, item: v, color: sec.color }))
        })
        // Tags section (not part of `places`): the shown tags, in their order.
        rows.push({ kind: "head", si: -1, title: "Tags", tags: true, first: false, collapsed: isCollapsed("Tags"),
                    afterFolded: places.length > 0 && isCollapsed(places[places.length - 1].title) })
        if (!isCollapsed("Tags")) {
            tagDefs.forEach((t, ti) => { if (t.shown) rows.push({ kind: "tag", si: -1, ti: ti, tag: t }) })
            if (!tagDefs.some(t => t.shown)) rows.push({ kind: "empty", si: -1, hint: "Right-click to show tags" })
        }
        return rows
    }
    Connections {
        target: Fm
        function onTagsChanged() { win.tagRev++; if (win.searchTag) win.runSearch() }
        function onVolumeMounted(udi, path) { win.openFolder(path, false) }
        function onFolderSizeReady(p, bytes, files, folders) {
            const s = Object.assign({}, win.folderSizes)
            s[p] = bytes < 0 ? "--" : Fm.humanSize(bytes)
            win.folderSizes = s
        }
        function onSearchBatch(items) {
            win.searchResults = win.searchResults.concat(items)
            for (const it of items)
                resultsModel.append({ fileName: it.name, filePath: it.path, fileIsDir: it.isDir, fileUrl: "file://" + it.path,
                                      fileModified: it.modified, fileSize: it.size < 0 ? 0 : it.size })
        }
        function onSearchDone(count, truncated) { win.searching = false; win.searchTruncated = truncated }
        function onArchiveDone(path, ok) { if (ok) win.selectWhenReady([path]) }
    }
    // ---- tags ------------------------------------------------------------------------
    function defaultTags() {
        return ["Red", "Orange", "Yellow", "Green", "Blue", "Purple", "Gray"]
            .map(n => ({ name: n, color: nordColors.find(c => c[0] === n)[1], shown: true }))
    }
    function setTagDefs(d) { tagsJson = JSON.stringify(d); settings.sync() }
    function isCollapsed(title) { return collapsed.includes(title) }
    function toggleSection(title) {
        collapsedJson = JSON.stringify(isCollapsed(title) ? collapsed.filter(t => t !== title) : collapsed.concat([title]))
        settings.sync()
    }
    function tagDef(name) { return tagDefs.find(t => t.name.toLowerCase() === String(name).toLowerCase()) }
    function tagColor(name) { const d = tagDef(name); return d ? d.color : "#4C566A" }
    function tagIcon(color) { return asset("tag-" + color.substring(1).toLowerCase()) }
    function tagsOf(p) { return Fm.tags(p, tagRev) }
    function selectionHasTag(name) {
        const ps = paths()
        return ps.length > 0 && ps.every(p => tagsOf(p).some(t => t.toLowerCase() === name.toLowerCase()))
    }
    function toggleTag(name) {
        const ps = paths()
        if (!ps.length) return
        if (selectionHasTag(name)) Fm.removeTag(ps, name); else Fm.addTag(ps, name)
    }
    function toggleTagIndex(i) { if (tagDefs[i]) toggleTag(tagDefs[i].name) }
    function clearSelectionTags() { if (paths().length) Fm.clearTags(paths()) }
    // A new tag gets the first unused Nord colour and opens for renaming in the sidebar.
    function newTag(assign) {
        let n = "New Tag", i = 2
        while (tagDef(n)) n = "New Tag " + i++
        const used = tagDefs.map(t => t.color)
        const free = nordColors.find(c => !used.includes(c[1])) || nordColors[0]
        setTagDefs(tagDefs.concat([{ name: n, color: free[1], shown: true }]))
        if (assign && paths().length) Fm.addTag(paths(), n)
        sidebarShown = true
        renamingTag = n
        return n
    }
    function renameTag(old, name) {
        name = name.trim().replace(/,/g, " ")
        if (!name || name === old || (tagDef(name) && name.toLowerCase() !== old.toLowerCase())) return
        setTagDefs(tagDefs.map(t => t.name === old ? Object.assign({}, t, { name: name }) : t))
        Fm.renameTagEverywhere(old, name)
        if (searchTag.toLowerCase() === old.toLowerCase()) search.text = "tag:" + name
    }
    function setTagColor(name, color) { setTagDefs(tagDefs.map(t => t.name === name ? Object.assign({}, t, { color: color }) : t)) }
    function setTagShown(name, shown) { setTagDefs(tagDefs.map(t => t.name === name ? Object.assign({}, t, { shown: shown }) : t)) }
    function moveTag(name, delta) {
        const d = tagDefs.slice(), i = d.findIndex(t => t.name === name), j = i + delta
        if (i < 0 || j < 0 || j >= d.length) return
        const [x] = d.splice(i, 1); d.splice(j, 0, x); setTagDefs(d)
    }
    function deleteTag(name) {
        setTagDefs(tagDefs.filter(t => t.name !== name))
        Fm.deleteTagEverywhere(name)
        if (searchTag.toLowerCase() === name.toLowerCase()) search.text = ""
    }
    function resetTags() { tagsJson = ""; settings.sync() }
    function showTagged(name) { if (folderTabs) openTabFor(dir, "tag:" + name); else search.text = "tag:" + name }
    function urlsToPaths(urls) { return urls.map(u => decodeURIComponent(u.toString().replace(/^file:\/\//, ""))) }

    // ---- Finder commands -----------------------------------------------------------
    function sortBy(field) {
        if (sortField === field) sortReversed = !sortReversed
        else { sortField = field; sortReversed = false }
    }
    function openSelection() { for (const p of paths()) activate(p, Fm.kind(p) === "folder") }
    function duplicateSelected() { if (paths().length) selectWhenReady(Fm.duplicate(paths())) }
    function aliasSelected() { if (paths().length) selectWhenReady(Fm.makeAlias(paths())) }
    function showOriginal() {
        const t = paths().length === 1 ? Fm.showOriginal(paths()[0]) : ""
        if (t) submitPath(t)
    }
    function getInfo() {
        for (const p of (paths().length ? paths() : [dir]))
            infoComponent.createObject(null, { path: p, tu: tu, fontFamily: fontFamily, assetFor: asset })
    }
    // Quick Look shows the focused item (the last one clicked or reached with the keys).
    function focusedPath() {
        const p = anchorIndex >= 0 ? itemAt(anchorIndex, "filePath") : ""
        return p && selection[p] ? p : (paths()[0] || "")
    }
    function toggleQuickLook() { quickLook = quickLook ? "" : focusedPath() }
    // Type to select: letters typed within a second form a prefix; the first matching name is selected.
    function typeSelect(text) {
        typeAhead += text.toLowerCase()
        typeAheadTimer.restart()
        for (let i = 0; i < itemCount; ++i)
            if (String(itemAt(i, "fileName")).toLowerCase().startsWith(typeAhead)) {
                select(i, itemAt(i, "filePath"), 0)
                view().positionViewAtIndex(i, ListView.Contain)
                return
            }
    }
    Timer { id: typeAheadTimer; interval: 1000; onTriggered: win.typeAhead = "" }
    // Quick Look follows the selection while it's open.
    onSelectionChanged: if (quickLook && paths().length) quickLook = focusedPath()

    function trashSelected() {
        if (inTrash) { deleteDialog.open(); return }    // in the Trash, Delete means "Delete Immediately"
        Fm.trash(paths()); selection = ({})
    }
    function putBack() { Fm.restoreFromTrash(paths()); selection = ({}) }
    function emptyTrashNow() { Fm.emptyTrash(); selection = ({}) }
    function deleteNow() { Fm.deletePermanently(paths()); selection = ({}) }
    function compressSelected() { if (paths().length) Fm.compress(paths()) }
    function expandSelected() { for (const p of paths()) if (Fm.isArchive(p)) Fm.expand(p) }
    // Search: results come from a background scan (Fm.startSearch); the scope bar picks where.
    function runSearch() {
        searchResults = []; resultsModel.clear(); searchTruncated = false
        selection = ({}); anchorIndex = -1
        if (!search.text) { Fm.stopSearch(); searching = false; return }
        searching = true
        if (searchTag) Fm.startTagSearch(searchTag, dir)   // "tag:Red" (or a click on a tag in the sidebar)
        else Fm.startSearch(searchScope === 2 ? Fm.home : dir, search.text, showHidden, searchScope !== 0)
    }
    Timer { id: searchDelay; interval: 200; onTriggered: win.runSearch() }
    onSearchScopeChanged: if (searchActive) runSearch()
    function showInFolder(path) { const where = path.substring(0, path.lastIndexOf("/")) || "/"; go(where); selectWhenReady([path]) }
    function newFolder() { const p = Fm.makeFolder(dir); const s = ({}); s[p] = true; selection = s; renaming = p }

    FolderListModel {
        id: folder
        folder: "file://" + win.dir
        showDirsFirst: true
        showHidden: win.showHidden
        showDotAndDotDot: false
        caseSensitive: false
        sortCaseSensitive: false
        sortField: win.sortField
        sortReversed: win.sortReversed
        onCountChanged: { win.selectPending(); win.applyPendingScroll() }
        onStatusChanged: {
            if (status === FolderListModel.Loading) win.waitForLoad = false   // the new folder is on its way
            win.selectPending(); win.applyPendingScroll()
        }
    }

    // ---- glass ---------------------------------------------------------------
    // Body tint with holes where the sidebar and toolbar pills sit: those show the
    // blurred wallpaper with their own lighter tint, exactly like the mockup layers.
    Shape {
        anchors.fill: parent
        preferredRendererType: Shape.CurveRenderer
        ShapePath {
            fillColor: win.bodyTint
            strokeColor: "transparent"
            fillRule: ShapePath.OddEvenFill
            PathRectangle { x: 0; y: 0; width: win.width; height: win.height; radius: win.r }
            PathRectangle { x: sidebar.x; y: sidebar.y; width: sidebar.visible ? sidebar.width : 0; height: sidebar.height; radius: 18 * win.u }
            PathRectangle { x: preview.x; y: preview.y; width: preview.visible ? preview.width : 0; height: preview.height; radius: 18 * win.u }
            PathRectangle { x: navPill.x; y: navPill.y; width: navPill.width; height: navPill.height; radius: navPill.height / 2 }
            PathRectangle { x: pathPill.x; y: pathPill.y; width: pathPill.width; height: pathPill.height; radius: pathPill.height / 2 }
            PathRectangle { x: viewPill.x; y: viewPill.y; width: viewPill.width; height: viewPill.height; radius: viewPill.height / 2 }
            PathRectangle { x: searchPill.x; y: searchPill.y; width: searchPill.width; height: searchPill.height; radius: searchPill.height / 2 }
        }
    }
    EdgeRing { anchors.fill: parent; radius: win.r; visible: !win.maximized }

    // Window dragging: empty toolbar strip and sidebar background.
    Item {
        x: 240 * u - win.sideShift; y: 0; width: parent.width - x; height: 72 * u
        DragHandler { target: null; onActiveChanged: if (active) win.startSystemMove() }
        TapHandler { onDoubleTapped: win.maximized ? win.showNormal() : win.showMaximized() }
    }

    // ---- sidebar -------------------------------------------------------------
    Item {
        id: sidebar
        x: 10 * u - win.sideShift * .3; y: 10 * u; width: 220 * u; height: parent.height - 20 * u
        opacity: 1 - win.sideShift / win.hiddenShift
        visible: opacity > .01
        Rectangle { anchors.fill: parent; radius: 18 * win.u; color: win.sidebarTint }
        EdgeRing { anchors.fill: parent; radius: 18 * win.u }
        DragHandler { target: null; onActiveChanged: if (active) win.startSystemMove() }


        // Places list: scrolls (scroll bar, touchpad, wheel) when the window is too short for it.
        ListView {
            id: placesView
            objectName: "places"
            x: 0; y: 10 * win.u + 41 * win.tu
            width: parent.width; height: parent.height - y - 10 * win.u
            clip: true
            interactive: false
            boundsBehavior: Flickable.StopAtBounds
            model: win.placeRows
            ScrollBar.vertical: GlassScrollBar { u: win.u }
            WheelHandler {
                acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                onWheel: event => win.scroll(placesView, event)
            }
            delegate: Item {
                id: row
                required property var modelData
                required property int index
                readonly property bool head: modelData.kind === "head"
                readonly property bool vol: modelData.kind === "volume"
                readonly property bool item: modelData.kind === "item" || vol
                readonly property bool tagRow: modelData.kind === "tag"
                readonly property bool tagHead: head && !!modelData.tags
                readonly property bool current: tagRow ? win.searchTag.toLowerCase() === modelData.tag.name.toLowerCase()
                                                : item && (modelData.item.action === "trash" ? win.inTrash
                                                           : !!modelData.item.path && win.expand(modelData.item.path) === win.dir)
                readonly property string key: modelData.si + "/" + (modelData.ii === undefined ? "" : modelData.ii)
                // Header rows carry the gap above them, so baselines match the mockup.
                // Header rows carry the gap above them; after a folded section it's small, so folded
                // sections stack tightly.
                readonly property real gap: head && !modelData.first ? (modelData.afterFolded ? 4 * win.tu : 30 * win.tu) : 0
                width: placesView.width
                height: gap + (head ? 28 : 30) * win.tu

                Rectangle {
                    visible: (row.item || row.tagRow) && (row.current || hov.hovered || drop.containsDrag)
                    x: 14 * win.u; y: 4 * win.tu; width: 196 * win.u; height: 26 * win.tu; radius: 8 * win.tu
                    color: Qt.rgba(1, 1, 1, row.current || drop.containsDrag ? .16 : .07)
                }
                Rectangle {   // drop target line for headers / empty sections
                    visible: !row.item && !row.tagRow && !row.tagHead && row.modelData.si >= 0 && drop.containsDrag
                    x: 14 * win.u; y: row.height - 3 * win.tu; width: 196 * win.u; height: 2 * win.tu; radius: 1
                    color: "#0a84ff"
                }
                Rectangle {   // a tag: a filled Nord colour dot
                    visible: row.tagRow
                    x: 23 * win.u; y: 8.5 * win.tu; width: 13 * win.tu; height: width; radius: width / 2
                    color: row.tagRow ? row.modelData.tag.color : "transparent"
                    border.color: Qt.rgba(1, 1, 1, .35); border.width: 1
                }
                Rectangle {
                    visible: row.item
                    x: 22 * win.u; y: 9 * win.tu; width: 15 * win.tu; height: 13 * win.tu; radius: 3 * win.tu
                    color: "transparent"; border.color: row.modelData.color || "transparent"; border.width: 1.6 * win.u
                }
                Text {
                    visible: !(row.item && win.renamingPlace === row.key) && !(row.tagRow && win.renamingTag === row.modelData.tag.name)
                             && !(row.head && !row.tagHead && win.renamingSection === row.modelData.si)
                    x: (row.head ? 16 : row.item || row.tagRow ? 48 : 22) * win.u
                    width: (row.head ? 170 : 160) * win.u
                    elide: Text.ElideRight
                    anchors.baseline: parent.top; anchors.baselineOffset: row.gap + 21 * win.tu
                    text: row.head ? row.modelData.title : row.item ? win.placeLabel(row.modelData.item)
                          : row.tagRow ? row.modelData.tag.name : (row.modelData.hint || "Drop folders here")
                    color: "white"; opacity: row.head ? .45 : row.item || row.tagRow ? 1 : .3
                    font.family: win.fontFamily
                    font.pixelSize: (row.head ? 11 : row.item || row.tagRow ? 13.5 : 12) * win.tu
                    font.weight: row.head ? Font.DemiBold : Font.Normal
                }
                Rename {
                    active: row.item && !row.vol && win.renamingPlace === row.key
                    x: 44 * win.u; width: 160 * win.u; y: 4 * win.tu
                    name: row.item ? win.placeLabel(row.modelData.item) : ""
                    u: win.tu; fontFamily: win.fontFamily; align: TextInput.AlignLeft
                    onCommitted: text => win.placeRename(row.modelData.si, row.modelData.ii, text)
                    onDone: { win.renamingPlace = ""; keys.forceActiveFocus() }
                }
                Rename {   // a section title (double-click, or Rename Section…)
                    active: row.head && !row.tagHead && win.renamingSection === row.modelData.si
                    x: 12 * win.u; width: 170 * win.u; y: row.gap + 4 * win.tu
                    name: row.head ? row.modelData.title : ""
                    u: win.tu; fontFamily: win.fontFamily; align: TextInput.AlignLeft
                    onCommitted: text => win.renameSection(row.modelData.si, text)
                    onDone: { win.renamingSection = -1; keys.forceActiveFocus() }
                }
                Rename {
                    active: row.tagRow && win.renamingTag === row.modelData.tag.name
                    x: 44 * win.u; width: 160 * win.u; y: 4 * win.tu
                    name: row.tagRow ? row.modelData.tag.name : ""
                    u: win.tu; fontFamily: win.fontFamily; align: TextInput.AlignLeft
                    onCommitted: text => win.renameTag(row.modelData.tag.name, text)
                    onDone: { win.renamingTag = ""; keys.forceActiveFocus() }
                }
                Text {   // section chevron: ▾ open / › folded; click the title to fold or open it
                    objectName: "chevron"
                    visible: row.head && (hov.hovered || !!row.modelData.collapsed)
                    anchors.right: parent.right; anchors.rightMargin: 40 * win.u
                    anchors.baseline: parent.top; anchors.baselineOffset: row.gap + 21 * win.tu
                    text: row.modelData.collapsed ? "›" : "⌄"; color: "white"; opacity: .55
                    font.family: win.fontFamily; font.pixelSize: (row.modelData.collapsed ? 15 : 13) * win.tu; font.weight: Font.DemiBold
                }
                Text {   // "+" on a hovered header: add the current folder to this section
                    visible: row.head && hov.hovered
                    anchors.right: parent.right; anchors.rightMargin: 18 * win.u
                    anchors.baseline: parent.top; anchors.baselineOffset: row.gap + 21 * win.tu
                    text: "+"; color: "white"; opacity: plusHover.hovered ? .9 : .5
                    font.family: win.fontFamily; font.pixelSize: 15 * win.tu; font.weight: Font.DemiBold
                    HoverHandler { id: plusHover; cursorShape: Qt.PointingHandCursor }
                    TapHandler { onTapped: row.tagHead ? win.newTag(false) : win.placeAdd(row.modelData.si, win.dir) }
                }
                Rectangle {   // ⊖ on a hovered entry: remove it from the sidebar
                    id: removeButton
                    objectName: "removeButton"
                    visible: ((row.item && !row.vol && win.renamingPlace !== row.key) || (row.tagRow && win.renamingTag !== row.modelData.tag.name)) && hov.hovered
                    anchors.right: parent.right; anchors.rightMargin: 20 * win.u
                    y: 8.5 * win.tu; width: 14 * win.tu; height: width; radius: width / 2
                    color: removeHover.hovered ? "#ff453a" : Qt.rgba(1, 1, 1, .25)
                    Rectangle { anchors.centerIn: parent; width: parent.width * .5; height: 1.6 * win.u; color: "white" }
                    HoverHandler { id: removeHover; cursorShape: Qt.PointingHandCursor }
                    // For a tag this only hides it from the sidebar (Delete Tag is in its menu).
                    TapHandler { onTapped: row.tagRow ? win.setTagShown(row.modelData.tag.name, false) : win.placeRemove(row.modelData.si, row.modelData.ii) }
                }
                Text {   // ⏏ on mounted drives
                    visible: row.vol && row.modelData.item.mounted
                    anchors.right: parent.right; anchors.rightMargin: 18 * win.u
                    anchors.baseline: parent.top; anchors.baselineOffset: 21 * win.tu
                    text: "⏏"; color: "white"; opacity: ejectHover.hovered ? .95 : .55
                    font.pixelSize: 13 * win.tu
                    HoverHandler { id: ejectHover; cursorShape: Qt.PointingHandCursor }
                    TapHandler { onTapped: Fm.ejectVolume(row.modelData.item.udi) }
                }
                HoverHandler { id: hov }
                MouseArea {
                    anchors.fill: parent; anchors.rightMargin: row.head || (row.vol && row.modelData.item.mounted) || removeButton.visible ? 36 * win.u : 0
                    acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton
                    onClicked: m => {
                        keys.forceActiveFocus()
                        if (m.button === Qt.MiddleButton) {   // a place in a background tab
                            const it = row.modelData.item
                            if (it && (it.path || it.action === "trash")) win.newTab(it.action === "trash" ? Fm.trashDir : win.expand(it.path), false)
                            return
                        }
                        if (row.tagRow) {
                            if (m.button === Qt.RightButton) { tagMenu.tag = row.modelData.tag; tagMenu.open() }
                            else win.showTagged(row.modelData.tag.name)
                            return
                        }
                        if (row.head && m.button === Qt.LeftButton) { win.toggleSection(row.modelData.title); return }
                        if (row.tagHead || row.modelData.si < 0) { if (m.button === Qt.RightButton) tagsHeadMenu.open(); return }
                        placeMenu.si = row.modelData.si
                        placeMenu.ii = row.item ? row.modelData.ii : -1
                        if (m.button === Qt.RightButton && row.vol) { volumeMenu.vol = row.modelData.item; volumeMenu.open() }
                        else if (m.button === Qt.RightButton && row.modelData.item && row.modelData.item.action === "trash") trashMenu.open()
                        else if (m.button === Qt.RightButton) (row.item ? placeMenu : sectionMenu).open()
                        else if (row.item) win.activatePlace(row.modelData.item)
                    }
                    onDoubleClicked: m => {
                        if (m.button !== Qt.LeftButton) return
                        if (row.tagRow) win.renamingTag = row.modelData.tag.name
                        // the first click folded it: open it again and rename
                        else if (row.head && !row.tagHead) { win.toggleSection(row.modelData.title); win.renamingSection = row.modelData.si }
                        else if (row.item && !row.vol) win.renamingPlace = row.key
                    }
                }
                DropArea {
                    id: drop
                    anchors.fill: parent
                    keys: ["text/uri-list"]
                    // Dropping on an entry inserts after it; on a header, at the top of the section. Onto a
                    // drive copies there; onto Trash moves to the Trash. Hovering opens it (spring-loaded).
                    onEntered: if (row.item && (row.modelData.item.path || row.modelData.item.action === "trash")) spring.restart()
                    onExited: spring.stop()
                    onDropped: d => {
                        spring.stop()
                        if (row.tagRow) { Fm.addTag(win.urlsToPaths(d.urls), row.modelData.tag.name); d.accept(Qt.LinkAction); return }   // tag them
                        if (row.modelData.si < 0) return
                        const it = row.modelData.item
                        if (row.vol && it.mounted) { Fm.drop(d.urls, it.path, false); d.accept(Qt.CopyAction) }
                        else if (it && it.action === "trash") { Fm.trash(d.urls.map(u => decodeURIComponent(u.toString().replace(/^file:\/\//, "")))); d.accept(Qt.MoveAction) }
                        else if (!row.vol) { win.dropOnPlaces(d.urls, row.modelData.si, row.item ? row.modelData.ii + 1 : 0); d.accept(Qt.LinkAction) }
                    }
                }
                Timer {
                    id: spring
                    interval: win.springDelay
                    onTriggered: win.activatePlace(row.modelData.item)
                }
            }
        }
    }

    // Traffic lights stay put when the sidebar hides; the sidebar button moves next to them.
    TrafficLights { z: 5; window: win; x: 27.5 * win.u; y: 36 * win.u - height / 2 }
    Item {
        id: sidebarButton
        objectName: "sidebarButton"
        z: 5
        x: 194 * u - win.sideShift * (78 / 98); y: 36 * u - height / 2
        width: 22 * u; height: 18 * u
        Rectangle {   // a window with a sidebar
            anchors.fill: parent; radius: 4 * win.u; color: "transparent"
            border.color: Qt.rgba(1, 1, 1, sbHover.hovered ? .9 : .55); border.width: 1.5 * win.u
            Rectangle { x: 6 * win.u; y: 2; width: 1.5 * win.u; height: parent.height - 4; color: parent.border.color }
        }
        HoverHandler { id: sbHover; cursorShape: Qt.PointingHandCursor }
        TapHandler { onTapped: win.sidebarShown = !win.sidebarShown }
    }

    // ---- toolbar -------------------------------------------------------------
    Pill {
        id: navPill
        x: 250 * u - win.sideShift; y: 18 * u; width: 86 * u; height: 36 * u
        Text {
            x: 24 * win.u - width / 2; anchors.baseline: parent.top; anchors.baselineOffset: 24 * win.u
            text: "‹"; color: "white"; opacity: win.backStack.length ? 1 : .4
            font.family: win.fontFamily; font.pixelSize: 22 * win.tu
        }
        Text {
            x: 62 * win.u - width / 2; anchors.baseline: parent.top; anchors.baselineOffset: 24 * win.u
            text: "›"; color: "white"; opacity: win.fwdStack.length ? 1 : .4
            font.family: win.fontFamily; font.pixelSize: 22 * win.tu
        }
        TapHandler { onTapped: p => p.position.x < navPill.width / 2 ? win.goBack() : win.goForward() }
    }

    Pill {
        id: pathPill
        x: 350 * u - win.sideShift; y: 18 * u; height: 36 * u
        transform: Translate { id: pathShake }
        width: viewPill.x - 15 * u - x
        // Drop pocket: the current directory as a draggable IRIX icon.
        Rectangle {
            id: pocket
            x: 6 * win.u; y: 5 * win.u; width: 34 * win.u; height: 26 * win.u; radius: 9 * win.u
            color: Qt.rgba(0, 0, 0, .35)
            Image {
                x: .2 * win.u; y: -3.8 * win.u; width: 33.6 * win.u; height: width
                source: win.asset("folder"); sourceSize: Qt.size(width * 2, height * 2); smooth: true
            }
            Item {
                id: pocketDrag
                Drag.active: pocketMouse.drag.active
                Drag.dragType: Drag.Automatic
                Drag.supportedActions: Qt.CopyAction | Qt.MoveAction | Qt.LinkAction
                Drag.mimeData: ({ "text/uri-list": Fm.uriList([win.dir]) })
            }
            MouseArea { id: pocketMouse; anchors.fill: parent; drag.target: pocketDrag; onReleased: pocketDrag.x = pocketDrag.y = 0 }
        }
        Item {
            id: crumbsClip
            visible: !win.editingPath
            x: 54 * win.u; y: 0; height: parent.height
            width: parent.width - x - 44 * win.u
            clip: true
            MouseArea {   // empty part of the path bar: start typing a path
                anchors.fill: parent
                cursorShape: Qt.IBeamCursor
                onClicked: win.editPath()
            }
            Row {
                id: crumbs
                x: Math.min(0, crumbsClip.width - width)
                height: parent.height
                Repeater {
                    model: {
                        if (win.inTrash) {
                            const out = [{ label: "Trash", path: Fm.trashDir }]
                            let acc = Fm.trashDir
                            for (const p of win.dir.substring(Fm.trashDir.length).split("/").filter(p => p.length)) { acc += "/" + p; out.push({ label: p, path: acc }) }
                            return out
                        }
                        const parts = win.dir.split("/").filter(p => p.length)
                        const out = [{ label: "/", path: "/" }]
                        let acc = ""
                        for (const p of parts) { acc += "/" + p; out.push({ label: p, path: acc }) }
                        return out
                    }
                    Row {
                        id: crumb
                        required property var modelData
                        required property int index
                        readonly property bool last: modelData.path === win.dir
                        height: crumbs.height
                        Text {
                            anchors.baseline: parent.top; anchors.baselineOffset: 23 * win.u
                            text: crumb.modelData.label; color: "white"; opacity: crumb.last ? 1 : .7
                            font.family: win.fontFamily; font.pixelSize: 13.5 * win.tu
                            font.weight: crumb.last ? Font.DemiBold : Font.Normal
                            MouseArea {   // a parent folder: go there; the current folder: edit the path
                                anchors.fill: parent
                                cursorShape: crumb.last ? Qt.IBeamCursor : Qt.PointingHandCursor
                                acceptedButtons: Qt.LeftButton | Qt.MiddleButton
                                onClicked: m => m.button === Qt.MiddleButton ? win.newTab(crumb.modelData.path, false)
                                                : crumb.last ? win.editPath() : win.go(crumb.modelData.path)
                            }
                        }
                        Text {
                            visible: !crumb.last
                            anchors.baseline: parent.top; anchors.baselineOffset: 23 * win.u
                            text: "  ›  "; color: "white"; opacity: .4
                            font.family: win.fontFamily; font.pixelSize: 13.5 * win.tu
                        }
                    }
                }
            }
        }
        TextInput {
            id: pathInput
            objectName: "pathInput"
            visible: win.editingPath
            x: 54 * win.u; width: parent.width - x - 44 * win.u
            anchors.verticalCenter: parent.verticalCenter
            color: "white"; selectionColor: "#0a84ff"; selectedTextColor: "white"
            font.family: win.fontFamily; font.pixelSize: 13.5 * win.tu
            clip: true
            // What Tab would complete to, shown greyed out after the cursor.
            readonly property string completion: activeFocus && cursorPosition === length && length > 0
                                                 ? Fm.completePath(text, win.dir) : ""
            readonly property string ghost: completion.toLowerCase().startsWith(text.toLowerCase()) ? completion.substring(length) : ""
            Text {
                visible: pathInput.ghost.length > 0 && pathInput.selectedText.length === 0
                x: pathInput.positionToRectangle(pathInput.length).x
                anchors.verticalCenter: parent.verticalCenter
                text: pathInput.ghost; color: "white"; opacity: .35
                font: pathInput.font
            }
            Keys.onReturnPressed: win.submitPath(text)
            Keys.onEnterPressed: win.submitPath(text)
            Keys.onEscapePressed: win.cancelPath()
            Keys.onTabPressed: event => { text = Fm.completePath(text, win.dir); cursorPosition = length; event.accepted = true }
            onActiveFocusChanged: if (!activeFocus && win.editingPath) win.editingPath = false
        }
        Rectangle {   // focus ring while typing; flashes red for a path that doesn't exist
            id: pathRing
            visible: win.editingPath
            anchors.fill: parent; anchors.margins: -1.5
            radius: height / 2
            color: "transparent"
            border.width: 2; border.color: Qt.rgba(10 / 255, 132 / 255, 1, .85)
            SequentialAnimation {
                id: pathError
                ColorAnimation { target: pathRing; property: "border.color"; to: "#ff453a"; duration: 60 }
                NumberAnimation { target: pathShake; property: "x"; from: 0; to: -6; duration: 50 }
                NumberAnimation { target: pathShake; property: "x"; to: 6; duration: 70 }
                NumberAnimation { target: pathShake; property: "x"; to: -3; duration: 60 }
                NumberAnimation { target: pathShake; property: "x"; to: 0; duration: 50 }
                PauseAnimation { duration: 350 }
                ColorAnimation { target: pathRing; property: "border.color"; to: Qt.rgba(10 / 255, 132 / 255, 1, .85); duration: 200 }
            }
        }
        Text {
            anchors.right: parent.right; anchors.rightMargin: 16 * win.u
            anchors.baseline: parent.top; anchors.baselineOffset: 22 * win.u
            text: "⌄"; color: "white"; opacity: .6
            font.family: win.fontFamily; font.pixelSize: 14 * win.tu
            TapHandler { onTapped: historyMenu.open() }
        }
    }

    Pill {
        id: viewPill
        width: 128 * u; height: 36 * u; y: 18 * u
        x: searchPill.x - 14 * u - width
        Rectangle {
            x: (4 + win.viewMode * 41) * win.u; y: 4 * win.u; width: 40 * win.u; height: 28 * win.u; radius: 14 * win.u
            color: Qt.rgba(1, 1, 1, .18)
            Behavior on x { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }
        }
        Repeater {
            model: 3
            Item {
                required property int index
                x: (16 + index * 41) * win.u; y: 11 * win.u; width: 15 * win.u; height: 14 * win.u
                Rectangle {
                    anchors.fill: parent; radius: 3 * win.u; color: "transparent"
                    border.color: Qt.rgba(1, 1, 1, .85); border.width: 1.5 * win.u
                }
                // tiny hint of what each mode is: grid, list, picture
                Grid {
                    visible: index === 0; anchors.centerIn: parent; columns: 2; spacing: 1.5 * win.u
                    Repeater { model: 4; Rectangle { width: 3 * win.u; height: 3 * win.u; color: Qt.rgba(1, 1, 1, .6) } }
                }
                Column {
                    visible: index === 1; anchors.centerIn: parent; spacing: 2 * win.u
                    Repeater { model: 3; Rectangle { width: 8 * win.u; height: 1.2 * win.u; color: Qt.rgba(1, 1, 1, .6) } }
                }
                Shape {
                    visible: index === 2; anchors.fill: parent
                    ShapePath {
                        fillColor: Qt.rgba(1, 1, 1, .6); strokeColor: "transparent"
                        startX: 3 * win.u; startY: 11 * win.u
                        PathLine { x: 6.5 * win.u; y: 6 * win.u }
                        PathLine { x: 9 * win.u; y: 9 * win.u }
                        PathLine { x: 10.5 * win.u; y: 7.5 * win.u }
                        PathLine { x: 12.5 * win.u; y: 11 * win.u }
                    }
                }
                TapHandler { onTapped: win.viewMode = index }
            }
        }
    }

    Pill {
        id: searchPill
        width: 176 * u; height: 36 * u; y: 18 * u
        x: win.width - 17 * u - width
        Rectangle {
            x: 14 * win.u; y: 11 * win.u; width: 12 * win.u; height: 12 * win.u; radius: width / 2
            color: "transparent"; border.color: Qt.rgba(1, 1, 1, .6); border.width: 1.6 * win.u
        }
        TextInput {
            id: search
            objectName: "search"
            x: 36 * win.u; width: parent.width - x - 14 * win.u
            anchors.baseline: parent.top; anchors.baselineOffset: 23 * win.u
            color: "white"; selectionColor: "#0a84ff"
            font.family: win.fontFamily; font.pixelSize: 13.5 * win.tu
            clip: true
            onTextChanged: { searchDelay.restart(); Qt.callLater(win.syncTab) }
            Keys.onEscapePressed: { text = ""; keys.forceActiveFocus() }
            Keys.onDownPressed: { keys.forceActiveFocus(); if (win.itemCount) win.select(0, win.itemAt(0, "filePath"), 0) }
            Keys.onReturnPressed: { keys.forceActiveFocus(); if (win.itemCount) win.select(0, win.itemAt(0, "filePath"), 0) }
            Text {
                visible: !search.text
                text: "Search"; color: "white"; opacity: .5
                font: search.font
            }
        }
    }

    // ---- content ---------------------------------------------------------------
    Item {
        id: keys
        focus: true
        Keys.onPressed: event => {
            const cols = Math.max(1, Math.floor(grid.width / grid.cellWidth))
            const vertical = win.viewMode === 1 ? 1 : cols
            if (win.renaming) return
            const plain = !(event.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier))
            if ((event.text === "/" || event.text === "~") && plain) {
                win.editPath(event.text); event.accepted = true; return
            }
            if (event.key === Qt.Key_Space && plain && !win.typeAhead) { win.toggleQuickLook(); event.accepted = true; return }
            if (win.quickLook && event.key === Qt.Key_Escape) { win.quickLook = ""; event.accepted = true; return }
            // Letters type-select; a space continues a name being typed (Space alone is Quick Look).
            if (plain && event.text.length === 1 && (event.text > " " || (event.text === " " && win.typeAhead)) && event.key !== Qt.Key_Delete) {
                win.typeSelect(event.text); event.accepted = true; return
            }
            if (win.searchActive && event.key === Qt.Key_Escape && !win.quickLook && !win.paths().length) { search.text = ""; event.accepted = true; return }
            if (event.key === Qt.Key_Up && event.modifiers & Qt.ControlModifier) { win.goUp(); event.accepted = true; return }
            if (event.key === Qt.Key_Down && event.modifiers & Qt.ControlModifier) { win.openSelection(); event.accepted = true; return }
            if (event.key === Qt.Key_Backspace && event.modifiers & Qt.ControlModifier) {   // in the Trash: Put Back, as in Finder
                win.inTrash ? win.putBack() : win.trashSelected(); event.accepted = true; return
            }
            if (event.key === Qt.Key_Left && event.modifiers & Qt.AltModifier) win.goBack()
            else if (event.key === Qt.Key_Right && event.modifiers & Qt.AltModifier) win.goForward()
            else if (event.key === Qt.Key_Up && event.modifiers & Qt.AltModifier) win.goUp()
            else if (event.key === Qt.Key_Left && win.viewMode !== 1) win.moveCursor(-1)
            else if (event.key === Qt.Key_Right && win.viewMode !== 1) win.moveCursor(1)
            else if (event.key === Qt.Key_Up) win.moveCursor(-vertical)
            else if (event.key === Qt.Key_Down) win.moveCursor(vertical)
            else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                if (win.anchorIndex >= 0) win.activate(win.itemAt(win.anchorIndex, "filePath"), win.itemAt(win.anchorIndex, "fileIsDir"))
            }
            else if (event.key === Qt.Key_Backspace) win.goUp()
            else if (event.key === Qt.Key_F2 && win.paths().length === 1) win.renaming = win.paths()[0]
            else if (event.key === Qt.Key_Delete) win.trashSelected()
            else if (event.key === Qt.Key_Escape) win.selection = ({})
            else return
            event.accepted = true
        }
    }

    // Drops onto empty space go into the current folder.
    DropArea {
        x: 240 * u - win.contentShift; y: 72 * u + win.scopeH; width: parent.width - x - win.previewW - win.splitW; height: parent.height - y - win.bottomH
        onDropped: drop => { if (!win.inTrash) win.dropFiles(drop.urls, win.dir, drop.proposedAction === Qt.MoveAction); drop.accept() }
    }

    GridView {
        id: grid
        objectName: "grid"
        visible: win.viewMode !== 1
        x: 262 * u - win.contentShift; y: 92 * u - 10 * win.k + win.scopeH
        width: parent.width - x - 20 * u - win.previewW - win.splitW
        height: parent.height - win.bottomH - y
        clip: true
        cellWidth: 168 * u * win.iconScale / .92
        cellHeight: 168 * u * win.iconScale / .92
        model: win.searchActive ? resultsModel : folder
        interactive: false
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: GlassScrollBar { u: win.u }
        // The grid is non-interactive (so a left drag draws a rubber band), so scrolling is done here.
        WheelHandler {
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
            acceptedModifiers: Qt.NoModifier
            onWheel: event => win.scroll(grid, event)
        }
        WheelHandler {
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
            acceptedModifiers: Qt.ControlModifier
            onWheel: event => wheelCtl.nudge((event.pixelDelta.y !== 0 ? event.pixelDelta.y / 50 : event.angleDelta.y / 120) * .05)
        }
        MouseArea {   // background: clear selection, rubber band, context menu
            id: band
            parent: grid
            anchors.fill: parent
            z: -1
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            property point start
            property rect area
            property bool active: false
            onPressed: mouse => {
                keys.forceActiveFocus()
                win.activePane = 0
                if (mouse.button === Qt.RightButton) { bgMenu.open(); return }
                if (!(mouse.modifiers & (Qt.ControlModifier | Qt.ShiftModifier))) win.selection = ({})
                start = Qt.point(mouse.x, mouse.y + grid.contentY); active = true
                area = Qt.rect(start.x, start.y, 0, 0)
            }
            onPositionChanged: mouse => {
                if (!active) return
                const y = mouse.y + grid.contentY
                area = Qt.rect(Math.min(start.x, mouse.x), Math.min(start.y, y), Math.abs(mouse.x - start.x), Math.abs(y - start.y))
                const cols = Math.max(1, Math.floor(grid.width / grid.cellWidth))
                const s = ({})
                for (let i = 0; i < win.itemCount; ++i) {
                    const cx = (i % cols) * grid.cellWidth, cy = Math.floor(i / cols) * grid.cellHeight
                    const ix = cx + grid.cellWidth * .15, iw = grid.cellWidth * .7
                    const iy = cy + 10 * win.k, ih = 100 * win.k + 26 * win.tu
                    if (ix < area.x + area.width && ix + iw > area.x && iy < area.y + area.height && iy + ih > area.y)
                        s[win.itemAt(i, "filePath")] = true
                }
                win.selection = s
            }
            onReleased: active = false
        }
        Rectangle {   // rubber band
            parent: grid.contentItem
            visible: band.active && band.area.width > 2
            x: band.area.x; y: band.area.y; width: band.area.width; height: band.area.height
            color: Qt.rgba(10 / 255, 132 / 255, 1, .18); border.color: Qt.rgba(10 / 255, 132 / 255, 1, .8); radius: 4 * win.u
        }

        delegate: Item {
            id: cell
            required property int index
            required property string fileName
            required property string filePath
            required property bool fileIsDir
            required property url fileUrl
            readonly property string kind: fileIsDir ? "folder" : Fm.kind(filePath)
            readonly property var info: kind === "app" ? Fm.desktopInfo(filePath) : null
            readonly property var tags: win.tagsOf(filePath)
            readonly property bool sel: win.selection[filePath] === true
            readonly property real k: win.k
            width: grid.cellWidth; height: grid.cellHeight

            Item {
                id: icon
                x: (cell.width - 120 * cell.k) / 2; y: 0
                width: 120 * cell.k; height: width
                readonly property size src: Qt.size(width * Screen.devicePixelRatio, height * Screen.devicePixelRatio)
                Image {
                    id: carpet
                    anchors.fill: parent
                    visible: !carpetTint.visible
                    source: win.asset(cell.sel || drop.containsDrag ? "carpet-sel" : cell.kind === "exe" ? "carpet-strong" : "carpet")
                    sourceSize: icon.src; smooth: true
                }
                // Tagged: the IRIX magic carpet takes the (first) tag's Nord colour. Same outline as
                // icons/carpet*.svg (points 6,84 58,97 98,80 46,68 in a -10..110 view box).
                Shape {
                    id: carpetTint
                    objectName: "carpetTint"
                    anchors.fill: parent
                    visible: cell.tags.length > 0 && !cell.sel && !drop.containsDrag
                    preferredRendererType: Shape.CurveRenderer
                    readonly property color tint: win.tagColor(cell.tags[0] || "")
                    ShapePath {
                        strokeColor: Qt.rgba(0, 0, 0, .45); strokeWidth: 1
                        fillGradient: LinearGradient {
                            x1: 16 * cell.k; y1: 78 * cell.k; x2: 108 * cell.k; y2: 107 * cell.k
                            GradientStop { position: 0; color: Qt.lighter(carpetTint.tint, 1.25) }
                            GradientStop { position: 1; color: carpetTint.tint }
                        }
                        startX: 16 * cell.k; startY: 94 * cell.k
                        PathLine { x: 68 * cell.k; y: 107 * cell.k }
                        PathLine { x: 108 * cell.k; y: 90 * cell.k }
                        PathLine { x: 56 * cell.k; y: 78 * cell.k }
                        PathLine { x: 16 * cell.k; y: 94 * cell.k }
                    }
                }
                Image {
                    anchors.fill: parent
                    visible: cell.kind !== "app" && !preview.visible
                    source: win.asset(cell.kind === "app" ? "doc" : cell.kind)
                    sourceSize: icon.src; smooth: true
                }
                K.Icon {
                    visible: cell.kind === "app"
                    x: 32 * cell.k; y: 22 * cell.k; width: 58 * cell.k; height: width
                    roundToIconSize: false
                    source: cell.info ? cell.info.icon : ""
                }
                Rectangle {   // previews mode: the real picture on a white mount
                    id: preview
                    visible: win.viewMode === 2 && cell.kind === "image" && thumb.status === Image.Ready
                    x: 30 * cell.k; y: 18 * cell.k; width: 62 * cell.k; height: 62 * cell.k
                    color: "white"; border.color: "#1d1d1d"; border.width: 1
                    rotation: 4
                    Image {
                        id: thumb
                        anchors.fill: parent; anchors.margins: 3 * cell.k
                        source: win.viewMode === 2 && cell.kind === "image" ? cell.fileUrl : ""
                        asynchronous: true; fillMode: Image.PreserveAspectCrop
                        sourceSize: Qt.size(256, 256)
                    }
                }
            }
            Rectangle {
                visible: cell.sel && win.renaming !== cell.filePath
                width: Math.min(label.implicitWidth, label.width) + 14 * win.tu; height: 21 * win.tu; radius: 6 * win.tu
                x: (cell.width - width) / 2; y: label.y + label.baselineOffset - 15 * win.tu
                color: "#0a84ff"
            }
            Text {
                id: label
                visible: win.renaming !== cell.filePath
                width: cell.width - 16 * win.u; x: 8 * win.u
                anchors.baseline: parent.top; anchors.baselineOffset: 110 * cell.k + 20 * win.tu
                horizontalAlignment: Text.AlignHCenter
                text: cell.info && cell.info.name ? cell.info.name : cell.fileName
                elide: Text.ElideMiddle
                color: "white"
                font.family: win.fontFamily; font.pixelSize: 13 * win.tu
                font.weight: cell.sel ? Font.Medium : Font.Normal
            }
            TagDots {
                tags: cell.tags; colorFor: win.tagColor; size: 9 * win.tu
                visible: cell.tags.length > 0 && label.visible
                x: label.x + (label.width + Math.min(label.contentWidth, label.width)) / 2 + (cell.sel ? 10 : 4) * win.u
                y: label.y + (label.height - height) / 2
            }
            Rename {
                active: win.renaming === cell.filePath
                width: cell.width - 16 * win.u; x: 8 * win.u
                y: label.y - 3 * win.u
                path: cell.filePath; name: cell.fileName; u: win.tu; fontFamily: win.fontFamily
                onDone: { win.renaming = ""; keys.forceActiveFocus() }
            }

            Item {
                id: dragProxy
                Drag.active: mouse.drag.active
                Drag.dragType: Drag.Automatic
                Drag.supportedActions: Qt.CopyAction | Qt.MoveAction | Qt.LinkAction
                Drag.mimeData: ({ "text/uri-list": Fm.uriList(win.paths()) })
                Drag.onActiveChanged: win.internalDrag = Drag.active
            }
            MouseArea {
                id: mouse
                x: (cell.width - 100 * cell.k) / 2; y: 10 * cell.k
                width: 100 * cell.k; height: 100 * cell.k + 26 * win.tu
                drag.target: dragProxy
                acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton
                onPressed: m => {
                    keys.forceActiveFocus()
                    win.activePane = 0
                    if (m.button === Qt.MiddleButton) { if (cell.fileIsDir) win.openInNewTab([cell.filePath], false); return }
                    if (!cell.sel || m.modifiers & (Qt.ControlModifier | Qt.ShiftModifier)) win.select(cell.index, cell.filePath, m.modifiers)
                    if (m.button === Qt.RightButton) itemMenu.open()
                }
                onReleased: { dragProxy.x = 0; dragProxy.y = 0 }
                onDoubleClicked: m => {
                    if (m.button !== Qt.LeftButton) return
                    if (cell.fileIsDir && m.modifiers & Qt.ControlModifier) win.folderTabs ? win.go(cell.filePath) : win.openInNewTab([cell.filePath], true)
                    else win.activate(cell.filePath, cell.fileIsDir)
                }
            }
            DropArea {
                id: drop
                enabled: cell.fileIsDir
                anchors.fill: mouse
                // Spring-loaded: hovering with a drag opens the folder.
                onEntered: cellSpring.restart()
                onExited: cellSpring.stop()
                onDropped: d => { cellSpring.stop(); win.dropFiles(d.urls, cell.filePath, d.proposedAction === Qt.MoveAction); d.accept() }
            }
            Timer { id: cellSpring; interval: win.springDelay; onTriggered: win.go(cell.filePath) }
        }
    }

    ListView {
        id: list
        objectName: "list"
        visible: win.viewMode === 1
        WheelHandler {
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
            onWheel: event => win.scroll(list, event)
        }
        x: 252 * u - win.contentShift; y: 80 * u + win.scopeH
        width: parent.width - x - 14 * u - win.previewW - win.splitW
        height: parent.height - win.bottomH - y
        clip: true
        model: win.searchActive ? resultsModel : folder
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: GlassScrollBar { u: win.u }
        headerPositioning: ListView.OverlayHeader
        header: Item {
            z: 2; width: list.width; height: 26 * win.tu
            Row {
                x: win.listTextX; height: parent.height
                Repeater {
                    // Click a column to sort by it; click again to reverse.
                    model: [["Name", list.width - (win.compactList ? 250 : 430) * win.u, FolderListModel.Name],
                            ["Date Modified", win.compactList ? 0 : 180 * win.u, FolderListModel.Time],
                            ["Size", 90 * win.u, FolderListModel.Size], [win.searchActive ? "Where" : "Kind", 110 * win.u, FolderListModel.Type]]
                    Text {
                        required property var modelData
                        readonly property bool active: win.sortField === modelData[2]
                        width: modelData[1]; visible: width > 0
                        anchors.verticalCenter: parent.verticalCenter
                        text: modelData[0] + (active ? (win.sortReversed ? "  ▾" : "  ▴") : "")
                        color: "white"; opacity: active ? .85 : .45
                        font.family: win.fontFamily; font.pixelSize: 11 * win.tu; font.weight: Font.DemiBold
                        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: win.sortBy(parent.modelData[2]) }
                    }
                }
            }
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Qt.rgba(1, 1, 1, .08) }
        }
        MouseArea {
            parent: list; anchors.fill: parent; z: -1
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            onPressed: m => { keys.forceActiveFocus(); if (m.button === Qt.RightButton) bgMenu.open(); else win.selection = ({}) }
        }
        delegate: Item {
            id: row
            required property int index
            required property string fileName
            required property string filePath
            required property bool fileIsDir
            required property date fileModified
            required property var fileSize
            readonly property string kind: fileIsDir ? "folder" : Fm.kind(filePath)
            readonly property var info: kind === "app" ? Fm.desktopInfo(filePath) : null
            readonly property var tags: win.tagsOf(filePath)
            readonly property bool sel: win.selection[filePath] === true
            width: list.width; height: win.listRowH
            Rectangle {
                anchors.fill: parent; radius: 6 * win.u
                color: row.sel ? "#0a84ff" : rowDrop.containsDrag ? Qt.rgba(1, 1, 1, .14) : row.index % 2 ? Qt.rgba(1, 1, 1, .035) : "transparent"
            }
            Image {
                x: 8 * win.u; y: (row.height - height) / 2 - 1 * win.u
                width: win.listIcon; height: width
                visible: row.kind !== "app"
                source: win.asset(row.kind === "app" ? "doc" : row.kind)
                sourceSize: Qt.size(width * 2, height * 2); smooth: true
            }
            K.Icon {
                x: 8 * win.u + win.listIcon * .15; y: (row.height - height) / 2
                width: win.listIcon * .7; height: width
                roundToIconSize: false
                visible: row.kind === "app"
                source: row.info ? row.info.icon : ""
            }
            Row {
                x: win.listTextX; height: parent.height
                Text {
                    id: rowName
                    width: list.width - (win.compactList ? 250 : 430) * win.u - (row.tags.length ? 30 * win.u : 0); anchors.verticalCenter: parent.verticalCenter
                    visible: win.renaming !== row.filePath
                    text: row.info && row.info.name ? row.info.name : row.fileName
                    elide: Text.ElideRight; color: "white"
                    font.family: win.fontFamily; font.pixelSize: 13 * win.tu
                    TagDots {
                        tags: row.tags; colorFor: win.tagColor; size: 9 * win.tu
                        x: Math.min(rowName.contentWidth, rowName.width) + 6 * win.u
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
                Rename {
                    active: win.renaming === row.filePath
                    width: list.width - (win.compactList ? 260 : 440) * win.u; anchors.verticalCenter: parent.verticalCenter
                    path: row.filePath; name: row.fileName; u: win.tu; fontFamily: win.fontFamily
                    onDone: { win.renaming = ""; keys.forceActiveFocus() }
                }
                Repeater {
                    model: [
                        [Qt.formatDateTime(row.fileModified, "d MMM yyyy 'at' HH:mm"), win.compactList ? 0 : 180],
                        // folder sizes: the fresh count, else the cached one (shown at once), else "…"
                        [row.fileIsDir ? (win.calcSizes ? (win.folderSizes[row.filePath] || Fm.cachedFolderSize(row.filePath) || "…") : "--")
                                       : Fm.humanSize(row.fileSize), 90],
                        [win.searchActive ? win.shorten(row.filePath.substring(0, row.filePath.lastIndexOf("/")) || "/")
                         : row.fileIsDir ? "Folder" : row.kind === "app" ? "Application" : row.kind === "image" ? "Image" : row.kind === "exe" ? "Executable" : "Document", 110]
                    ]
                    Text {
                        required property var modelData
                        width: modelData[1] * win.u; visible: width > 0; anchors.verticalCenter: parent.verticalCenter
                        text: modelData[0]; color: "white"; opacity: row.sel ? .9 : .55
                        elide: Text.ElideRight
                        font.family: win.fontFamily; font.pixelSize: 12 * win.tu
                    }
                }
            }
            Item {
                id: rowDrag
                Drag.active: rowMouse.drag.active
                Drag.dragType: Drag.Automatic
                Drag.supportedActions: Qt.CopyAction | Qt.MoveAction | Qt.LinkAction
                Drag.mimeData: ({ "text/uri-list": Fm.uriList(win.paths()) })
                Drag.onActiveChanged: win.internalDrag = Drag.active
            }
            MouseArea {
                id: rowMouse
                anchors.fill: parent
                drag.target: rowDrag
                acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton
                onPressed: m => {
                    keys.forceActiveFocus()
                    win.activePane = 0
                    if (m.button === Qt.MiddleButton) { if (row.fileIsDir) win.openInNewTab([row.filePath], false); return }
                    if (!row.sel || m.modifiers & (Qt.ControlModifier | Qt.ShiftModifier)) win.select(row.index, row.filePath, m.modifiers)
                    if (m.button === Qt.RightButton) itemMenu.open()
                }
                onReleased: { rowDrag.x = 0; rowDrag.y = 0 }
                onDoubleClicked: m => {
                    if (m.button !== Qt.LeftButton) return
                    if (row.fileIsDir && m.modifiers & Qt.ControlModifier) win.folderTabs ? win.go(row.filePath) : win.openInNewTab([row.filePath], true)
                    else win.activate(row.filePath, row.fileIsDir)
                }
            }
            DropArea {
                id: rowDrop
                enabled: row.fileIsDir
                anchors.fill: parent
                onEntered: rowSpring.restart()
                onExited: rowSpring.stop()
                onDropped: d => { rowSpring.stop(); win.dropFiles(d.urls, row.filePath, d.proposedAction === Qt.MoveAction); d.accept() }
            }
            Timer { id: rowSpring; interval: win.springDelay; onTriggered: win.go(row.filePath) }
            // Folder sizes are counted in the background (View ▸ Calculate All Sizes).
            Component.onCompleted: if (fileIsDir && win.calcSizes && win.folderSizes[filePath] === undefined) Fm.folderSize(filePath)
        }
    }

    // ---- search: scope bar + results ------------------------------------------------
    Row {
        id: scopeBar
        objectName: "scopeBar"
        visible: win.searchActive
        x: 262 * u - win.contentShift; y: 66 * u
        height: 28 * win.tu
        spacing: 6 * u
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: "Search:  "; color: "white"; opacity: .55
            font.family: win.fontFamily; font.pixelSize: 12 * win.tu
        }
        Row {   // tag search: "Tag: ● Name — everywhere"
            visible: win.searchTag !== ""
            anchors.verticalCenter: parent.verticalCenter
            spacing: 6 * win.u
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: 11 * win.tu; height: width; radius: width / 2; color: win.tagColor(win.searchTag)
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: (win.tagDef(win.searchTag) ? win.tagDef(win.searchTag).name : win.searchTag) + "  ·  everywhere"
                color: "white"
                font.family: win.fontFamily; font.pixelSize: 12 * win.tu; font.weight: Font.DemiBold
            }
        }
        Repeater {
            model: win.searchTag ? [] : [win.dir === "/" ? "/" : "“" + win.dir.split("/").pop() + "”", "Subfolders", "Home"]
            Rectangle {
                required property string modelData
                required property int index
                anchors.verticalCenter: parent.verticalCenter
                height: 22 * win.tu; width: scopeLabel.implicitWidth + 22 * win.u; radius: height / 2
                color: win.searchScope === index ? Qt.rgba(1, 1, 1, .2) : scopeHover.hovered ? Qt.rgba(1, 1, 1, .08) : "transparent"
                Text {
                    id: scopeLabel
                    anchors.centerIn: parent
                    text: modelData; color: "white"
                    font.family: win.fontFamily; font.pixelSize: 12 * win.tu
                    font.weight: win.searchScope === index ? Font.DemiBold : Font.Normal
                }
                HoverHandler { id: scopeHover }
                TapHandler { onTapped: win.searchScope = index }
            }
        }
    }
    Text {
        visible: win.searchActive && !win.searching && resultsModel.count === 0
        x: 240 * u - win.contentShift + (parent.width - 240 * u + win.contentShift - win.previewW) / 2 - width / 2
        y: parent.height / 2
        text: "No results"; color: "white"; opacity: .4
        font.family: win.fontFamily; font.pixelSize: 15 * win.tu
    }

    // ---- split view: the second side ---------------------------------------------------
    SplitPane {
        id: splitPane
        visible: win.splitW > 4
        x: parent.width - win.previewW - win.splitW + 4 * u; y: 66 * u
        width: Math.max(0, win.splitW - 14 * u); height: parent.height - y - win.bottomH - 4 * u
        // set once (from the saved split folder), then the side navigates on its own and saves it
        Component.onCompleted: dir = win.splitDir || win.dir
        onDirChanged: if (dir !== win.splitDir) win.splitDir = dir
        active: win.activePane === 1
        u: win.u; tu: win.tu; fontFamily: win.fontFamily
        iconSize: win.listIcon
        assetFor: win.asset
        tagsFor: win.tagsOf
        colorFor: win.tagColor
        showHidden: win.showHidden
        onActivated: { win.activePane = 1; keys.forceActiveFocus() }
        onDropRequested: (urls, target, proposedMove) => win.dropFiles(urls, target, proposedMove)
        onDragStarted: on => win.internalDrag = on
        onOpenFile: p => Fm.open(p)
        onMenuRequested: splitMenu.open()
    }

    // ---- preview pane (Ctrl+Shift+P) ----------------------------------------------------
    PreviewPane {
        id: preview
        objectName: "previewPane"
        visible: win.previewW > 1
        x: parent.width - win.previewW; y: 72 * u
        width: 270 * u; height: parent.height - 72 * u - win.bottomH - 8 * u
        clip: true
        path: win.focusedPath() || win.dir
        u: win.u; tu: win.tu; fontFamily: win.fontFamily
        assetFor: win.asset
        sizes: win.folderSizes
        onOpenRequested: p => Fm.open(p)
        onInfoRequested: win.getInfo()
    }

    // ---- tabs (IRIX notebook tabs along the bottom of the page) ------------------------
    TabStrip {
        id: tabStrip
        visible: win.tabBarShown
        x: 262 * u - win.contentShift; y: parent.height - win.bottomH
        width: parent.width - x - win.previewW - win.splitW - 20 * u; height: win.tabBarH
        tabs: win.tabModel
        current: win.tabIndex
        u: win.u; tu: win.tu; fontFamily: win.fontFamily
        springDelay: win.springDelay
        onActivated: i => win.switchTab(i)
        onCloseRequested: i => win.closeTab(i)
        onMiddleClicked: i => win.closeTab(i)
        onMenuRequested: i => { tabMenu.index = i; tabMenu.open() }
        onNewRequested: win.newTab(win.dir, true)
        onOverflowRequested: tabListMenu.open()
        onFilesDropped: (i, urls, move) => {
            const target = i === win.tabIndex ? win.dir : (win.tabs[i] ? win.tabs[i].dir : "")
            if (!target) return
            if (target === Fm.trashDir || target.startsWith(Fm.trashDir + "/")) Fm.trash(win.urlsToPaths(urls))
            else Fm.drop(urls, target, move)
        }
    }

    // ---- status bar ------------------------------------------------------------
    Rectangle {
        visible: !win.tabBarShown   // with tabs, the strip draws the page edge
        x: 240 * u - win.contentShift; y: parent.height - 52 * u; width: parent.width - x - 10 * u; height: 1
        color: Qt.rgba(1, 1, 1, .08)
    }
    Text {
        id: statusText
        objectName: "status"
        x: 262 * u - win.contentShift; anchors.baseline: parent.top; anchors.baselineOffset: parent.height - 28 * u
        readonly property int n: win.paths().length
        text: win.searchActive
              ? (win.searching ? "Searching… " : "") + win.searchResults.length + (win.searchResults.length === 1 ? " result" : " results")
                + (win.searchTruncated ? " (first " + win.searchResults.length + ")" : "")
              : (n ? n + " of " + win.itemCount + " selected" : win.itemCount + (win.itemCount === 1 ? " item" : " items"))
                + (win.inTrash ? " in Trash" : " · " + (folder.count >= 0 ? Fm.freeSpace(win.dir) : ""))
        color: "white"; opacity: .55
        font.family: win.fontFamily; font.pixelSize: 12 * tu
    }
    Pill {   // the Trash's "Empty" button, as in Finder
        visible: win.inTrash && folder.count > 0 && !win.searchActive
        x: statusText.x + statusText.width + 14 * u; y: parent.height - 41 * u
        height: 22 * u; width: emptyLabel.implicitWidth + 24 * u
        Text {
            id: emptyLabel
            anchors.centerIn: parent
            text: "Empty"; color: "white"
            font.family: win.fontFamily; font.pixelSize: 12 * win.tu
        }
        TapHandler { onTapped: emptyDialog.open() }
    }
    Text {
        anchors.right: parent.right; anchors.rightMargin: 258 * u
        anchors.baseline: parent.top; anchors.baselineOffset: parent.height - 28 * u
        text: "Icon size"; color: "white"; opacity: .55
        font.family: win.fontFamily; font.pixelSize: 12 * tu
    }
    Thumbwheel {
        id: wheelCtl
        objectName: "thumbwheel"
        u: win.u
        x: parent.width - 244 * u; y: parent.height - 42 * u; width: 190 * u; height: 22 * u
        value: win.wheelValue
        onValueChanged: win.wheelValue = value
    }
    // The wheel's own value is set by dragging (which breaks a binding), so keep it in step here.
    Connections {
        target: win
        function onWheelValueChanged() { if (Math.abs(wheelCtl.value - win.wheelValue) > 1e-6) wheelCtl.value = win.wheelValue }
    }
    // IRIX "home button" (the small box at the end of the thumbwheel): back to the standard icon size.
    Item {
        id: homeButton
        objectName: "homeButton"
        x: parent.width - 48 * u; y: parent.height - 42 * u; width: 22 * u; height: 22 * u
        readonly property bool atHome: Math.abs(win.wheelValue - win.defaultWheel) < .005
        Rectangle { anchors.fill: parent; radius: 5 * win.u; color: Qt.rgba(11 / 255, 12 / 255, 16 / 255, .6) }
        Rectangle {
            anchors.fill: parent; anchors.margins: 3 * win.u; radius: 3 * win.u
            gradient: Gradient {
                GradientStop { position: 0; color: homeHover.hovered ? "#a9aebb" : "#8a8f9c" }
                GradientStop { position: 1; color: "#3a3d46" }
            }
            Rectangle {   // the inner square, as on SGI's home button
                anchors.centerIn: parent; width: parent.width * .42; height: width
                color: "transparent"; border.color: "#0d0e12"; border.width: 1.6 * win.u
                Rectangle { anchors.centerIn: parent; width: parent.width * .42; height: width; color: "#0d0e12"; opacity: homeButton.atHome ? .35 : 1 }
            }
        }
        HoverHandler { id: homeHover; cursorShape: Qt.PointingHandCursor }
        TapHandler { onTapped: win.wheelValue = win.defaultWheel }
        ToolTip.visible: homeHover.hovered
        ToolTip.delay: 700
        ToolTip.text: "Standard icon size"
    }

    QuickLook {
        anchors.fill: parent
        z: 20
        visible: win.quickLook !== ""
        path: win.quickLook
        u: win.u; tu: win.tu; fontFamily: win.fontFamily
        assetFor: win.asset
        onCloseRequested: { win.quickLook = ""; keys.forceActiveFocus() }
        onOpenRequested: p => { win.quickLook = ""; Fm.open(p) }
    }
    Component { id: infoComponent; InfoWindow {} }

    // Resize handles along the edges (the window has no KWin frame).
    Repeater {
        model: [
            { e: Qt.LeftEdge, c: Qt.SizeHorCursor }, { e: Qt.RightEdge, c: Qt.SizeHorCursor },
            { e: Qt.TopEdge, c: Qt.SizeVerCursor }, { e: Qt.BottomEdge, c: Qt.SizeVerCursor },
            { e: Qt.TopEdge | Qt.LeftEdge, c: Qt.SizeFDiagCursor }, { e: Qt.BottomEdge | Qt.RightEdge, c: Qt.SizeFDiagCursor },
            { e: Qt.TopEdge | Qt.RightEdge, c: Qt.SizeBDiagCursor }, { e: Qt.BottomEdge | Qt.LeftEdge, c: Qt.SizeBDiagCursor }
        ]
        Item {
            required property var modelData
            readonly property int t: 6 * win.u
            readonly property int c: 18 * win.u
            readonly property bool l: modelData.e & Qt.LeftEdge
            readonly property bool rr: modelData.e & Qt.RightEdge
            readonly property bool tp: modelData.e & Qt.TopEdge
            readonly property bool bt: modelData.e & Qt.BottomEdge
            readonly property bool corner: (l || rr) && (tp || bt)
            visible: !win.maximized
            x: rr ? win.width - (corner ? c : t) : (l ? 0 : c)
            y: bt ? win.height - (corner ? c : t) : (tp ? 0 : c)
            width: corner ? c : (l || rr ? t : win.width - 2 * c)
            height: corner ? c : (tp || bt ? t : win.height - 2 * c)
            z: 10
            HoverHandler { cursorShape: parent.modelData.c }
            DragHandler { target: null; onActiveChanged: if (active) win.startSystemResize(parent.modelData.e) }
        }
    }

    // ---- menus -----------------------------------------------------------------
    // Sidebar menus: `si`/`ii` say which section/entry was right-clicked (ii = -1 for a header).
    P.Menu {
        id: placeMenu
        property int si: 0
        property int ii: -1
        readonly property var entry: ii >= 0 && win.places[si] ? win.places[si].items[ii] : null
        P.MenuItem { text: "Open in New Tab"; visible: !!(placeMenu.entry && placeMenu.entry.path); onTriggered: win.newTab(win.expand(placeMenu.entry.path), true) }
        P.MenuItem { text: "Open in New Window"; visible: !!(placeMenu.entry && placeMenu.entry.path); onTriggered: Fm.newWindow(win.expand(placeMenu.entry.path)) }
        P.MenuItem { text: "Rename…"; onTriggered: win.renamingPlace = placeMenu.si + "/" + placeMenu.ii }
        P.MenuSeparator {}
        P.MenuItem { text: "Move Up"; enabled: placeMenu.ii > 0; onTriggered: win.placeMove(placeMenu.si, placeMenu.ii, -1) }
        P.MenuItem { text: "Move Down"; enabled: !!win.places[placeMenu.si] && placeMenu.ii < win.places[placeMenu.si].items.length - 1; onTriggered: win.placeMove(placeMenu.si, placeMenu.ii, 1) }
        Instantiator {
            model: win.places.length
            delegate: P.MenuItem {
                required property int index
                visible: index !== placeMenu.si
                text: "Move to " + (win.places[index] ? win.places[index].title : "")
                onTriggered: win.placeMoveTo(placeMenu.si, placeMenu.ii, index)
            }
            onObjectAdded: (i, o) => placeMenu.insertItem(5 + i, o)
            onObjectRemoved: (i, o) => placeMenu.removeItem(o)
        }
        P.MenuSeparator {}
        P.MenuItem { text: "Remove from Sidebar"; onTriggered: win.placeRemove(placeMenu.si, placeMenu.ii) }
    }
    P.Menu {
        id: sectionMenu
        readonly property string sectionTitle: win.places[placeMenu.si] ? win.places[placeMenu.si].title : ""
        P.MenuItem { text: (win.isCollapsed(sectionMenu.sectionTitle) ? "Open “" : "Fold “") + sectionMenu.sectionTitle + "”"; onTriggered: win.toggleSection(sectionMenu.sectionTitle) }
        P.MenuSeparator {}
        P.MenuItem { text: "Add Current Folder"; onTriggered: win.placeAdd(placeMenu.si, win.dir) }
        P.MenuItem { text: "Add Folder…"; onTriggered: { folderPicker.si = placeMenu.si; folderPicker.folder = "file://" + win.dir; folderPicker.open() } }
        P.MenuSeparator {}
        P.MenuItem { text: "New Section"; onTriggered: win.newSection() }
        P.MenuItem { text: "Rename Section…"; onTriggered: win.renamingSection = placeMenu.si }
        P.MenuItem { text: "Move Section Up"; enabled: placeMenu.si > 0; onTriggered: win.moveSection(placeMenu.si, -1) }
        P.MenuItem { text: "Move Section Down"; enabled: placeMenu.si < win.places.length - 1; onTriggered: win.moveSection(placeMenu.si, 1) }
        P.MenuItem { text: "Remove Section…"; onTriggered: removeSectionDialog.open() }
        P.MenuSeparator {}
        P.MenuItem { text: "Reset Sidebar"; onTriggered: win.resetPlaces() }
    }
    P.MessageDialog {
        id: removeSectionDialog
        title: "Remove Section"
        text: "Remove the section “" + sectionMenu.sectionTitle + "” from the sidebar?"
        informativeText: "Only the sidebar entries go; the folders themselves aren't touched."
        buttons: P.MessageDialog.Cancel | P.MessageDialog.Ok
        onOkClicked: win.removeSection(placeMenu.si)
    }
    P.FolderDialog {
        id: folderPicker
        property int si: 0
        title: "Add Folder to " + (win.places[si] ? win.places[si].title : "Sidebar")
        onAccepted: win.placeAdd(si, decodeURIComponent(folder.toString().replace(/^file:\/\//, "")))
    }

    P.Menu {
        id: itemMenu
        P.MenuItem { text: "Open"; onTriggered: { for (const p of win.paths()) win.activate(p, Fm.kind(p) === "folder") } }
        P.MenuItem { text: "Show in Enclosing Folder"; visible: win.searchActive; onTriggered: { const p = win.focusedPath(); search.text = ""; win.showInFolder(p) } }
        P.Menu {
            id: openWithMenu
            title: "Open With"
            Instantiator {
                model: win.paths().length ? Fm.appsFor(win.focusedPath()) : []
                delegate: P.MenuItem {
                    required property var modelData
                    text: modelData.name
                    icon.name: modelData.icon
                    onTriggered: Fm.openWith(win.paths(), modelData.id)
                }
                onObjectAdded: (i, o) => openWithMenu.insertItem(i, o)
                onObjectRemoved: (i, o) => openWithMenu.removeItem(o)
            }
            P.MenuSeparator {}
            P.MenuItem { text: "Other…"; onTriggered: Fm.openWith(win.paths(), "") }
        }
        P.MenuItem { text: "Open in New Tab"; visible: win.paths().length > 0 && win.paths().every(p => Fm.kind(p) === "folder"); onTriggered: win.openInNewTab(win.paths(), true) }
        P.MenuItem { text: "Copy to Other Side\tF5"; visible: win.splitView; onTriggered: { win.activePane = 0; win.sendToOtherSide(false) } }
        P.MenuItem { text: "Move to Other Side\tF6"; visible: win.splitView; onTriggered: { win.activePane = 0; win.sendToOtherSide(true) } }
        P.MenuItem { text: "Open in Dolphin"; onTriggered: Fm.openInDolphin(win.paths()[0]) }
        P.MenuSeparator {}
        P.Menu {
            id: itemTagsMenu
            title: "Tags"
            Instantiator {
                model: win.tagDefs
                delegate: P.MenuItem {
                    required property var modelData
                    required property int index
                    text: modelData.name + (index < 7 ? "\tCtrl+Alt+" + (index + 1) : "")
                    icon.source: win.tagIcon(modelData.color)
                    checkable: true
                    checked: win.selectionHasTag(modelData.name)
                    onTriggered: win.toggleTag(modelData.name)
                }
                onObjectAdded: (i, o) => itemTagsMenu.insertItem(i, o)
                onObjectRemoved: (i, o) => itemTagsMenu.removeItem(o)
            }
            P.MenuSeparator {}
            P.MenuItem { text: "New Tag…"; onTriggered: win.newTag(true) }
            P.MenuItem { text: "Remove All Tags\tCtrl+Alt+0"; onTriggered: win.clearSelectionTags() }
        }
        P.MenuItem { text: "Put Back"; visible: win.inTrash; onTriggered: win.putBack() }
        P.MenuItem { text: "Delete Immediately…"; visible: win.inTrash; onTriggered: deleteDialog.open() }
        P.MenuSeparator { visible: win.inTrash }
        P.MenuItem { text: "Get Info"; onTriggered: win.getInfo() }
        P.MenuItem { text: "Quick Look"; onTriggered: win.toggleQuickLook() }
        P.MenuSeparator {}
        P.MenuItem { text: "Rename…"; enabled: win.paths().length === 1; onTriggered: win.renaming = win.paths()[0] }
        P.MenuItem { text: "Duplicate"; onTriggered: win.duplicateSelected() }
        P.MenuItem { text: "Make Alias"; onTriggered: win.aliasSelected() }
        P.MenuItem {
            text: win.paths().length === 1 ? "Compress “" + win.paths()[0].split("/").pop() + "”" : "Compress " + win.paths().length + " Items"
            onTriggered: win.compressSelected()
        }
        P.MenuItem { text: "Expand Here"; visible: win.paths().length > 0 && win.paths().every(p => Fm.isArchive(p)); onTriggered: win.expandSelected() }
        P.MenuItem { text: "Show Original"; visible: win.paths().length === 1 && Fm.showOriginal(win.paths()[0]) !== ""; onTriggered: win.showOriginal() }
        P.MenuItem { text: "Copy"; onTriggered: Fm.copyToClipboard(win.paths()) }
        P.MenuItem { text: "Copy Path"; onTriggered: Fm.copyPathText(win.paths().join("\n")) }
        P.Menu {
            id: addToSidebar
            title: "Add to Sidebar"
            Instantiator {
                model: win.places.length
                delegate: P.MenuItem {
                    required property int index
                    text: win.places[index] ? win.places[index].title : ""
                    onTriggered: { for (const p of win.paths()) if (Fm.kind(p) === "folder") win.placeAdd(index, p) }
                }
                onObjectAdded: (i, o) => addToSidebar.insertItem(i, o)
                onObjectRemoved: (i, o) => addToSidebar.removeItem(o)
            }
        }
        P.MenuSeparator {}
        P.MenuItem { text: "Move to Trash"; visible: !win.inTrash; onTriggered: win.trashSelected() }
    }
    P.Menu {
        id: bgMenu
        P.MenuItem { text: "New Folder"; onTriggered: win.newFolder() }
        P.MenuItem { text: "Paste"; onTriggered: Fm.paste(win.dir) }
        P.MenuItem { text: "Add Current Folder to Favorites"; onTriggered: win.placeAdd(0, win.dir) }
        P.MenuItem { text: "Get Info"; onTriggered: win.getInfo() }
        P.Menu {
            title: "Sort By"
            P.MenuItem { text: "Name"; checkable: true; checked: win.sortField === FolderListModel.Name; onTriggered: win.sortBy(FolderListModel.Name) }
            P.MenuItem { text: "Kind"; checkable: true; checked: win.sortField === FolderListModel.Type; onTriggered: win.sortBy(FolderListModel.Type) }
            P.MenuItem { text: "Date Modified"; checkable: true; checked: win.sortField === FolderListModel.Time; onTriggered: win.sortBy(FolderListModel.Time) }
            P.MenuItem { text: "Size"; checkable: true; checked: win.sortField === FolderListModel.Size; onTriggered: win.sortBy(FolderListModel.Size) }
        }
        P.MenuSeparator {}
        P.MenuItem { text: "Open Terminal Here"; onTriggered: Fm.openTerminal(win.dir) }
        P.MenuItem { text: "Open in Dolphin"; onTriggered: Fm.openInDolphin(win.dir) }
        P.MenuSeparator {}
        P.MenuItem { text: "Show Hidden Files"; checkable: true; checked: win.showHidden; onTriggered: win.showHidden = !win.showHidden }
    }
    P.Menu {
        id: tagMenu
        property var tag: null
        readonly property string name: tag ? tag.name : ""
        P.MenuItem { text: "Show Items Tagged “" + tagMenu.name + "”"; onTriggered: win.showTagged(tagMenu.name) }
        P.MenuItem { text: "Rename “" + tagMenu.name + "”…"; onTriggered: win.renamingTag = tagMenu.name }
        P.Menu {
            id: tagColorMenu
            title: "Colour"
            Instantiator {
                model: win.nordColors
                delegate: P.MenuItem {
                    required property var modelData
                    text: modelData[0]
                    icon.source: win.tagIcon(modelData[1])
                    checkable: true
                    checked: !!tagMenu.tag && tagMenu.tag.color === modelData[1]
                    onTriggered: win.setTagColor(tagMenu.name, modelData[1])
                }
                onObjectAdded: (i, o) => tagColorMenu.insertItem(i, o)
                onObjectRemoved: (i, o) => tagColorMenu.removeItem(o)
            }
        }
        P.MenuSeparator {}
        P.MenuItem { text: "Move Up"; onTriggered: win.moveTag(tagMenu.name, -1) }
        P.MenuItem { text: "Move Down"; onTriggered: win.moveTag(tagMenu.name, 1) }
        P.MenuItem { text: "Remove from Sidebar"; onTriggered: win.setTagShown(tagMenu.name, false) }
        P.MenuSeparator {}
        P.MenuItem { text: "Delete Tag “" + tagMenu.name + "”…"; onTriggered: deleteTagDialog.open() }
    }
    P.Menu {
        id: tagsHeadMenu
        P.MenuItem { text: (win.isCollapsed("Tags") ? "Open" : "Fold") + " “Tags”"; onTriggered: win.toggleSection("Tags") }
        P.MenuSeparator {}
        P.MenuItem { text: "New Tag"; onTriggered: win.newTag(false) }
        P.MenuItem { text: "New Section"; onTriggered: win.newSection() }
        P.Menu {
            id: hiddenTagsMenu
            title: "Show in Sidebar"
            Instantiator {
                model: win.tagDefs
                delegate: P.MenuItem {
                    required property var modelData
                    text: modelData.name
                    icon.source: win.tagIcon(modelData.color)
                    checkable: true
                    checked: modelData.shown
                    onTriggered: win.setTagShown(modelData.name, !modelData.shown)
                }
                onObjectAdded: (i, o) => hiddenTagsMenu.insertItem(i, o)
                onObjectRemoved: (i, o) => hiddenTagsMenu.removeItem(o)
            }
        }
        P.MenuSeparator {}
        P.MenuItem { text: "Reset Tags"; onTriggered: win.resetTags() }
    }
    P.MessageDialog {
        id: deleteTagDialog
        title: "Delete Tag"
        text: "Are you sure you want to delete the tag “" + tagMenu.name + "”?"
        informativeText: "It will be removed from all items that have it."
        buttons: P.MessageDialog.Cancel | P.MessageDialog.Ok
        onOkClicked: win.deleteTag(tagMenu.name)
    }
    P.Menu {
        id: splitMenu
        P.MenuItem { text: "Open"; enabled: splitPane.paths().length > 0; onTriggered: { for (const p of splitPane.paths()) Fm.kind(p) === "folder" ? splitPane.go(p) : Fm.open(p) } }
        P.MenuItem { text: "Copy to Other Side\tF5"; enabled: splitPane.paths().length > 0; onTriggered: { win.activePane = 1; win.sendToOtherSide(false) } }
        P.MenuItem { text: "Move to Other Side\tF6"; enabled: splitPane.paths().length > 0; onTriggered: { win.activePane = 1; win.sendToOtherSide(true) } }
        P.MenuItem { text: "Open in New Tab"; enabled: splitPane.paths().some(p => Fm.kind(p) === "folder"); onTriggered: win.openInNewTab(splitPane.paths(), true) }
        P.MenuSeparator {}
        P.MenuItem { text: "Show This Folder on the Other Side"; onTriggered: win.go(splitPane.dir) }
        P.MenuItem { text: "Move to Trash"; enabled: splitPane.paths().length > 0; onTriggered: { Fm.trash(splitPane.paths()); splitPane.selection = ({}) } }
        P.MenuSeparator {}
        P.MenuItem { text: "Close Split View\tF3"; onTriggered: win.toggleSplit() }
    }
    P.Menu {
        id: tabMenu
        property int index: 0
        P.MenuItem { text: "New Tab\tCtrl+T"; onTriggered: win.newTab(win.tabs[tabMenu.index] ? (tabMenu.index === win.tabIndex ? win.dir : win.tabs[tabMenu.index].dir) : win.dir, true) }
        P.MenuSeparator {}
        P.MenuItem { text: "Move Tab Left"; enabled: tabMenu.index > 0; onTriggered: win.moveTab(tabMenu.index, tabMenu.index - 1) }
        P.MenuItem { text: "Move Tab Right"; enabled: tabMenu.index < win.tabs.length - 1; onTriggered: win.moveTab(tabMenu.index, tabMenu.index + 1) }
        P.MenuItem {
            text: "Move Tab to New Window"; enabled: win.tabs.length > 1
            onTriggered: { const i = tabMenu.index; Fm.newWindow(i === win.tabIndex ? win.dir : win.tabs[i].dir); win.closeTab(i) }
        }
        P.MenuSeparator {}
        P.MenuItem { text: "Close Tab"; onTriggered: win.closeTab(tabMenu.index) }
        P.MenuItem { text: "Close Other Tabs"; enabled: win.tabs.length > 1; onTriggered: win.closeOtherTabs(tabMenu.index) }
        P.MenuItem { text: "Reopen Closed Tab\tCtrl+Shift+T"; enabled: win.closedTabs.length > 0; onTriggered: win.reopenClosedTab() }
    }
    P.Menu {   // the IRIX "////" stack: every tab, to jump to one that doesn't fit
        id: tabListMenu
        Instantiator {
            model: win.tabModel
            delegate: P.MenuItem {
                required property var modelData
                required property int index
                text: modelData.title
                checkable: true
                checked: index === win.tabIndex
                onTriggered: win.switchTab(index)
            }
            onObjectAdded: (i, o) => tabListMenu.insertItem(i, o)
            onObjectRemoved: (i, o) => tabListMenu.removeItem(o)
        }
    }
    P.Menu {
        id: trashMenu
        P.MenuItem { text: "Open"; onTriggered: win.go(Fm.trashDir) }
        P.MenuItem { text: "Empty Trash…"; onTriggered: emptyDialog.open() }
        P.MenuSeparator {}
        P.MenuItem { text: "Remove from Sidebar"; onTriggered: win.placeRemove(placeMenu.si, placeMenu.ii) }
    }
    P.Menu {
        id: volumeMenu
        property var vol: null
        P.MenuItem { text: "Open"; onTriggered: win.activatePlace(volumeMenu.vol) }
        P.MenuItem { text: "Eject “" + (volumeMenu.vol ? volumeMenu.vol.label : "") + "”"; enabled: !!(volumeMenu.vol && volumeMenu.vol.mounted); onTriggered: Fm.ejectVolume(volumeMenu.vol.udi) }
        P.MenuItem { text: "Get Info"; enabled: !!(volumeMenu.vol && volumeMenu.vol.mounted); onTriggered: infoComponent.createObject(null, { path: volumeMenu.vol.path, tu: win.tu, fontFamily: win.fontFamily, assetFor: win.asset }) }
    }
    P.MessageDialog {
        id: emptyDialog
        title: "Empty Trash"
        text: "Are you sure you want to permanently erase the items in the Trash?"
        informativeText: "You can't undo this action."
        buttons: P.MessageDialog.Cancel | P.MessageDialog.Ok
        onOkClicked: win.emptyTrashNow()
    }
    P.MessageDialog {
        id: deleteDialog
        title: "Delete Immediately"
        text: "Are you sure you want to delete " + (win.paths().length === 1 ? "“" + win.paths()[0].split("/").pop() + "”" : win.paths().length + " items") + " immediately?"
        informativeText: "You can't undo this action."
        buttons: P.MessageDialog.Cancel | P.MessageDialog.Ok
        onOkClicked: win.deleteNow()
    }
    P.Menu {
        id: historyMenu
        Instantiator {
            model: win.recent
            delegate: P.MenuItem {
                required property string modelData
                text: modelData
                onTriggered: win.go(modelData)
            }
            onObjectAdded: (i, o) => historyMenu.insertItem(i, o)
            onObjectRemoved: (i, o) => historyMenu.removeItem(o)
        }
    }

    // Global menu (shown in the top bar), IRIX fm style names plus the usual Finder ones.
    P.MenuBar {
        window: win
        P.Menu {
            title: "File"
            P.MenuItem { text: "New Window\tCtrl+N"; onTriggered: Fm.newWindow(win.dir) }
            P.MenuItem { text: "New Tab\tCtrl+T"; onTriggered: win.newTab(win.dir, true) }
            P.MenuItem { text: "New Folder\tCtrl+Shift+N"; onTriggered: win.newFolder() }
            P.MenuItem { text: "Open\tCtrl+O"; onTriggered: win.openSelection() }
            P.MenuSeparator {}
            P.MenuItem { text: "Get Info\tCtrl+I"; onTriggered: win.getInfo() }
            P.MenuItem { text: "Quick Look\tSpace"; onTriggered: win.toggleQuickLook() }
            P.MenuItem { text: "Duplicate\tCtrl+D"; onTriggered: win.duplicateSelected() }
            P.MenuItem { text: "Make Alias"; onTriggered: win.aliasSelected() }
            P.MenuItem { text: "Show Original\tCtrl+R"; onTriggered: win.showOriginal() }
            P.MenuSeparator {}
            P.MenuItem { text: "Open Terminal Here"; onTriggered: Fm.openTerminal(win.dir) }
            P.MenuItem { text: "Open in Dolphin"; onTriggered: Fm.openInDolphin(win.dir) }
            P.MenuSeparator {}
            P.MenuItem { text: "Compress\tCtrl+Alt+Z"; onTriggered: win.compressSelected() }
            P.MenuItem { text: "Expand Here"; onTriggered: win.expandSelected() }
            P.MenuSeparator {}
            P.MenuItem { text: "Move to Trash\tDel"; onTriggered: win.trashSelected() }
            P.MenuItem { text: "Put Back"; enabled: win.inTrash; onTriggered: win.putBack() }
            P.MenuItem { text: "Empty Trash…\tCtrl+Shift+Del"; onTriggered: emptyDialog.open() }
            P.MenuSeparator {}
            P.MenuItem { text: "Close Tab\tCtrl+W"; onTriggered: win.closeTab(win.tabIndex) }
            P.MenuItem { text: "Close Window\tCtrl+Shift+W"; onTriggered: win.close() }
            P.MenuItem { text: "Reopen Closed Tab\tCtrl+Shift+T"; enabled: win.closedTabs.length > 0; onTriggered: win.reopenClosedTab() }
        }
        P.Menu {
            title: "Edit"
            P.MenuItem { text: (Fm.undoText || "Undo") + "\tCtrl+Z"; enabled: Fm.canUndo; onTriggered: Fm.undo() }
            P.MenuSeparator {}
            P.MenuItem { text: "Copy\tCtrl+C"; onTriggered: Fm.copyToClipboard(win.paths()) }
            P.MenuItem { text: "Paste\tCtrl+V"; onTriggered: Fm.paste(win.dir) }
            P.MenuItem { text: "Move Item Here\tCtrl+Alt+V"; onTriggered: Fm.pasteMove(win.dir) }
            P.MenuItem { text: "Select All\tCtrl+A"; onTriggered: win.selectAll() }
            P.MenuItem { text: "Rename…\tF2"; onTriggered: if (win.paths().length === 1) win.renaming = win.paths()[0] }
            P.MenuItem { text: "Copy Path\tCtrl+Alt+C"; onTriggered: Fm.copyPathText(win.paths().length ? win.paths().join("\n") : win.dir) }
        }
        P.Menu {
            title: "View"
            P.MenuItem { text: "as Icons\tCtrl+1"; checkable: true; checked: win.viewMode === 0; onTriggered: win.viewMode = 0 }
            P.MenuItem { text: "as List\tCtrl+2"; checkable: true; checked: win.viewMode === 1; onTriggered: win.viewMode = 1 }
            P.MenuItem { text: "as Previews\tCtrl+3"; checkable: true; checked: win.viewMode === 2; onTriggered: win.viewMode = 2 }
            P.MenuSeparator {}
            P.Menu {
                title: "Sort By"
                P.MenuItem { text: "Name"; checkable: true; checked: win.sortField === FolderListModel.Name; onTriggered: win.sortBy(FolderListModel.Name) }
                P.MenuItem { text: "Kind"; checkable: true; checked: win.sortField === FolderListModel.Type; onTriggered: win.sortBy(FolderListModel.Type) }
                P.MenuItem { text: "Date Modified"; checkable: true; checked: win.sortField === FolderListModel.Time; onTriggered: win.sortBy(FolderListModel.Time) }
                P.MenuItem { text: "Size"; checkable: true; checked: win.sortField === FolderListModel.Size; onTriggered: win.sortBy(FolderListModel.Size) }
                P.MenuSeparator {}
                P.MenuItem { text: "Reverse Order"; checkable: true; checked: win.sortReversed; onTriggered: win.sortReversed = !win.sortReversed }
            }
            P.MenuSeparator {}
            P.MenuItem { text: "Bigger Icons\tCtrl++"; onTriggered: wheelCtl.nudge(.08) }
            P.MenuItem { text: "Smaller Icons\tCtrl+-"; onTriggered: wheelCtl.nudge(-.08) }
            P.MenuSeparator {}
            P.MenuItem { text: "Show Hidden Files\tCtrl+Shift+."; checkable: true; checked: win.showHidden; onTriggered: win.showHidden = !win.showHidden }
            P.MenuItem { text: "Calculate All Sizes"; checkable: true; checked: win.calcSizes; onTriggered: win.calcSizes = !win.calcSizes }
            P.MenuSeparator {}
            P.MenuItem { text: (win.sidebarShown ? "Hide" : "Show") + " Sidebar\tCtrl+Alt+S"; onTriggered: win.sidebarShown = !win.sidebarShown }
            P.MenuItem { text: (win.previewShown ? "Hide" : "Show") + " Preview\tCtrl+Shift+P"; onTriggered: win.previewShown = !win.previewShown }
            P.MenuItem { text: (win.tabBarShown ? "Hide" : "Show") + " Tab Bar"; onTriggered: win.tabBarShown = !win.tabBarShown }
            P.MenuItem { text: "Split View\tF3"; checkable: true; checked: win.splitView; onTriggered: win.toggleSplit() }
            P.MenuItem { text: "Open Folders in New Tabs"; checkable: true; checked: win.folderTabs; onTriggered: win.folderTabs = !win.folderTabs }
            P.MenuItem { text: "Standard Icon Size"; onTriggered: win.wheelValue = win.defaultWheel }
        }
        P.Menu {
            title: "Go"
            P.MenuItem { text: "Back\tCtrl+["; onTriggered: win.goBack() }
            P.MenuItem { text: "Forward\tCtrl+]"; onTriggered: win.goForward() }
            P.MenuItem { text: "Enclosing Folder\tCtrl+Up"; onTriggered: win.goUp() }
            P.MenuItem { text: "Go to Folder…\tCtrl+Shift+G"; onTriggered: win.editPath() }
            P.Menu {
                id: recentMenu
                title: "Recent Folders"
                Instantiator {
                    model: win.recent
                    delegate: P.MenuItem {
                        required property string modelData
                        text: win.shorten(modelData)
                        onTriggered: win.go(modelData)
                    }
                    onObjectAdded: (i, o) => recentMenu.insertItem(i, o)
                    onObjectRemoved: (i, o) => recentMenu.removeItem(o)
                }
                P.MenuSeparator {}
                P.MenuItem { text: "Clear Menu"; onTriggered: win.clearRecent() }
            }
            P.MenuSeparator {}
            P.MenuItem { text: "Home\tCtrl+Shift+H"; onTriggered: win.go(Fm.home) }
            P.MenuItem { text: "Desktop\tCtrl+Shift+D"; onTriggered: win.go(Fm.home + "/Desktop") }
            P.MenuItem { text: "Documents\tCtrl+Shift+O"; onTriggered: win.go(Fm.home + "/Documents") }
            P.MenuItem { text: "Downloads\tCtrl+Alt+L"; onTriggered: win.go(Fm.home + "/Downloads") }
            P.MenuItem { text: "Applications\tCtrl+Shift+A"; onTriggered: win.go(Fm.home + "/Applications") }
            P.MenuItem { text: "Icon Catalog"; onTriggered: win.go("/usr/share/applications") }
            P.MenuItem { text: "Computer\tCtrl+Shift+C"; onTriggered: win.go("/") }
            P.MenuItem { text: "Network\tCtrl+Shift+K"; onTriggered: Fm.openInDolphin("remote:/") }
            P.MenuItem { text: "Trash"; onTriggered: win.go(Fm.trashDir) }
        }
        P.Menu {
            title: "Window"
            P.MenuItem { text: "Minimize\tCtrl+M"; onTriggered: win.showMinimized() }
            P.MenuItem { text: "Next Tab\tCtrl+PgDown"; onTriggered: win.cycleTab(1) }
            P.MenuItem { text: "Previous Tab\tCtrl+PgUp"; onTriggered: win.cycleTab(-1) }
            P.MenuItem { text: "Zoom"; onTriggered: win.maximized ? win.showNormal() : win.showMaximized() }
            P.MenuItem { text: "Desks Overview"; onTriggered: Fm.overview() }
        }
        P.Menu {
            title: "Help"
            P.MenuItem { text: "fm Help"; onTriggered: Qt.openUrlExternally("file://" + Fm.assetDir + "/README.md") }
        }
    }

    Shortcut { sequence: "Ctrl+N"; onActivated: Fm.newWindow(win.dir) }
    Shortcut { sequence: "Ctrl+Shift+N"; onActivated: win.newFolder() }
    Shortcut { sequence: "Ctrl+W"; onActivated: win.closeTab(win.tabIndex) }
    Shortcut { sequence: "Ctrl+Shift+W"; onActivated: win.close() }
    Shortcut { sequence: "Ctrl+T"; onActivated: win.newTab(win.dir, true) }
    Shortcut { sequence: "F3"; onActivated: win.toggleSplit() }
    Shortcut { sequence: "F5"; enabled: win.splitView; onActivated: win.sendToOtherSide(false) }
    Shortcut { sequence: "F6"; enabled: win.splitView; onActivated: win.sendToOtherSide(true) }
    Shortcut { sequence: "Ctrl+Shift+T"; onActivated: win.reopenClosedTab() }
    // Next / previous tab (IRIX used Page Down / Page Up for Icon Catalog pages).
    Shortcut { sequences: ["Ctrl+Tab", "Ctrl+PgDown"]; onActivated: win.cycleTab(1) }
    Shortcut { sequences: ["Ctrl+Shift+Tab", "Ctrl+Backtab", "Ctrl+Shift+Backtab", "Ctrl+PgUp"]; onActivated: win.cycleTab(-1) }
    // Alt+1…8 jump to a tab, Alt+9 to the last one (Ctrl+digit is views, Ctrl+Alt+digit is tags).
    Repeater {
        model: 9
        Item {
            required property int index
            Shortcut { sequence: "Alt+" + (index + 1); onActivated: win.switchTab(index === 8 ? win.tabs.length - 1 : index) }
        }
    }
    Shortcut { sequence: "Ctrl+M"; onActivated: win.showMinimized() }
    Shortcut { sequences: [StandardKey.Copy]; enabled: !search.activeFocus && !win.renaming; onActivated: Fm.copyToClipboard(win.paths()) }
    Shortcut { sequences: [StandardKey.Paste]; enabled: !search.activeFocus && !win.renaming; onActivated: Fm.paste(win.dir) }
    Shortcut { sequences: [StandardKey.SelectAll]; enabled: !search.activeFocus && !win.renaming; onActivated: win.selectAll() }
    Shortcut { sequence: "Ctrl+F"; onActivated: search.forceActiveFocus() }
    Shortcut { sequences: ["Ctrl+L", "Ctrl+Shift+G"]; onActivated: win.editPath() }
    Shortcut { sequences: ["Ctrl+H", "Ctrl+Shift+>", "Ctrl+Shift+."]; onActivated: win.showHidden = !win.showHidden }
    Shortcut { sequence: "Ctrl+O"; onActivated: win.openSelection() }
    Shortcut { sequence: "Ctrl+I"; onActivated: win.getInfo() }
    Shortcut { sequence: "Ctrl+D"; enabled: !search.activeFocus && !win.editingPath; onActivated: win.duplicateSelected() }
    Shortcut { sequence: "Ctrl+R"; onActivated: win.showOriginal() }
    Shortcut { sequence: "Ctrl+Z"; enabled: !search.activeFocus && !win.editingPath && !win.renaming; onActivated: Fm.undo() }
    Shortcut { sequence: "Ctrl+Alt+V"; onActivated: Fm.pasteMove(win.dir) }
    Shortcut { sequence: "Ctrl+Alt+C"; onActivated: Fm.copyPathText(win.paths().length ? win.paths().join("\n") : win.dir) }
    Shortcut { sequence: "Ctrl+["; onActivated: win.goBack() }
    Shortcut { sequence: "Ctrl+]"; onActivated: win.goForward() }
    Shortcut { sequence: "Ctrl+Shift+D"; onActivated: win.go(Fm.home + "/Desktop") }
    Shortcut { sequence: "Ctrl+Shift+O"; onActivated: win.go(Fm.home + "/Documents") }
    Shortcut { sequence: "Ctrl+Alt+L"; onActivated: win.go(Fm.home + "/Downloads") }
    Shortcut { sequence: "Ctrl+Shift+A"; onActivated: win.go(Fm.home + "/Applications") }
    Shortcut { sequence: "Ctrl+Shift+C"; onActivated: win.go("/") }
    Shortcut { sequence: "Ctrl+Shift+K"; onActivated: Fm.openInDolphin("remote:/") }
    Shortcut { sequence: "Ctrl+Alt+S"; onActivated: win.sidebarShown = !win.sidebarShown }
    Shortcut { sequence: "Ctrl+Shift+P"; onActivated: win.previewShown = !win.previewShown }
    Shortcut { sequence: "Ctrl+Shift+Del"; onActivated: emptyDialog.open() }
    Shortcut { sequence: "Ctrl+Alt+Z"; onActivated: win.compressSelected() }
    // Tags: Ctrl+Alt+1…7 toggle the first seven tags on the selection, Ctrl+Alt+0 removes all.
    Repeater {
        model: 7
        Item {
            required property int index
            Shortcut { sequence: "Ctrl+Alt+" + (index + 1); onActivated: win.toggleTagIndex(index) }
        }
    }
    Shortcut { sequence: "Ctrl+Alt+0"; onActivated: win.clearSelectionTags() }
    Shortcut { sequence: "Ctrl+Shift+H"; onActivated: win.go(Fm.home) }
    Shortcut { sequence: "Ctrl+1"; onActivated: win.viewMode = 0 }
    Shortcut { sequence: "Ctrl+2"; onActivated: win.viewMode = 1 }
    Shortcut { sequence: "Ctrl+3"; onActivated: win.viewMode = 2 }
    Shortcut { sequences: ["Ctrl++", "Ctrl+="]; onActivated: wheelCtl.nudge(.08) }
    Shortcut { sequence: "Ctrl+-"; onActivated: wheelCtl.nudge(-.08) }
}
