import QtQuick
import QtQuick.Shapes
import QtQuick.Controls

// One IRIX notebook tab (Icon Catalog page tab), hanging below the page: a trapezoid wide at
// the page edge and narrow at the bottom, slanted sides overlapping its neighbours. The active
// tab is joined to the page (no line across its top) and lit like the glass pills.
Item {
    id: tab
    property string title
    property string tip
    property bool active: false
    property bool closable: true
    property real u: 1
    property real tu: 1
    property string fontFamily
    readonly property real s: 10 * u                  // slant
    signal clicked()
    signal middleClicked()
    signal closeClicked()
    signal menuRequested()

    implicitWidth: Math.max(96 * u, Math.min(200 * u, label.implicitWidth + 2 * s + 34 * u))
    implicitHeight: 24 * tu
    z: active ? 2 : 1

    Shape {
        anchors.fill: parent
        preferredRendererType: Shape.CurveRenderer
        ShapePath {   // fill
            strokeColor: "transparent"
            fillColor: tab.active ? Qt.rgba(1, 1, 1, .12) : hover.hovered ? Qt.rgba(1, 1, 1, .06) : Qt.rgba(0, 0, 0, .18)
            startX: 0; startY: 0
            PathLine { x: tab.width; y: 0 }
            PathLine { x: tab.width - tab.s; y: tab.height }
            PathLine { x: tab.s; y: tab.height }
            PathLine { x: 0; y: 0 }
        }
        ShapePath {   // outline on three sides only: open towards the page
            fillColor: "transparent"
            strokeColor: Qt.rgba(1, 1, 1, tab.active ? .38 : .12); strokeWidth: 1
            startX: 0; startY: 0
            PathLine { x: tab.s; y: tab.height }
            PathLine { x: tab.width - tab.s; y: tab.height }
            PathLine { x: tab.width; y: 0 }
        }
    }
    Text {
        id: label
        anchors.verticalCenter: parent.verticalCenter
        x: tab.s + 20 * tab.u; width: tab.width - 2 * tab.s - 30 * tab.u
        horizontalAlignment: Text.AlignHCenter; elide: Text.ElideMiddle
        text: tab.title; color: "white"; opacity: tab.active ? .95 : .55
        font.family: tab.fontFamily; font.pixelSize: 12 * tab.tu
        font.weight: tab.active ? Font.Medium : Font.Normal
    }
    Text {   // × on hover (only when there is more than one tab)
        objectName: "tabClose"
        visible: tab.closable && hover.hovered
        x: tab.s + 6 * tab.u; anchors.verticalCenter: parent.verticalCenter
        text: "×"; color: "white"; opacity: closeHover.hovered ? .95 : .55
        font.pixelSize: 14 * tab.tu
        HoverHandler { id: closeHover; cursorShape: Qt.PointingHandCursor }
        TapHandler { onTapped: tab.closeClicked() }
    }
    HoverHandler { id: hover }
    ToolTip.visible: hover.hovered && tab.tip !== ""
    ToolTip.delay: 700
    ToolTip.text: tab.tip
    MouseArea {
        anchors.fill: parent
        anchors.leftMargin: closeHover.hovered ? 30 * tab.u : 0
        acceptedButtons: Qt.LeftButton | Qt.MiddleButton | Qt.RightButton
        onClicked: m => {
            if (m.button === Qt.MiddleButton) tab.middleClicked()
            else if (m.button === Qt.RightButton) tab.menuRequested()
            else tab.clicked()
        }
    }
}
