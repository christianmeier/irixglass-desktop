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
    property var recent: []
    property var selection: ({})                // path -> true
    property int anchorIndex: -1
    property string renaming: ""
    property bool editingPath: false
    property string selectAfterLoad: ""         // file to select once the folder has loaded (typed file paths)
    // Sidebar: sections of places, saved as JSON in the settings. Paths may start with "~".
    // Items have a path (a folder) or an action (overview, find, network).
    property string placesJson: ""
    readonly property var places: placesJson ? JSON.parse(placesJson) : defaultPlaces()
    property string renamingPlace: ""           // "si/ii" of the sidebar entry being renamed
    property real wheelValue: .352              // thumbwheel 0..1
    readonly property real iconScale: .55 + wheelValue * 1.05
    readonly property real k: u * iconScale     // px per icon unit (icons are drawn in a 100-unit box)
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
    title: (dir === "/" ? "/" : dir.split("/").pop()) + " — fm"

    Settings {
        category: "fm"
        property alias wheelValue: win.wheelValue
        property alias viewMode: win.viewMode
        property alias showHidden: win.showHidden
        property alias places: win.placesJson
        property alias width: win.width
        property alias height: win.height
    }

    function asset(name) { return "file://" + Fm.assetDir + "/icons/" + name + ".svg" }
    function shape() { Fm.updateWindow(win, Math.round(r), !maximized) }
    onWidthChanged: shape()
    onHeightChanged: shape()
    onVisibilityChanged: shape()
    Component.onCompleted: Qt.callLater(shape)

    // ---- navigation ----------------------------------------------------------
    function go(path, fromHistory) {
        if (!path || path === dir) return
        if (!fromHistory) { backStack = backStack.concat([dir]); fwdStack = [] }
        dir = path
        selection = ({}); anchorIndex = -1; renaming = ""
        search.text = ""
        recent = [path].concat(recent.filter(p => p !== path)).slice(0, 12)
        view().positionViewAtBeginning()
    }
    function goBack() { if (backStack.length) { fwdStack = [dir].concat(fwdStack); const p = backStack[backStack.length - 1]; backStack = backStack.slice(0, -1); go(p, true) } }
    function goForward() { if (fwdStack.length) { backStack = backStack.concat([dir]); const p = fwdStack[0]; fwdStack = fwdStack.slice(1); go(p, true) } }
    function goUp() { if (dir !== "/") go(dir.substring(0, dir.lastIndexOf("/")) || "/") }
    function activate(path, isDir) { isDir ? go(path) : Fm.open(path) }

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
        if (r.file) { selectAfterLoad = r.file; selectPending() }
        return true
    }
    function selectPending() {
        if (!selectAfterLoad) return
        const i = folder.indexOf("file://" + selectAfterLoad)
        if (i < 0) return
        selectAfterLoad = ""
        select(i, folder.get(i, "filePath"), 0)
        view().positionViewAtIndex(i, ListView.Contain)
    }

    // ---- selection -----------------------------------------------------------
    function view() { return viewMode === 1 ? list : grid }
    function paths() { return Object.keys(selection) }
    function select(index, path, mods) {
        let s = Object.assign({}, selection)
        if (mods & Qt.ShiftModifier && anchorIndex >= 0) {
            s = ({})
            for (let i = Math.min(anchorIndex, index); i <= Math.max(anchorIndex, index); ++i) s[folder.get(i, "filePath")] = true
        } else if (mods & Qt.ControlModifier) {
            if (s[path]) delete s[path]; else s[path] = true
            anchorIndex = index
        } else {
            s = ({}); s[path] = true; anchorIndex = index
        }
        selection = s
        view().currentIndex = index
    }
    function selectAll() { const s = ({}); for (let i = 0; i < folder.count; ++i) s[folder.get(i, "filePath")] = true; selection = s }
    function moveCursor(delta) {
        if (!folder.count) return
        const i = Math.max(0, Math.min(folder.count - 1, (anchorIndex < 0 ? -delta : anchorIndex) + delta))
        select(i, folder.get(i, "filePath"), 0)
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
                { label: "cachyOS", path: "/" }, { label: "Network", action: "network" }] }
        ]
    }
    function expand(p) { return p === "~" ? Fm.home : p.startsWith("~/") ? Fm.home + p.substring(1) : p }
    function shorten(p) { return p === Fm.home ? "~" : p.startsWith(Fm.home + "/") ? "~" + p.substring(Fm.home.length) : p }
    function placeLabel(item) { return item.label || (item.path === "~" ? Fm.userName : item.path.split("/").pop() || "/") }
    function setPlaces(p) { placesJson = JSON.stringify(p) }
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
    function resetPlaces() { placesJson = "" }
    function activatePlace(item) {
        if (item.path) go(expand(item.path))
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
            rows.push({ kind: "head", si: si, title: sec.title, first: si === 0 })
            sec.items.forEach((it, ii) => rows.push({ kind: "item", si: si, ii: ii, item: it, color: sec.color }))
            if (!sec.items.length) rows.push({ kind: "empty", si: si })
        })
        return rows
    }
    function trashSelected() { Fm.trash(paths()); selection = ({}) }
    function newFolder() { const p = Fm.makeFolder(dir); const s = ({}); s[p] = true; selection = s; renaming = p }

    FolderListModel {
        id: folder
        folder: "file://" + win.dir
        showDirsFirst: true
        showHidden: win.showHidden
        showDotAndDotDot: false
        caseSensitive: false
        sortCaseSensitive: false
        nameFilters: search.text ? ["*" + search.text + "*"] : []
        onCountChanged: win.selectPending()
        onStatusChanged: win.selectPending()
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
            PathRectangle { x: sidebar.x; y: sidebar.y; width: sidebar.width; height: sidebar.height; radius: 18 * win.u }
            PathRectangle { x: navPill.x; y: navPill.y; width: navPill.width; height: navPill.height; radius: navPill.height / 2 }
            PathRectangle { x: pathPill.x; y: pathPill.y; width: pathPill.width; height: pathPill.height; radius: pathPill.height / 2 }
            PathRectangle { x: viewPill.x; y: viewPill.y; width: viewPill.width; height: viewPill.height; radius: viewPill.height / 2 }
            PathRectangle { x: searchPill.x; y: searchPill.y; width: searchPill.width; height: searchPill.height; radius: searchPill.height / 2 }
        }
    }
    EdgeRing { anchors.fill: parent; radius: win.r; visible: !win.maximized }

    // Window dragging: empty toolbar strip and sidebar background.
    Item {
        x: 240 * u; y: 0; width: parent.width - x; height: 72 * u
        DragHandler { target: null; onActiveChanged: if (active) win.startSystemMove() }
        TapHandler { onDoubleTapped: win.maximized ? win.showNormal() : win.showMaximized() }
    }

    // ---- sidebar -------------------------------------------------------------
    Item {
        id: sidebar
        x: 10 * u; y: 10 * u; width: 220 * u; height: parent.height - 20 * u
        Rectangle { anchors.fill: parent; radius: 18 * win.u; color: win.sidebarTint }
        EdgeRing { anchors.fill: parent; radius: 18 * win.u }
        DragHandler { target: null; onActiveChanged: if (active) win.startSystemMove() }

        TrafficLights { window: win; x: 17.5 * win.u; y: 26 * win.u - height / 2 }

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
                readonly property bool item: modelData.kind === "item"
                readonly property bool current: item && !!modelData.item.path && win.expand(modelData.item.path) === win.dir
                readonly property string key: modelData.si + "/" + (modelData.ii === undefined ? "" : modelData.ii)
                // Header rows carry the gap above them, so baselines match the mockup.
                readonly property real gap: head && !modelData.first ? 30 * win.tu : 0
                width: placesView.width
                height: gap + (head ? 28 : 30) * win.tu

                Rectangle {
                    visible: row.item && (row.current || hov.hovered || drop.containsDrag)
                    x: 14 * win.u; y: 4 * win.tu; width: 196 * win.u; height: 26 * win.tu; radius: 8 * win.tu
                    color: Qt.rgba(1, 1, 1, row.current || drop.containsDrag ? .16 : .07)
                }
                Rectangle {   // drop target line for headers / empty sections
                    visible: !row.item && drop.containsDrag
                    x: 14 * win.u; y: row.height - 3 * win.tu; width: 196 * win.u; height: 2 * win.tu; radius: 1
                    color: "#0a84ff"
                }
                Rectangle {
                    visible: row.item
                    x: 22 * win.u; y: 9 * win.tu; width: 15 * win.tu; height: 13 * win.tu; radius: 3 * win.tu
                    color: "transparent"; border.color: row.modelData.color || "transparent"; border.width: 1.6 * win.u
                }
                Text {
                    visible: !(row.item && win.renamingPlace === row.key)
                    x: (row.head ? 16 : row.item ? 48 : 22) * win.u
                    width: (row.head ? 170 : 160) * win.u
                    elide: Text.ElideRight
                    anchors.baseline: parent.top; anchors.baselineOffset: row.gap + 21 * win.tu
                    text: row.head ? row.modelData.title : row.item ? win.placeLabel(row.modelData.item) : "Drop folders here"
                    color: "white"; opacity: row.head ? .45 : row.item ? 1 : .3
                    font.family: win.fontFamily
                    font.pixelSize: (row.head ? 11 : row.item ? 13.5 : 12) * win.tu
                    font.weight: row.head ? Font.DemiBold : Font.Normal
                }
                Rename {
                    active: row.item && win.renamingPlace === row.key
                    x: 44 * win.u; width: 160 * win.u; y: 4 * win.tu
                    name: row.item ? win.placeLabel(row.modelData.item) : ""
                    u: win.tu; fontFamily: win.fontFamily; align: TextInput.AlignLeft
                    onCommitted: text => win.placeRename(row.modelData.si, row.modelData.ii, text)
                    onDone: { win.renamingPlace = ""; keys.forceActiveFocus() }
                }
                Text {   // "+" on a hovered header: add the current folder to this section
                    visible: row.head && hov.hovered
                    anchors.right: parent.right; anchors.rightMargin: 18 * win.u
                    anchors.baseline: parent.top; anchors.baselineOffset: row.gap + 21 * win.tu
                    text: "+"; color: "white"; opacity: plusHover.hovered ? .9 : .5
                    font.family: win.fontFamily; font.pixelSize: 15 * win.tu; font.weight: Font.DemiBold
                    HoverHandler { id: plusHover; cursorShape: Qt.PointingHandCursor }
                    TapHandler { onTapped: win.placeAdd(row.modelData.si, win.dir) }
                }
                HoverHandler { id: hov }
                MouseArea {
                    anchors.fill: parent; anchors.rightMargin: row.head ? 36 * win.u : 0
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    onClicked: m => {
                        keys.forceActiveFocus()
                        placeMenu.si = row.modelData.si
                        placeMenu.ii = row.item ? row.modelData.ii : -1
                        if (m.button === Qt.RightButton) (row.item ? placeMenu : sectionMenu).open()
                        else if (row.item) win.activatePlace(row.modelData.item)
                    }
                }
                DropArea {
                    id: drop
                    anchors.fill: parent
                    keys: ["text/uri-list"]
                    // Dropping on an entry inserts after it; on a header, at the top of the section.
                    onDropped: d => { win.dropOnPlaces(d.urls, row.modelData.si, row.item ? row.modelData.ii + 1 : 0); d.accept(Qt.LinkAction) }
                }
            }
        }
    }

    // ---- toolbar -------------------------------------------------------------
    Pill {
        id: navPill
        x: 250 * u; y: 18 * u; width: 86 * u; height: 36 * u
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
        x: 350 * u; y: 18 * u; height: 36 * u
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
                                onClicked: crumb.last ? win.editPath() : win.go(crumb.modelData.path)
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
            x: 36 * win.u; width: parent.width - x - 14 * win.u
            anchors.baseline: parent.top; anchors.baselineOffset: 23 * win.u
            color: "white"; selectionColor: "#0a84ff"
            font.family: win.fontFamily; font.pixelSize: 13.5 * win.tu
            clip: true
            Keys.onEscapePressed: { text = ""; keys.forceActiveFocus() }
            Keys.onDownPressed: keys.forceActiveFocus()
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
            if ((event.text === "/" || event.text === "~") && !(event.modifiers & (Qt.ControlModifier | Qt.AltModifier))) {
                win.editPath(event.text); event.accepted = true; return
            }
            if (event.key === Qt.Key_Left && event.modifiers & Qt.AltModifier) win.goBack()
            else if (event.key === Qt.Key_Right && event.modifiers & Qt.AltModifier) win.goForward()
            else if (event.key === Qt.Key_Up && event.modifiers & Qt.AltModifier) win.goUp()
            else if (event.key === Qt.Key_Left && win.viewMode !== 1) win.moveCursor(-1)
            else if (event.key === Qt.Key_Right && win.viewMode !== 1) win.moveCursor(1)
            else if (event.key === Qt.Key_Up) win.moveCursor(-vertical)
            else if (event.key === Qt.Key_Down) win.moveCursor(vertical)
            else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                if (win.anchorIndex >= 0) win.activate(folder.get(win.anchorIndex, "filePath"), folder.get(win.anchorIndex, "fileIsDir"))
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
        x: 240 * u; y: 72 * u; width: parent.width - x; height: parent.height - y - 52 * u
        onDropped: drop => { Fm.drop(drop.urls, win.dir, drop.proposedAction === Qt.MoveAction); drop.accept() }
    }

    GridView {
        id: grid
        objectName: "grid"
        visible: win.viewMode !== 1
        x: 262 * u; y: 92 * u - 10 * win.k
        width: parent.width - x - 20 * u
        height: parent.height - 52 * u - y
        clip: true
        cellWidth: 168 * u * win.iconScale / .92
        cellHeight: 168 * u * win.iconScale / .92
        model: folder
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
                for (let i = 0; i < folder.count; ++i) {
                    const cx = (i % cols) * grid.cellWidth, cy = Math.floor(i / cols) * grid.cellHeight
                    const ix = cx + grid.cellWidth * .15, iw = grid.cellWidth * .7
                    const iy = cy + 10 * win.k, ih = 100 * win.k + 26 * win.tu
                    if (ix < area.x + area.width && ix + iw > area.x && iy < area.y + area.height && iy + ih > area.y)
                        s[folder.get(i, "filePath")] = true
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
            readonly property bool sel: win.selection[filePath] === true
            readonly property real k: win.k
            width: grid.cellWidth; height: grid.cellHeight

            Item {
                id: icon
                x: (cell.width - 120 * cell.k) / 2; y: 0
                width: 120 * cell.k; height: width
                readonly property size src: Qt.size(width * Screen.devicePixelRatio, height * Screen.devicePixelRatio)
                Image {
                    anchors.fill: parent
                    source: win.asset(cell.sel || drop.containsDrag ? "carpet-sel" : cell.kind === "exe" ? "carpet-strong" : "carpet")
                    sourceSize: icon.src; smooth: true
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
            }
            MouseArea {
                id: mouse
                x: (cell.width - 100 * cell.k) / 2; y: 10 * cell.k
                width: 100 * cell.k; height: 100 * cell.k + 26 * win.tu
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                drag.target: dragProxy
                onPressed: m => {
                    keys.forceActiveFocus()
                    if (!cell.sel || m.modifiers & (Qt.ControlModifier | Qt.ShiftModifier)) win.select(cell.index, cell.filePath, m.modifiers)
                    if (m.button === Qt.RightButton) itemMenu.open()
                }
                onReleased: { dragProxy.x = 0; dragProxy.y = 0 }
                onDoubleClicked: m => { if (m.button === Qt.LeftButton) win.activate(cell.filePath, cell.fileIsDir) }
            }
            DropArea {
                id: drop
                enabled: cell.fileIsDir
                anchors.fill: mouse
                onDropped: d => { Fm.drop(d.urls, cell.filePath, d.proposedAction === Qt.MoveAction); d.accept() }
            }
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
        x: 252 * u; y: 80 * u
        width: parent.width - x - 14 * u
        height: parent.height - 52 * u - y
        clip: true
        model: folder
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: GlassScrollBar { u: win.u }
        headerPositioning: ListView.OverlayHeader
        header: Item {
            z: 2; width: list.width; height: 26 * win.tu
            Row {
                x: 40 * win.u; height: parent.height
                Repeater {
                    model: [["Name", list.width - 430 * win.u], ["Date Modified", 180 * win.u], ["Size", 90 * win.u], ["Kind", 110 * win.u]]
                    Text {
                        required property var modelData
                        width: modelData[1]
                        anchors.verticalCenter: parent.verticalCenter
                        text: modelData[0]; color: "white"; opacity: .45
                        font.family: win.fontFamily; font.pixelSize: 11 * win.tu; font.weight: Font.DemiBold
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
            readonly property bool sel: win.selection[filePath] === true
            width: list.width; height: 28 * win.tu
            Rectangle {
                anchors.fill: parent; radius: 6 * win.u
                color: row.sel ? "#0a84ff" : rowDrop.containsDrag ? Qt.rgba(1, 1, 1, .14) : row.index % 2 ? Qt.rgba(1, 1, 1, .035) : "transparent"
            }
            Image {
                x: 8 * win.u; y: (row.height - height) / 2 - 1 * win.u
                width: 26 * win.u; height: width
                visible: row.kind !== "app"
                source: win.asset(row.kind === "app" ? "doc" : row.kind)
                sourceSize: Qt.size(64, 64); smooth: true
            }
            K.Icon {
                x: 11 * win.u; y: (row.height - height) / 2
                width: 18 * win.u; height: width
                visible: row.kind === "app"
                source: row.info ? row.info.icon : ""
            }
            Row {
                x: 40 * win.u; height: parent.height
                Text {
                    width: list.width - 430 * win.u; anchors.verticalCenter: parent.verticalCenter
                    visible: win.renaming !== row.filePath
                    text: row.info && row.info.name ? row.info.name : row.fileName
                    elide: Text.ElideRight; color: "white"
                    font.family: win.fontFamily; font.pixelSize: 13 * win.tu
                }
                Rename {
                    active: win.renaming === row.filePath
                    width: list.width - 440 * win.u; anchors.verticalCenter: parent.verticalCenter
                    path: row.filePath; name: row.fileName; u: win.tu; fontFamily: win.fontFamily
                    onDone: { win.renaming = ""; keys.forceActiveFocus() }
                }
                Repeater {
                    model: [
                        [Qt.formatDateTime(row.fileModified, "d MMM yyyy 'at' HH:mm"), 180],
                        [row.fileIsDir ? "--" : Fm.humanSize(row.fileSize), 90],
                        [row.fileIsDir ? "Folder" : row.kind === "app" ? "Application" : row.kind === "image" ? "Image" : row.kind === "exe" ? "Executable" : "Document", 110]
                    ]
                    Text {
                        required property var modelData
                        width: modelData[1] * win.u; anchors.verticalCenter: parent.verticalCenter
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
            }
            MouseArea {
                id: rowMouse
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                drag.target: rowDrag
                onPressed: m => {
                    keys.forceActiveFocus()
                    if (!row.sel || m.modifiers & (Qt.ControlModifier | Qt.ShiftModifier)) win.select(row.index, row.filePath, m.modifiers)
                    if (m.button === Qt.RightButton) itemMenu.open()
                }
                onReleased: { rowDrag.x = 0; rowDrag.y = 0 }
                onDoubleClicked: m => { if (m.button === Qt.LeftButton) win.activate(row.filePath, row.fileIsDir) }
            }
            DropArea {
                id: rowDrop
                enabled: row.fileIsDir
                anchors.fill: parent
                onDropped: d => { Fm.drop(d.urls, row.filePath, d.proposedAction === Qt.MoveAction); d.accept() }
            }
        }
    }

    // ---- status bar ------------------------------------------------------------
    Rectangle {
        x: 240 * u; y: parent.height - 52 * u; width: parent.width - x - 10 * u; height: 1
        color: Qt.rgba(1, 1, 1, .08)
    }
    Text {
        x: 262 * u; anchors.baseline: parent.top; anchors.baselineOffset: parent.height - 28 * u
        readonly property int n: win.paths().length
        text: (n ? n + " of " + folder.count + " selected" : folder.count + (folder.count === 1 ? " item" : " items"))
              + " · " + (folder.count >= 0 ? Fm.freeSpace(win.dir) : "")
        color: "white"; opacity: .55
        font.family: win.fontFamily; font.pixelSize: 12 * tu
    }
    Text {
        anchors.right: parent.right; anchors.rightMargin: 230 * u
        anchors.baseline: parent.top; anchors.baselineOffset: parent.height - 28 * u
        text: "Icon size"; color: "white"; opacity: .55
        font.family: win.fontFamily; font.pixelSize: 12 * tu
    }
    Thumbwheel {
        id: wheelCtl
        u: win.u
        x: parent.width - 216 * u; y: parent.height - 42 * u; width: 190 * u; height: 22 * u
        value: win.wheelValue
        onValueChanged: win.wheelValue = value
    }

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
        P.MenuItem { text: "Add Current Folder"; onTriggered: win.placeAdd(placeMenu.si, win.dir) }
        P.MenuItem { text: "Add Folder…"; onTriggered: { folderPicker.si = placeMenu.si; folderPicker.folder = "file://" + win.dir; folderPicker.open() } }
        P.MenuSeparator {}
        P.MenuItem { text: "Reset Sidebar"; onTriggered: win.resetPlaces() }
    }
    P.FolderDialog {
        id: folderPicker
        property int si: 0
        title: "Add Folder to " + (win.places[si] ? win.places[si].title : "Sidebar")
        onAccepted: win.placeAdd(si, decodeURIComponent(folder.toString().replace(/^file:\/\//, "")))
    }

    P.Menu {
        id: itemMenu
        P.MenuItem { text: "Open"; onTriggered: { for (const p of win.paths()) win.activate(p, folder.get(folder.indexOf("file://" + p), "fileIsDir")) } }
        P.MenuItem { text: "Open in Dolphin"; onTriggered: Fm.openInDolphin(win.paths()[0]) }
        P.MenuSeparator {}
        P.MenuItem { text: "Rename…"; enabled: win.paths().length === 1; onTriggered: win.renaming = win.paths()[0] }
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
        P.MenuItem { text: "Move to Trash"; onTriggered: win.trashSelected() }
    }
    P.Menu {
        id: bgMenu
        P.MenuItem { text: "New Folder"; onTriggered: win.newFolder() }
        P.MenuItem { text: "Paste"; onTriggered: Fm.paste(win.dir) }
        P.MenuItem { text: "Add Current Folder to Favorites"; onTriggered: win.placeAdd(0, win.dir) }
        P.MenuSeparator {}
        P.MenuItem { text: "Open Terminal Here"; onTriggered: Fm.openTerminal(win.dir) }
        P.MenuItem { text: "Open in Dolphin"; onTriggered: Fm.openInDolphin(win.dir) }
        P.MenuSeparator {}
        P.MenuItem { text: "Show Hidden Files"; checkable: true; checked: win.showHidden; onTriggered: win.showHidden = !win.showHidden }
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
            P.MenuItem { text: "New Folder\tCtrl+Shift+N"; onTriggered: win.newFolder() }
            P.MenuItem { text: "Open Terminal Here"; onTriggered: Fm.openTerminal(win.dir) }
            P.MenuItem { text: "Open in Dolphin"; onTriggered: Fm.openInDolphin(win.dir) }
            P.MenuSeparator {}
            P.MenuItem { text: "Move to Trash\tDel"; onTriggered: win.trashSelected() }
            P.MenuSeparator {}
            P.MenuItem { text: "Close Window\tCtrl+W"; onTriggered: win.close() }
        }
        P.Menu {
            title: "Edit"
            P.MenuItem { text: "Copy\tCtrl+C"; onTriggered: Fm.copyToClipboard(win.paths()) }
            P.MenuItem { text: "Paste\tCtrl+V"; onTriggered: Fm.paste(win.dir) }
            P.MenuItem { text: "Select All\tCtrl+A"; onTriggered: win.selectAll() }
            P.MenuItem { text: "Rename…\tF2"; onTriggered: if (win.paths().length === 1) win.renaming = win.paths()[0] }
            P.MenuItem { text: "Copy Path"; onTriggered: Fm.copyPathText(win.paths().length ? win.paths().join("\n") : win.dir) }
        }
        P.Menu {
            title: "View"
            P.MenuItem { text: "as Icons\tCtrl+1"; checkable: true; checked: win.viewMode === 0; onTriggered: win.viewMode = 0 }
            P.MenuItem { text: "as List\tCtrl+2"; checkable: true; checked: win.viewMode === 1; onTriggered: win.viewMode = 1 }
            P.MenuItem { text: "as Previews\tCtrl+3"; checkable: true; checked: win.viewMode === 2; onTriggered: win.viewMode = 2 }
            P.MenuSeparator {}
            P.MenuItem { text: "Bigger Icons\tCtrl++"; onTriggered: wheelCtl.nudge(.08) }
            P.MenuItem { text: "Smaller Icons\tCtrl+-"; onTriggered: wheelCtl.nudge(-.08) }
            P.MenuSeparator {}
            P.MenuItem { text: "Show Hidden Files\tCtrl+H"; checkable: true; checked: win.showHidden; onTriggered: win.showHidden = !win.showHidden }
        }
        P.Menu {
            title: "Go"
            P.MenuItem { text: "Back\tAlt+Left"; onTriggered: win.goBack() }
            P.MenuItem { text: "Forward\tAlt+Right"; onTriggered: win.goForward() }
            P.MenuItem { text: "Enclosing Folder\tAlt+Up"; onTriggered: win.goUp() }
            P.MenuItem { text: "Go to Folder…\tCtrl+Shift+G"; onTriggered: win.editPath() }
            P.MenuSeparator {}
            P.MenuItem { text: "Home\tCtrl+Shift+H"; onTriggered: win.go(Fm.home) }
            P.MenuItem { text: "Desktop"; onTriggered: win.go(Fm.home + "/Desktop") }
            P.MenuItem { text: "Documents"; onTriggered: win.go(Fm.home + "/Documents") }
            P.MenuItem { text: "Downloads"; onTriggered: win.go(Fm.home + "/Downloads") }
            P.MenuItem { text: "Applications"; onTriggered: win.go(Fm.home + "/Applications") }
            P.MenuItem { text: "Icon Catalog"; onTriggered: win.go("/usr/share/applications") }
            P.MenuItem { text: "Computer"; onTriggered: win.go("/") }
        }
        P.Menu {
            title: "Window"
            P.MenuItem { text: "Minimize\tCtrl+M"; onTriggered: win.showMinimized() }
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
    Shortcut { sequence: "Ctrl+W"; onActivated: win.close() }
    Shortcut { sequence: "Ctrl+M"; onActivated: win.showMinimized() }
    Shortcut { sequences: [StandardKey.Copy]; enabled: !search.activeFocus && !win.renaming; onActivated: Fm.copyToClipboard(win.paths()) }
    Shortcut { sequences: [StandardKey.Paste]; enabled: !search.activeFocus && !win.renaming; onActivated: Fm.paste(win.dir) }
    Shortcut { sequences: [StandardKey.SelectAll]; enabled: !search.activeFocus && !win.renaming; onActivated: win.selectAll() }
    Shortcut { sequence: "Ctrl+F"; onActivated: search.forceActiveFocus() }
    Shortcut { sequences: ["Ctrl+L", "Ctrl+Shift+G"]; onActivated: win.editPath() }
    Shortcut { sequence: "Ctrl+H"; onActivated: win.showHidden = !win.showHidden }
    Shortcut { sequence: "Ctrl+Shift+H"; onActivated: win.go(Fm.home) }
    Shortcut { sequence: "Ctrl+1"; onActivated: win.viewMode = 0 }
    Shortcut { sequence: "Ctrl+2"; onActivated: win.viewMode = 1 }
    Shortcut { sequence: "Ctrl+3"; onActivated: win.viewMode = 2 }
    Shortcut { sequences: ["Ctrl++", "Ctrl+="]; onActivated: wheelCtl.nudge(.08) }
    Shortcut { sequence: "Ctrl+-"; onActivated: wheelCtl.nudge(-.08) }
}
