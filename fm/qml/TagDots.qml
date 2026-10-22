import QtQuick

// Finder-style tag dots: small overlapping circles in the tags' colours, after an item's name.
Row {
    id: dots
    property var tags: []
    property var colorFor            // function(tagName) -> colour
    property real size: 9
    objectName: "tagDots"
    visible: tags.length > 0
    spacing: -size * .4
    Repeater {
        model: dots.tags.slice(0, 5)
        Rectangle {
            required property string modelData
            width: dots.size; height: width; radius: width / 2
            color: dots.colorFor ? dots.colorFor(modelData) : "gray"
            border.color: Qt.rgba(0, 0, 0, .45); border.width: 1
        }
    }
}
