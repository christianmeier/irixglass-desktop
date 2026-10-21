import QtQuick
import Qt.labs.folderlistmodel

// The list view's columns after the name, as in Finder's View Options: Date Modified, Date Created,
// Date Last Opened, Date Changed, Size, Kind, Tags, Permissions, Owner. Which ones show is the
// window's `listColumns` (right-click a heading, or View ▸ Show Columns); both sides use them.
// `heading: true` draws the titles (click to sort where the folder model can), otherwise one row.
Row {
    id: lc
    property var cols: []                // keys shown, in the fixed order below
    property real u: 1
    property real tu: 1
    property string fontFamily
    property bool heading: false
    // heading
    property int sortField: -1
    property bool sortReversed: false
    property bool searching: false
    signal sortRequested(int field)
    signal menuRequested()
    // a row
    property string path
    property bool isDir
    property date modified
    property var size
    property string kindText
    property string sizeText             // folder sizes come from the window
    property var tags: []
    property bool sel: false

    // key: [title, width (units), FolderListModel sort field or -1]
    readonly property var defs: ({
        modified: ["Date Modified", 160, FolderListModel.Time], created: ["Date Created", 160, -1],
        opened: ["Date Last Opened", 160, -1], changed: ["Date Changed", 160, -1],
        size: ["Size", 80, FolderListModel.Size], kind: ["Kind", 100, FolderListModel.Type],
        tags: ["Tags", 90, -1], permissions: ["Permissions", 100, -1], owner: ["Owner", 90, -1] })
    function shown(key) { return cols.indexOf(key) >= 0 }
    // The extra details only for columns that need them (one stat per row).
    readonly property var meta: !heading && (shown("created") || shown("opened") || shown("changed") || shown("permissions") || shown("owner"))
                                ? Fm.fileMeta(path) : ({})
    function date(d) { return d && !isNaN(d) ? Qt.formatDateTime(d, "d MMM yyyy 'at' HH:mm") : "--" }

    component Col: Text {
        id: col
        property string key
        property string value
        width: lc.shown(key) ? lc.defs[key][1] * lc.u : 0
        visible: width > 0
        anchors.verticalCenter: parent ? parent.verticalCenter : undefined
        readonly property bool sorted: lc.heading && lc.sortField === lc.defs[key][2] && lc.defs[key][2] >= 0
        text: !visible ? "" : lc.heading ? (key === "kind" && lc.searching ? "Where" : lc.defs[key][0]) + (sorted ? (lc.sortReversed ? "  ▾" : "  ▴") : "")
                                         : value
        color: "white"
        opacity: lc.heading ? (sorted ? .85 : .45) : lc.sel ? .9 : .55
        elide: Text.ElideRight
        font.family: lc.fontFamily
        font.pixelSize: (lc.heading ? 11 : 12) * lc.tu
        font.weight: lc.heading ? Font.DemiBold : Font.Normal
        MouseArea {
            enabled: lc.heading
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            cursorShape: lc.heading && lc.defs[col.key][2] >= 0 ? Qt.PointingHandCursor : Qt.ArrowCursor
            onClicked: m => {
                if (m.button === Qt.RightButton) lc.menuRequested()
                else if (lc.defs[col.key][2] >= 0) lc.sortRequested(lc.defs[col.key][2])
            }
        }
    }
    Col { key: "modified"; value: visible ? lc.date(lc.modified) : "" }
    Col { key: "created"; value: visible ? lc.date(lc.meta.created) : "" }
    Col { key: "opened"; value: visible ? lc.date(lc.meta.opened) : "" }
    Col { key: "changed"; value: visible ? lc.date(lc.meta.changed) : "" }
    Col { key: "size"; value: visible ? (lc.sizeText || (lc.isDir ? "--" : Fm.humanSize(lc.size))) : "" }
    Col { key: "kind"; value: lc.kindText }
    Col { key: "tags"; value: visible ? lc.tags.join(", ") : "" }
    Col { key: "permissions"; value: visible ? lc.meta.permissions || "" : "" }
    Col { key: "owner"; value: visible ? lc.meta.owner || "" : "" }
}
