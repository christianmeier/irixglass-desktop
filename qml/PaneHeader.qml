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

    // ‹, ↑ and the path sit on one line: the two buttons are drawn, centred on the middle of
    // the path's capital letters (glyphs of different sizes don't line up when centred as boxes).
    FontMetrics { id: fm; font.family: header.fontFamily; font.pixelSize: 12.5 * header.tu }
    readonly property real capHeight: fm.tightBoundingRect("H").height
    readonly property real textBaseline: Math.round((height + capHeight) / 2)
    readonly property real midY: textBaseline - capHeight / 2

    Row {
        anchors.fill: parent
        spacing: 10 * header.u
        // Strokes are thin rounded bars (render the same everywhere, also off-screen).
        component Bar: Rectangle {
            property real cx; property real cy; property real len; property real angle; property real thick: 1.5
            x: cx - width / 2; y: cy - height / 2; width: len; height: thick; radius: height / 2
            rotation: angle; color: "white"; antialiasing: true
        }
        Item {   // ‹ back
            width: 12 * header.u; height: parent.height
            opacity: header.canGoBack ? .9 : .3
            Bar { cx: 5.6 * header.u; cy: header.midY - 2.4 * header.u; len: 7 * header.u; angle: -50; thick: 1.7 * header.u }
            Bar { cx: 5.6 * header.u; cy: header.midY + 2.4 * header.u; len: 7 * header.u; angle: 50; thick: 1.7 * header.u }
            TapHandler { onTapped: { header.activated(); header.backRequested() } }
        }
        Item {   // ↑ up
            width: 12 * header.u; height: parent.height
            opacity: header.dir !== "/" ? .9 : .3
            Bar { cx: 6 * header.u; cy: header.midY; len: 11.5 * header.u; angle: 90; thick: 1.7 * header.u }
            Bar { cx: 3.9 * header.u; cy: header.midY - 3.6 * header.u; len: 6.5 * header.u; angle: -50; thick: 1.7 * header.u }
            Bar { cx: 8.1 * header.u; cy: header.midY - 3.6 * header.u; len: 6.5 * header.u; angle: 50; thick: 1.7 * header.u }
            TapHandler { onTapped: { header.activated(); header.upRequested() } }
        }
        Item {
            id: clip
            width: header.width - 54 * header.u; height: parent.height; clip: true
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
                            anchors.baseline: parent.top; anchors.baselineOffset: header.textBaseline
                            text: crumb.modelData.label; color: "white"; opacity: crumb.last ? 1 : .65
                            font.family: header.fontFamily; font.pixelSize: 12.5 * header.tu
                            font.weight: crumb.last ? Font.DemiBold : Font.Normal
                            TapHandler { onTapped: { header.activated(); header.pathClicked(crumb.modelData.path) } }
                        }
                        Text {
                            visible: !crumb.last
                            anchors.baseline: parent.top; anchors.baselineOffset: header.textBaseline
                            text: "  ›  "; color: "white"; opacity: .35
                            font.family: header.fontFamily; font.pixelSize: 12.5 * header.tu
                        }
                    }
                }
            }
        }
    }
}
