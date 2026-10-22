import QtQuick
import QtQuick.Controls

// Thin overlay scroll bar like macOS: no track or arrows, a rounded thumb that widens on hover
// and fades out when idle.
ScrollBar {
    id: bar
    property real u: 1
    policy: ScrollBar.AsNeeded
    padding: 2 * u
    minimumSize: .08
    contentItem: Rectangle {
        implicitWidth: (bar.hovered || bar.pressed ? 8 : 5) * bar.u   // vertical bars
        implicitHeight: implicitWidth                                 // horizontal bars
        radius: width / 2
        color: Qt.rgba(1, 1, 1, bar.pressed ? .55 : bar.hovered ? .45 : .32)
        opacity: bar.active || bar.hovered ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: 400 } }
        Behavior on implicitWidth { NumberAnimation { duration: 120 } }
        Behavior on implicitHeight { NumberAnimation { duration: 120 } }
    }
    background: Item {}
}
