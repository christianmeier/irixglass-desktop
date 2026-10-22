import QtQuick
import QtQuick.Shapes

// The tab bar: IRIX notebook tabs along the bottom of the page (like the Icon Catalog's page
// tabs), with "+" for a new tab and the IRIX "////" stack when tabs don't fit. Dragging files
// over a tab switches to it after a moment (spring-loaded); dropping puts them in its folder.
Item {
    id: strip
    objectName: "tabStrip"
    property var tabs: []              // [{title, dir}]
    property int current: 0
    property real u: 1
    property real tu: 1
    property string fontFamily
    property int springDelay: 900
    readonly property bool overflow: list.contentWidth > list.width + 1
    signal activated(int index)
    signal closeRequested(int index)
    signal middleClicked(int index)
    signal menuRequested(int index)
    signal newRequested()
    signal overflowRequested()
    signal filesDropped(int index, var urls, bool move)

    // The page's bottom edge, with a gap where the active tab joins it.
    // currentItem notifies (itemAtIndex doesn't); clamp to the visible part of the list.
    readonly property var activeItem: list.currentItem
    readonly property real gapX: activeItem ? Math.max(0, Math.min(list.width, activeItem.x - list.contentX)) + list.x : -1
    readonly property real gapW: activeItem ? Math.max(0, Math.min(list.width + list.x - gapX, activeItem.width)) : 0
    Rectangle { x: -22 * strip.u; y: 0; width: Math.max(0, strip.gapX + 22 * strip.u); height: 1; color: Qt.rgba(1, 1, 1, .14) }
    Rectangle {
        x: strip.gapX >= 0 ? strip.gapX + strip.gapW : -22 * strip.u
        y: 0; width: strip.width + 12 * strip.u - x; height: 1; color: Qt.rgba(1, 1, 1, .14)
    }

    ListView {
        id: list
        objectName: "tabList"
        x: 0; y: 0
        width: strip.width - 64 * strip.u
        height: 24 * strip.tu
        orientation: ListView.Horizontal
        spacing: -10 * strip.u                 // slanted sides interleave
        interactive: false
        clip: true
        model: strip.tabs
        currentIndex: strip.current
        onCurrentIndexChanged: positionViewAtIndex(currentIndex, ListView.Contain)
        WheelHandler {
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
            onWheel: e => {
                const d = (e.pixelDelta.x || e.pixelDelta.y) || (e.angleDelta.x || e.angleDelta.y) / 2
                list.contentX = Math.max(0, Math.min(list.contentWidth - list.width, list.contentX - d))
            }
        }
        delegate: NotebookTab {
            id: nb
            required property var modelData
            required property int index
            objectName: "tab" + index
            title: modelData.title
            tip: modelData.dir
            active: index === strip.current
            closable: strip.tabs.length > 1
            u: strip.u; tu: strip.tu; fontFamily: strip.fontFamily
            width: implicitWidth; height: implicitHeight
            onClicked: strip.activated(index)
            onMiddleClicked: strip.middleClicked(index)
            onCloseClicked: strip.closeRequested(index)
            onMenuRequested: strip.menuRequested(index)
            DropArea {
                anchors.fill: parent
                keys: ["text/uri-list"]
                onEntered: if (!nb.active) spring.restart()
                onExited: spring.stop()
                onDropped: d => { spring.stop(); strip.filesDropped(nb.index, d.urls, d.proposedAction === Qt.MoveAction); d.accept() }
            }
            Timer { id: spring; objectName: "tabSpring"; interval: strip.springDelay; onTriggered: strip.activated(nb.index) }
        }
    }
    Text {   // IRIX overflow stack: click for the list of all tabs
        objectName: "tabOverflow"
        visible: strip.overflow
        x: list.x + list.width + 4 * strip.u; anchors.verticalCenter: list.verticalCenter
        text: "////"; color: "white"; opacity: ofHover.hovered ? .9 : .45
        font.family: strip.fontFamily; font.pixelSize: 13 * strip.tu; font.italic: true
        HoverHandler { id: ofHover; cursorShape: Qt.PointingHandCursor }
        TapHandler { onTapped: strip.overflowRequested() }
    }
    Text {   // new tab
        objectName: "tabNew"
        x: strip.overflow ? list.x + list.width + 34 * strip.u : list.x + Math.min(list.contentWidth, list.width) + 8 * strip.u
        anchors.verticalCenter: list.verticalCenter
        text: "+"; color: "white"; opacity: plusHover.hovered ? .9 : .5
        font.family: strip.fontFamily; font.pixelSize: 17 * strip.tu; font.weight: Font.DemiBold
        HoverHandler { id: plusHover; cursorShape: Qt.PointingHandCursor }
        TapHandler { onTapped: strip.newRequested() }
    }
}
