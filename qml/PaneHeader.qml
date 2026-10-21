import QtQuick

// The header of a split-view side: ‹ back, ↑ up and the clickable path. Both sides use it, so
// they look the same.
Item {
    id: header
    property string dir
    property bool canGoBack: false
    property real u: 1
    property real tu: 1
    property string fontFamily
    signal backRequested()
    signal upRequested()
    signal pathClicked(string path)
    signal activated()
    height: 26 * tu

    Row {
        anchors.fill: parent
        spacing: 8 * header.u
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: "‹"; color: "white"; opacity: header.canGoBack ? .9 : .3
            font.family: header.fontFamily; font.pixelSize: 19 * header.tu
            TapHandler { onTapped: { header.activated(); header.backRequested() } }
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: "↑"; color: "white"; opacity: header.dir !== "/" ? .9 : .3
            font.family: header.fontFamily; font.pixelSize: 14 * header.tu
            TapHandler { onTapped: { header.activated(); header.upRequested() } }
        }
        Item {
            id: clip
            width: header.width - 50 * header.u; height: parent.height; clip: true
            Row {
                x: Math.min(0, clip.width - width); height: parent.height
                Repeater {
                    model: {
                        const out = [{ label: "/", path: "/" }]
                        let acc = ""
                        for (const p of header.dir.split("/").filter(p => p.length)) { acc += "/" + p; out.push({ label: p, path: acc }) }
                        return out
                    }
                    Row {
                        id: crumb
                        required property var modelData
                        readonly property bool last: modelData.path === header.dir
                        height: clip.height
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: crumb.modelData.label; color: "white"; opacity: crumb.last ? 1 : .65
                            font.family: header.fontFamily; font.pixelSize: 12.5 * header.tu
                            font.weight: crumb.last ? Font.DemiBold : Font.Normal
                            TapHandler { onTapped: { header.activated(); header.pathClicked(crumb.modelData.path) } }
                        }
                        Text {
                            visible: !crumb.last; anchors.verticalCenter: parent.verticalCenter
                            text: "  ›  "; color: "white"; opacity: .35
                            font.family: header.fontFamily; font.pixelSize: 12.5 * header.tu
                        }
                    }
                }
            }
        }
    }
}
