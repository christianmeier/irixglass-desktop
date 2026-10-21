import QtQuick
import QtQuick.Controls
import Qt.labs.folderlistmodel

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
    property var colorFor                // function(tag) -> colour
    property bool showHidden: false
    signal activated()
    signal dropRequested(var urls, string target, bool proposedMove)
    signal menuRequested()
    signal openFile(string path)
    signal dragStarted(bool on)

    readonly property alias count: model.count
    function paths() { return Object.keys(selection) }
    function go(path) { if (path && path !== dir) { back = back.concat([dir]); dir = path; selection = ({}); anchorIndex = -1; rows.positionViewAtBeginning() } }
    function goBack() { if (back.length) { const p = back[back.length - 1]; back = back.slice(0, -1); dir = p; selection = ({}); anchorIndex = -1 } }
    function goUp() { if (dir !== "/") go(dir.substring(0, dir.lastIndexOf("/")) || "/") }
    function select(i, mods) {
        const path = model.get(i, "filePath")
        let s = Object.assign({}, selection)
        if (mods & Qt.ShiftModifier && anchorIndex >= 0) {
            s = ({})
            for (let k = Math.min(anchorIndex, i); k <= Math.max(anchorIndex, i); ++k) s[model.get(k, "filePath")] = true
        } else if (mods & Qt.ControlModifier) {
            if (s[path]) delete s[path]; else s[path] = true
            anchorIndex = i
        } else { s = ({}); s[path] = true; anchorIndex = i }
        selection = s
    }
    function activateRow(i) {
        const path = model.get(i, "filePath")
        if (model.get(i, "fileIsDir")) go(path); else pane.openFile(path)
    }

    FolderListModel {
        id: model
        folder: pane.dir ? "file://" + pane.dir : ""
        showDirsFirst: true
        showHidden: pane.showHidden
        showDotAndDotDot: false
        caseSensitive: false
        sortCaseSensitive: false
    }

    // a glass panel of its own, lit when active

    MouseArea {   // background: activate, clear the selection
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        onPressed: m => { pane.activated(); if (m.button === Qt.RightButton) pane.menuRequested(); else pane.selection = ({}) }
    }
    DropArea {   // dropping on the empty part: into this side's folder
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
        // the same height as the main side's list header, so rows line up across the sides
        x: 6 * pane.u; y: bar.y + bar.height + 2 * pane.u; width: pane.width - 12 * pane.u; height: 26 * pane.tu
        Text {
            x: pane.iconSize + 12 * pane.u; anchors.verticalCenter: parent.verticalCenter
            text: "Name"; color: "white"; opacity: .45
            font.family: pane.fontFamily; font.pixelSize: 11 * pane.tu; font.weight: Font.DemiBold
        }
        Text {
            anchors.right: parent.right; anchors.rightMargin: 8 * pane.u; anchors.verticalCenter: parent.verticalCenter
            text: "Size"; color: "white"; opacity: .45
            font.family: pane.fontFamily; font.pixelSize: 11 * pane.tu; font.weight: Font.DemiBold
        }
    }
    PaneFrame { anchors.fill: parent; u: pane.u; lineY: bar.y + bar.height; active: pane.active }
    Rectangle { x: 8 * pane.u; y: columns.y + columns.height; width: pane.width - 16 * pane.u; height: 1; color: Qt.rgba(1, 1, 1, .06) }

    ListView {
        id: rows
        objectName: "splitList"
        x: 6 * pane.u; y: columns.y + columns.height
        width: pane.width - 12 * pane.u; height: pane.height - y - 6 * pane.u
        clip: true
        model: model
        interactive: false
        ScrollBar.vertical: GlassScrollBar { u: pane.u }
        WheelHandler {
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
            onWheel: e => {
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
                width: parent.width - x - 90 * pane.u
                text: r.fileName; elide: Text.ElideMiddle; color: "white"
                font.family: pane.fontFamily; font.pixelSize: 13 * pane.tu
                TagDots {
                    tags: r.tags; colorFor: pane.colorFor; size: 9 * pane.tu
                    x: Math.min(nameText.contentWidth, nameText.width) + 6 * pane.u
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
            Text {
                anchors.right: parent.right; anchors.rightMargin: 8 * pane.u; anchors.verticalCenter: parent.verticalCenter
                text: r.fileIsDir ? "" : Fm.humanSize(r.fileSize); color: "white"; opacity: .5
                font.family: pane.fontFamily; font.pixelSize: 11.5 * pane.tu
            }
            Item {
                id: dragProxy
                Drag.active: rowMouse.drag.active
                Drag.dragType: Drag.Automatic
                Drag.supportedActions: Qt.CopyAction | Qt.MoveAction | Qt.LinkAction
                Drag.mimeData: ({ "text/uri-list": Fm.uriList(pane.paths()) })
                Drag.onActiveChanged: pane.dragStarted(Drag.active)
            }
            MouseArea {
                id: rowMouse
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                drag.target: dragProxy
                onPressed: m => {
                    pane.activated()
                    if (!r.sel || m.modifiers & (Qt.ControlModifier | Qt.ShiftModifier)) pane.select(r.index, m.modifiers)
                    if (m.button === Qt.RightButton) pane.menuRequested()
                }
                onReleased: { dragProxy.x = 0; dragProxy.y = 0 }
                onDoubleClicked: m => { if (m.button === Qt.LeftButton) pane.activateRow(r.index) }
            }
            DropArea {   // onto a folder: into it
                id: rowDrop
                enabled: r.fileIsDir
                anchors.fill: parent
                onDropped: d => { pane.dropRequested(d.urls, r.filePath, d.proposedAction === Qt.MoveAction); d.accept() }
            }
        }
    }
}
