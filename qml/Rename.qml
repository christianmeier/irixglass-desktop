import QtQuick

// Inline rename field (F2). Enter applies, Esc or losing focus cancels.
Loader {
    id: rename
    property string path
    property string name
    property real u: 1
    property string fontFamily
    signal done()
    active: false
    height: 22 * u
    sourceComponent: Rectangle {
        radius: 5 * rename.u
        color: "white"
        border.color: "#0a84ff"; border.width: 2 * rename.u
        TextInput {
            id: input
            anchors.fill: parent; anchors.margins: 4 * rename.u
            verticalAlignment: TextInput.AlignVCenter
            horizontalAlignment: TextInput.AlignHCenter
            text: rename.name
            color: "black"; selectionColor: "#0a84ff"; selectedTextColor: "white"
            font.family: rename.fontFamily; font.pixelSize: 13 * rename.u
            clip: true
            Component.onCompleted: {
                forceActiveFocus()
                const dot = rename.name.lastIndexOf(".")
                select(0, dot > 0 ? dot : rename.name.length)
            }
            Keys.onReturnPressed: { Fm.rename(rename.path, text); rename.done() }
            Keys.onEnterPressed: { Fm.rename(rename.path, text); rename.done() }
            Keys.onEscapePressed: rename.done()
            onActiveFocusChanged: if (!activeFocus) rename.done()
        }
    }
}
