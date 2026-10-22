import QtQuick

// The marker of a split-view side: no box, just the line under its header — blue on the active
// side (where F5 / F6 copy / move from), faint on the other. Both sides use it.
Item {
    id: frame
    property real u: 1
    property real lineY: 0
    property bool active: false
    Rectangle {
        x: 4 * frame.u; y: frame.lineY; width: frame.width - 8 * frame.u; height: frame.active ? 2 : 1; radius: 1
        color: frame.active ? "#0a84ff" : Qt.rgba(1, 1, 1, .10)
    }
}
