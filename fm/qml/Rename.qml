import QtQuick

// Inline rename field. With `path` set it renames that file (F2); without, it only emits `committed`
// (used for sidebar labels). Enter applies, Esc or losing focus cancels.
Loader {
    id: rename
    property string path
    property string name
    property real u: 1
    property string fontFamily
    property int align: TextInput.AlignHCenter
    signal done()
    signal committed(string text)
    active: false
    visible: active      // takes no room in a Row (the list view's columns) unless it's in use
    height: 22 * u
    sourceComponent: Rectangle {
        radius: 5 * rename.u
        color: "white"
        border.color: "#0a84ff"; border.width: 2 * rename.u
        TextInput {
            id: input
            anchors.fill: parent; anchors.margins: 4 * rename.u
            verticalAlignment: TextInput.AlignVCenter
            horizontalAlignment: rename.align
            text: rename.name
            color: "black"; selectionColor: "#0a84ff"; selectedTextColor: "white"
            font.family: rename.fontFamily; font.pixelSize: 13 * rename.u
            clip: true
            Component.onCompleted: {
                forceActiveFocus()
                const dot = rename.name.lastIndexOf(".")
                select(0, dot > 0 ? dot : rename.name.length)
            }
            function apply() {
                if (rename.path) Fm.rename(rename.path, text)
                rename.committed(text)
                rename.done()
            }
            Keys.onReturnPressed: apply()
            Keys.onEnterPressed: apply()
            Keys.onEscapePressed: rename.done()
            onActiveFocusChanged: if (!activeFocus) rename.done()
        }
    }
}
