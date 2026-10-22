import QtQuick
import QtQuick.Controls
import QtQuick.Shapes
import Qt.labs.folderlistmodel
import org.kde.kirigami as K
import IrixFm

// The second side of the split view (F3): a list of its own folder, with a path bar, back / up,
// selection and drag and drop. Dragging between the sides moves (Ctrl copies); F5 / F6 copy /
// move the active side's selection to the other side (handled by the window).
Item {
    id: pane
    objectName: "splitPane"
    property string dir
    property var back: []
    property var selection: ({})
    property int anchorIndex: -1
    property bool active: false
    property real u: 1
    property real tu: 1
    property real iconSize: 26
    property string fontFamily
    property var assetFor
    property var tagsFor                 // function(path) -> tags
    // list columns, shared with the main side (the window's listColumns / columnDefs)
    property var columnDefs: ({})
    property var columnsFor              // function(width) -> keys that fit
    property var kindTextFor             // function(isDir, kind) -> "Folder", "Image", …
    property int sortField: FolderListModel.Name
    property bool sortReversed: false
    signal sortRequested(int field)
    signal columnsMenuRequested()
    readonly property var listCols: columnsFor ? columnsFor(rows.width - iconSize - 20 * u) : []
    readonly property real listNameW: rows.width - iconSize - 20 * u - listCols.reduce((s, k) => s + columnDefs[k][1] * u, 0)
    property var colorFor                // function(tag) -> colour
    property bool showHidden: false
    // Its own view (0 icons, 1 list, 2 previews), set by the toolbar's view buttons while active.
    property int viewMode: 1
    property real k: 1                   // icon units → px (the window's icon size)
    property real cellSize: 140
    signal activated()
    signal zoomRequested(real delta)     // Ctrl+wheel: this side's icon size
    signal dropRequested(var urls, string target, bool proposedMove)
    signal menuRequested()
    signal openFile(string path)
    signal dragStarted(bool on)
    signal lastTabClosed()
    signal terminalRequested(string kind, var args)   // the window creates it in termLayer
    signal terminalClosed(int termId)
    signal terminalCdRequested(int termId, string path)

    // terminal tabs on this side
    property int activeTermId: 0
    readonly property bool termActive: activeTermId !== 0
    property alias termLayer: termLayer
    property int titleRev: 0
    function openTerminal(kind) {
        const files = paths().filter(p => Fm.kind(p) !== "folder")
        terminalRequested(kind, kind === "nvim" ? (files.length ? files : ["."]) : kind === "web" ? files : [])
    }
    function addTerminalTab(kind, id) {
        saveTab()
        tabs = tabs.concat([{ dir: dir, back: [], term: kind, termId: id }])
        loadTab(tabs.length - 1)
    }
    function closeTerminalTab(id) {
        const i = tabs.findIndex(t => t.termId === id)
        if (i >= 0) closeTab(i)
    }

    // ---- this side's own tabs: the active one is live (dir, back, selection), others are snapshots
    property var tabs: [{ dir: "", back: [] }]
    property int tabIndex: 0
    property var closedTabs: []
    property var termTitleFor: null      // function(tab) -> title (from the window, which owns the terminals)
    readonly property var tabModel: tabs.map((t, i) => {
        // a dependency only: re-evaluate when it changes
        titleRev   // qmllint disable confusing-expression-statement
        const d = i === tabIndex ? dir : t.dir
        if (t.term && termTitleFor) return { title: termTitleFor(t), dir: d }
        return { title: d === "/" ? "/" : Kio.isUrl(d) ? Kio.title(d, Kio.rev) : (d.split("/").pop() || d), dir: d }
    })
    function saveTab() {
        if (tabIndex < 0 || tabIndex >= tabs.length) return
        const t = tabs.slice(); t[tabIndex] = Object.assign({}, tabs[tabIndex], { dir: dir, back: back.slice(-20) }); tabs = t
    }
    function loadTab(i) {
        const t = tabs[i]
        if (!t) return
        tabIndex = i
        activeTermId = t.termId || 0
        back = t.back || []
        dir = t.dir
        selection = ({}); anchorIndex = -1
    }
    function switchTab(i) { if (i !== tabIndex && i >= 0 && i < tabs.length) { saveTab(); loadTab(i) } }
    function cycleTab(d) { if (tabs.length > 1) switchTab((tabIndex + d + tabs.length) % tabs.length) }
    function newTab(path, activate) {
        saveTab()
        tabs = tabs.concat([{ dir: path, back: [] }])
        if (activate) loadTab(tabs.length - 1)
    }
    function openInNewTab(paths, activate) {
        const dirs = paths.filter(p => Fm.kind(p) === "folder")
        dirs.forEach((p, k) => newTab(p, activate && k === dirs.length - 1))
    }
    // A sidebar place on this side: its existing tab, or a new one.
    function openTabFor(path) {
        for (let i = 0; i < tabs.length; ++i)
            if ((i === tabIndex ? dir : tabs[i].dir) === path) { switchTab(i); return }
        newTab(path, true)
    }
    function closeTab(i) {
        const closing = tabs[i]
        if (closing && closing.term) terminalClosed(closing.termId)   // ends its program
        if (tabs.length <= 1) { if (closing && closing.term) { tabs = [{ dir: dir, back: [] }]; tabIndex = -1; loadTab(0) } pane.lastTabClosed(); return }
        saveTab()
        if (!(closing && closing.term)) closedTabs = closedTabs.concat([tabs[i]]).slice(-20)
        const t = tabs.slice(); t.splice(i, 1)
        const wasActive = i === tabIndex
        const next = tabIndex > i ? tabIndex - 1 : tabIndex
        tabs = t
        if (wasActive) { tabIndex = -1; loadTab(Math.min(i, t.length - 1)) } else tabIndex = next
    }
    function closeOtherTabs(i) {
        saveTab()
        tabs = [tabs[i]]
        tabIndex = -1
        loadTab(0)
    }
    function reopenClosedTab() {
        if (!closedTabs.length) return
        const t = closedTabs[closedTabs.length - 1]
        closedTabs = closedTabs.slice(0, -1)
        saveTab()
        tabs = tabs.concat([t])
        loadTab(tabs.length - 1)
    }
    function moveTab(from, to) {
        if (from === to || from < 0 || to < 0 || from >= tabs.length || to >= tabs.length) return
        saveTab()
        const t = tabs.slice(), [x] = t.splice(from, 1)
        t.splice(to, 0, x)
        tabIndex = tabIndex === from ? to : (from < tabIndex && to >= tabIndex ? tabIndex - 1 : from > tabIndex && to <= tabIndex ? tabIndex + 1 : tabIndex)
        tabs = t
    }

    // a KIO URL (smb://, sftp://, remote:/, mtp:/ …) is listed by kioModel, a local folder by `model`
    readonly property bool remote: Kio.isUrl(dir)
    readonly property var dirModel: remote ? kioModel : model
    readonly property int count: dirModel.count
    function paths() { return Object.keys(selection) }
    function go(path) {
        if (path && path !== dir) {
            back = back.concat([dir]); dir = path; selection = ({}); anchorIndex = -1; rows.positionViewAtBeginning()
            if (termActive) terminalCdRequested(activeTermId, path)   // the shell follows
        }
    }
    onDirChanged: Qt.callLater(saveTab)
    function goBack() { if (back.length) { const p = back[back.length - 1]; back = back.slice(0, -1); dir = p; selection = ({}); anchorIndex = -1 } }
    function goUp() {
        if (remote) { const p = Kio.parentUrl(dir); if (p) go(p); return }
        if (dir !== "/") go(dir.substring(0, dir.lastIndexOf("/")) || "/")
    }
    function select(i, mods) {
        const path = dirModel.get(i, "filePath")
        let s = Object.assign({}, selection)
        if (mods & Qt.ShiftModifier && anchorIndex >= 0) {
            s = ({})
            for (let k = Math.min(anchorIndex, i); k <= Math.max(anchorIndex, i); ++k) s[dirModel.get(k, "filePath")] = true
        } else if (mods & Qt.ControlModifier) {
            if (s[path]) delete s[path]; else s[path] = true
            anchorIndex = i
        } else { s = ({}); s[path] = true; anchorIndex = i }
        selection = s
    }
    // keyboard (the window routes keys here while this side is active)
    function focusedPath() {
        const p = anchorIndex >= 0 ? dirModel.get(anchorIndex, "filePath") : ""
        return p && selection[p] ? p : (paths()[0] || "")
    }
    readonly property int columns: viewMode === 1 ? 1 : Math.max(1, Math.floor(grid.width / grid.cellWidth))
    function moveCursor(delta) {
        if (!dirModel.count) return
        const i = Math.max(0, Math.min(dirModel.count - 1, (anchorIndex < 0 ? -delta : anchorIndex) + delta))
        select(i, 0)
        rows.positionViewAtIndex(i, ListView.Contain)
        grid.positionViewAtIndex(i, GridView.Contain)
    }
    function selectAll() { const s = ({}); for (let i = 0; i < dirModel.count; ++i) s[dirModel.get(i, "filePath")] = true; selection = s }
    function activateRow(i) {
        const path = dirModel.get(i, "filePath")
        if (dirModel.get(i, "fileIsDir")) go(path); else pane.openFile(path)
    }

    FolderListModel {
        id: model
        folder: pane.dir && !pane.remote ? "file://" + pane.dir : ""
        showDirsFirst: true
        showHidden: pane.showHidden
        showDotAndDotDot: false
        caseSensitive: false
        sortCaseSensitive: false
        sortField: pane.sortField
        sortReversed: pane.sortReversed
        onFolderChanged: if (pane.dir && !pane.remote) Fm.prefetchKinds(pane.dir)   // as on the main side
    }
    KioModel {   // qmllint disable import
        id: kioModel
        objectName: "splitKioModel"
        url: pane.remote ? pane.dir : ""
        showHidden: pane.showHidden
        sortField: pane.sortField
        sortReversed: pane.sortReversed
        onRedirected: (from, to) => { if (pane.dir === from) pane.dir = to }
    }

    // a glass panel of its own, lit when active

    MouseArea {   // background: activate, clear the selection
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        onPressed: m => { pane.activated(); if (m.button === Qt.RightButton) pane.menuRequested(); else pane.selection = ({}) }
    }
    DropArea {   // dropping on the empty part: into this side's folder
        enabled: !pane.termActive   // a terminal takes drops itself (it types the paths)
        anchors.fill: parent
        onDropped: d => { pane.dropRequested(d.urls, pane.dir, d.proposedAction === Qt.MoveAction); d.accept() }
    }

    // header (same as the main side's while split): ‹ ↑ path, then the column headings
    PaneHeader {
        id: bar
        x: 6 * pane.u; y: 4 * pane.u; width: pane.width - 12 * pane.u
        dir: pane.dir; canGoBack: pane.back.length > 0
        u: pane.u; tu: pane.tu; fontFamily: pane.fontFamily
        onActivated: pane.activated()
        onBackRequested: pane.goBack()
        onUpRequested: pane.goUp()
        onPathClicked: p => pane.go(p)
    }
    Item {
        id: columns
        visible: pane.viewMode === 1 && !pane.termActive
        // the same height as the main side's list header, so rows line up across the sides
        x: 6 * pane.u; y: bar.y + bar.height + 2 * pane.u; width: pane.width - 12 * pane.u; height: 26 * pane.tu
        Row {   // the same columns as the main side (right-click: Show Columns)
            x: pane.iconSize + 12 * pane.u; height: parent.height
            Text {
                readonly property bool active: pane.sortField === FolderListModel.Name
                width: pane.listNameW; anchors.verticalCenter: parent.verticalCenter
                text: "Name" + (active ? (pane.sortReversed ? "  ▾" : "  ▴") : "")
                color: "white"; opacity: active ? .85 : .45
                font.family: pane.fontFamily; font.pixelSize: 11 * pane.tu; font.weight: Font.DemiBold
                MouseArea {
                    anchors.fill: parent; cursorShape: Qt.PointingHandCursor; acceptedButtons: Qt.LeftButton | Qt.RightButton
                    onClicked: m => { pane.activated(); m.button === Qt.RightButton ? pane.columnsMenuRequested() : pane.sortRequested(FolderListModel.Name) }
                }
            }
            ListColumns {
                objectName: "splitListHeading"
                heading: true; cols: pane.listCols; defs: pane.columnDefs
                height: parent.height; u: pane.u; tu: pane.tu; fontFamily: pane.fontFamily
                sortField: pane.sortField; sortReversed: pane.sortReversed
                onSortRequested: f => { pane.activated(); pane.sortRequested(f) }
                onMenuRequested: { pane.activated(); pane.columnsMenuRequested() }
            }
        }
    }
    PaneFrame { anchors.fill: parent; u: pane.u; lineY: bar.y + bar.height; active: pane.active }
    Rectangle { visible: pane.viewMode === 1; x: 8 * pane.u; y: columns.y + columns.height; width: pane.width - 16 * pane.u; height: 1; color: Qt.rgba(1, 1, 1, .06) }

    ListView {
        id: rows
        objectName: "splitList"
        visible: pane.viewMode === 1 && !pane.termActive
        x: 6 * pane.u; y: columns.y + columns.height
        width: pane.width - 12 * pane.u; height: pane.height - y - 6 * pane.u
        clip: true
        reuseItems: true   // as on the main side: scrolled-out rows are refilled, not made again
        // local or KIO folder: switch with the pool drained, as on the main side (reuseItems gotcha)
        readonly property var shownModel: pane.dirModel
        Component.onCompleted: rows.model = shownModel
        onShownModelChanged: { reuseItems = false; rows.model = shownModel; reuseItems = true }
        interactive: false
        ScrollBar.vertical: GlassScrollBar { u: pane.u }
        WheelHandler {
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
            onWheel: e => {
                if (e.modifiers & Qt.ControlModifier) { pane.zoomRequested((e.pixelDelta.y !== 0 ? e.pixelDelta.y / 50 : e.angleDelta.y / 120) * .05); return }
                const dy = e.pixelDelta.y !== 0 ? e.pixelDelta.y : e.angleDelta.y / 2
                rows.contentY = Math.max(0, Math.min(Math.max(0, rows.contentHeight - rows.height), rows.contentY - dy))
            }
        }
        delegate: Item {
            id: r
            required property int index
            required property string fileName
            required property string filePath
            required property bool fileIsDir
            required property var fileSize
            required property date fileModified
            readonly property bool sel: pane.selection[filePath] === true
            readonly property string kind: fileIsDir ? "folder" : Fm.kind(filePath)
            readonly property var tags: pane.tagsFor ? pane.tagsFor(filePath) : []
            width: rows.width; height: Math.max(26 * pane.tu, pane.iconSize + 6 * pane.u)
            Rectangle {
                anchors.fill: parent; radius: 6 * pane.u
                color: r.sel ? (pane.active ? "#0a84ff" : Qt.rgba(1, 1, 1, .18)) : rowDrop.containsDrag ? Qt.rgba(1, 1, 1, .14)
                       : r.index % 2 ? Qt.rgba(1, 1, 1, .03) : "transparent"
            }
            Image {
                x: 6 * pane.u; anchors.verticalCenter: parent.verticalCenter
                width: pane.iconSize; height: width
                source: pane.assetFor ? pane.assetFor(r.kind === "app" ? "doc" : r.kind) : ""
                sourceSize: Qt.size(width * 2, height * 2); smooth: true
            }
            Text {
                id: nameText
                x: pane.iconSize + 12 * pane.u; anchors.verticalCenter: parent.verticalCenter
                width: pane.listNameW - 8 * pane.u
                text: r.fileName; elide: Text.ElideMiddle; color: "white"
                font.family: pane.fontFamily; font.pixelSize: 13 * pane.tu
                Loader {   // only tagged items have dots
                    active: r.tags.length > 0
                    x: Math.min(nameText.contentWidth, nameText.width) + 6 * pane.u
                    anchors.verticalCenter: parent.verticalCenter
                    sourceComponent: TagDots { tags: r.tags; colorFor: pane.colorFor; size: 9 * pane.tu }
                }
            }
            ListColumns {
                x: pane.iconSize + 12 * pane.u + pane.listNameW; height: parent.height
                cols: pane.listCols; defs: pane.columnDefs
                u: pane.u; tu: pane.tu; fontFamily: pane.fontFamily; sel: r.sel
                path: r.filePath; isDir: r.fileIsDir; modified: r.fileModified; size: r.fileSize; tags: r.tags
                kindText: pane.kindTextFor ? pane.kindTextFor(r.fileIsDir, r.kind) : ""
                sizeText: r.fileIsDir ? Fm.cachedFolderSize(r.filePath) || "--" : ""
            }
            Item {
                id: dragProxy
                Drag.active: rowMouse.drag.active
                Drag.dragType: Drag.Automatic
                Drag.supportedActions: Qt.CopyAction | Qt.MoveAction | Qt.LinkAction
                Drag.onActiveChanged: pane.dragStarted(Drag.active)
            }
            MouseArea {
                id: rowMouse
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton
                drag.target: dragProxy
                onPressed: m => {
                    pane.activated()
                    if (m.button !== Qt.MiddleButton && (!r.sel || m.modifiers & (Qt.ControlModifier | Qt.ShiftModifier)))
                        pane.select(r.index, m.modifiers)
                    // set now, not bound: a binding per row re-ran on every selection change
                    dragProxy.Drag.mimeData = { "text/uri-list": Fm.uriList(pane.paths()) }
                    if (m.button === Qt.MiddleButton) { if (r.fileIsDir) pane.newTab(r.filePath, false); return }
                    if (m.button === Qt.RightButton) pane.menuRequested()
                }
                onReleased: { dragProxy.x = 0; dragProxy.y = 0 }
                onDoubleClicked: m => {
                    if (m.button !== Qt.LeftButton) return
                    if (r.fileIsDir && m.modifiers & Qt.ControlModifier) pane.newTab(r.filePath, true)
                    else pane.activateRow(r.index)
                }
            }
            DropArea {   // onto a folder: into it
                id: rowDrop
                enabled: r.fileIsDir
                anchors.fill: parent
                onDropped: d => { pane.dropRequested(d.urls, r.filePath, d.proposedAction === Qt.MoveAction); d.accept() }
            }
        }
    }

    // ---- icons / previews: the same IRIX icons as the main side ----------------------------
    GridView {
        id: grid
        objectName: "splitGrid"
        visible: pane.viewMode !== 1 && !pane.termActive
        x: 6 * pane.u; y: columns.y + 6 * pane.u
        width: pane.width - 12 * pane.u; height: pane.height - y - 6 * pane.u
        clip: true
        reuseItems: true
        readonly property var shownModel: pane.dirModel
        Component.onCompleted: grid.model = shownModel
        onShownModelChanged: { reuseItems = false; grid.model = shownModel; reuseItems = true }
        interactive: false
        cellWidth: pane.cellSize; cellHeight: pane.cellSize
        ScrollBar.vertical: GlassScrollBar { u: pane.u }
        WheelHandler {
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
            onWheel: e => {
                if (e.modifiers & Qt.ControlModifier) { pane.zoomRequested((e.pixelDelta.y !== 0 ? e.pixelDelta.y / 50 : e.angleDelta.y / 120) * .05); return }
                const dy = e.pixelDelta.y !== 0 ? e.pixelDelta.y : e.angleDelta.y / 2
                grid.contentY = Math.max(0, Math.min(Math.max(0, grid.contentHeight - grid.height), grid.contentY - dy))
            }
        }
        delegate: Item {
            id: c
            required property int index
            required property string fileName
            required property string filePath
            required property bool fileIsDir
            required property url fileUrl
            readonly property bool sel: pane.selection[filePath] === true
            readonly property string kind: fileIsDir ? "folder" : Fm.kind(filePath)
            readonly property var info: kind === "app" ? Fm.desktopInfo(filePath) : null
            readonly property var tags: pane.tagsFor ? pane.tagsFor(filePath) : []
            readonly property real kk: pane.k
            width: grid.cellWidth; height: grid.cellHeight
            Item {
                id: ic
                x: (c.width - 120 * c.kk) / 2; width: 120 * c.kk; height: width
                Image {
                    anchors.fill: parent
                    visible: !(c.tags.length && !c.sel)
                    source: pane.assetFor ? pane.assetFor(c.sel || cDrop.containsDrag ? "carpet-sel" : c.kind === "exe" ? "carpet-strong" : "carpet") : ""
                    sourceSize: Qt.size(width * 2, height * 2); smooth: true
                }
                Loader {   // a tagged item's carpet in its tag colour (Loaders: most items need none of these)
                    anchors.fill: parent
                    active: c.tags.length > 0
                    visible: !c.sel
                    sourceComponent: Shape {
                        preferredRendererType: Shape.CurveRenderer
                        ShapePath {
                            strokeColor: Qt.rgba(0, 0, 0, .45); strokeWidth: 1
                            fillColor: pane.colorFor ? pane.colorFor(c.tags[0] || "") : "gray"
                            startX: 16 * c.kk; startY: 94 * c.kk
                            PathLine { x: 68 * c.kk; y: 107 * c.kk }
                            PathLine { x: 108 * c.kk; y: 90 * c.kk }
                            PathLine { x: 56 * c.kk; y: 78 * c.kk }
                            PathLine { x: 16 * c.kk; y: 94 * c.kk }
                        }
                    }
                }
                Image {
                    anchors.fill: parent
                    visible: c.kind !== "app" && !thumb.visible
                    source: pane.assetFor ? pane.assetFor(c.kind === "app" ? "doc" : c.kind) : ""
                    sourceSize: Qt.size(width * 2, height * 2); smooth: true
                }
                Loader {
                    active: c.kind === "app"
                    x: 32 * c.kk; y: 22 * c.kk; width: 58 * c.kk; height: width
                    sourceComponent: K.Icon {
                        roundToIconSize: false
                        source: c.info ? c.info.icon : ""
                    }
                }
                Loader {   // previews: the real picture
                    id: thumb
                    active: pane.viewMode === 2 && c.kind === "image" && !pane.remote   // no thumbnails over KIO
                    visible: active && item.ready
                    x: 30 * c.kk; y: 18 * c.kk; width: 62 * c.kk; height: 62 * c.kk
                    sourceComponent: Rectangle {
                        readonly property bool ready: thumbImg.status === Image.Ready
                        color: "white"; border.color: "#1d1d1d"; rotation: 4
                        Image {
                            id: thumbImg
                            anchors.fill: parent; anchors.margins: 3 * c.kk
                            source: c.fileUrl
                            asynchronous: true; fillMode: Image.PreserveAspectCrop; sourceSize: Qt.size(256, 256)
                        }
                    }
                }
            }
            Rectangle {
                visible: c.sel
                width: Math.min(lbl.implicitWidth, lbl.width) + 14 * pane.tu; height: 21 * pane.tu; radius: 6 * pane.tu
                x: (c.width - width) / 2; y: lbl.y + lbl.baselineOffset - 15 * pane.tu
                color: pane.active ? "#0a84ff" : Qt.rgba(1, 1, 1, .2)
            }
            Text {
                id: lbl
                width: c.width - 16 * pane.u; x: 8 * pane.u
                anchors.baseline: parent.top; anchors.baselineOffset: 110 * c.kk + 20 * pane.tu
                horizontalAlignment: Text.AlignHCenter; elide: Text.ElideMiddle
                text: c.info && c.info.name ? c.info.name : c.fileName
                color: "white"; font.family: pane.fontFamily; font.pixelSize: 13 * pane.tu
            }
            Loader {
                active: c.tags.length > 0
                x: lbl.x + (lbl.width + Math.min(lbl.contentWidth, lbl.width)) / 2 + (c.sel ? 10 : 4) * pane.u
                y: lbl.y + (lbl.height - height) / 2
                sourceComponent: TagDots { tags: c.tags; colorFor: pane.colorFor; size: 9 * pane.tu }
            }
            Item {
                id: cDrag
                Drag.active: cMouse.drag.active
                Drag.dragType: Drag.Automatic
                Drag.supportedActions: Qt.CopyAction | Qt.MoveAction | Qt.LinkAction
                Drag.onActiveChanged: pane.dragStarted(Drag.active)
            }
            MouseArea {
                id: cMouse
                x: (c.width - 100 * c.kk) / 2; y: 10 * c.kk
                width: 100 * c.kk; height: 100 * c.kk + 26 * pane.tu
                acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton
                drag.target: cDrag
                onPressed: m => {
                    pane.activated()
                    if (m.button !== Qt.MiddleButton && (!c.sel || m.modifiers & (Qt.ControlModifier | Qt.ShiftModifier)))
                        pane.select(c.index, m.modifiers)
                    cDrag.Drag.mimeData = { "text/uri-list": Fm.uriList(pane.paths()) }   // as in the list
                    if (m.button === Qt.MiddleButton) { if (c.fileIsDir) pane.newTab(c.filePath, false); return }
                    if (m.button === Qt.RightButton) pane.menuRequested()
                }
                onReleased: { cDrag.x = 0; cDrag.y = 0 }
                onDoubleClicked: m => {
                    if (m.button !== Qt.LeftButton) return
                    if (c.fileIsDir && m.modifiers & Qt.ControlModifier) pane.newTab(c.filePath, true)
                    else pane.activateRow(c.index)
                }
            }
            DropArea {
                id: cDrop
                enabled: c.fileIsDir
                anchors.fill: cMouse
                onDropped: d => { pane.dropRequested(d.urls, c.filePath, d.proposedAction === Qt.MoveAction); d.accept() }
            }
        }
    }

    Text {   // a KIO folder still connecting, or one that couldn't be listed
        objectName: "splitRemoteMessage"
        visible: pane.remote && !pane.termActive && kioModel.count === 0
                 && (kioModel.status === KioModel.Loading || kioModel.status === KioModel.Error)
        x: 20 * pane.u; width: pane.width - 40 * pane.u
        y: pane.height / 2 - height / 2
        horizontalAlignment: Text.AlignHCenter; wrapMode: Text.Wrap
        text: kioModel.status === KioModel.Error ? "Can't open “" + Kio.title(pane.dir, Kio.rev) + "”\n\n" + kioModel.errorString
                                                 : "Connecting to “" + Kio.title(pane.dir, Kio.rev) + "”…"
        color: "white"; opacity: kioModel.status === KioModel.Error ? .75 : .4
        font.family: pane.fontFamily; font.pixelSize: 14 * pane.tu
    }
    Item {   // terminal tabs of this side are shown here
        id: termLayer
        objectName: "splitTermLayer"
        x: 0; y: bar.y + bar.height + 4 * pane.u; width: pane.width; height: pane.height - y
        visible: pane.termActive
    }
}
