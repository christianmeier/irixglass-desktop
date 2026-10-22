import QtQuick

// A toolbar glass pill. The window tint has a hole under it (see Main.qml), so the
// blurred wallpaper shows through with this lighter tint, as in the mockup.
Item {
    id: pill
    property real radius: height / 2
    property color tint: Qt.rgba(58 / 255, 61 / 255, 72 / 255, .40)
    Rectangle { anchors.fill: parent; radius: pill.radius; color: pill.tint }
    EdgeRing { anchors.fill: parent; radius: pill.radius }
}
