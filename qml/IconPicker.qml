import QtQuick
import QtQuick.Controls
import org.kde.kirigami as K

// Choose an icon for a sidebar entry: an emoji or an app's icon. The result is a string kept on
// the entry: "emoji:🏠" or an icon name ("firefox"); "" = the standard outline.
Popup {
    id: picker
    objectName: "iconPicker"
    property real u: 1
    property real tu: 1
    property string fontFamily
    property string current
    signal picked(string icon)

    property int page: 0                 // 0 emoji, 1 apps
    property var apps: []
    onOpened: { filter.text = ""; if (!apps.length) apps = Fm.applications(); filter.forceActiveFocus() }

    // A short, folder-minded set first, then everyday ones.
    readonly property var emojis: [
        "📁", "📂", "🗂️", "🏠", "💻", "🖥️", "📥", "📤", "📄", "📝", "📚", "📖", "🗃️", "🗄️", "🗑️", "⭐",
        "❤️", "🔖", "📌", "📎", "🔒", "🔑", "⚙️", "🛠️", "🔧", "🧪", "🐞", "🚀", "🎯", "💡", "📦", "🧩",
        "🎵", "🎧", "🎬", "📷", "🖼️", "🎨", "🎮", "📺", "📻", "🎤", "📸", "🎞️", "🖌️", "✏️", "🖊️", "📐",
        "🌍", "☁️", "🌐", "📡", "💾", "💿", "📀", "🔌", "🔋", "🖨️", "⌨️", "🖱️", "📱", "⌚", "🧭", "🗺️",
        "💼", "💰", "🧾", "📊", "📈", "📅", "🗓️", "✉️", "📬", "💬", "👥", "👤", "🏢", "🏫", "🏥", "🏦",
        "🐧", "🐍", "🦀", "☕", "🐳", "🐙", "🦊", "🐱", "🐶", "🌲", "🌸", "🍀", "🔥", "❄️", "⚡", "🌈",
        "🔴", "🟠", "🟡", "🟢", "🔵", "🟣", "⚫", "⚪"
    ]
    readonly property var shownApps: {
        const q = filter.text.trim().toLowerCase()
        return q ? apps.filter(a => a.name.toLowerCase().includes(q) || a.icon.toLowerCase().includes(q)) : apps
    }

    width: 404 * u; height: 384 * u
    padding: 0
    modal: true
    background: Item {
        Rectangle { anchors.fill: parent; radius: 14 * picker.u; color: Qt.rgba(28 / 255, 30 / 255, 38 / 255, .97) }
        EdgeRing { anchors.fill: parent; radius: 14 * picker.u }
    }

    contentItem: Item {
        // Emoji | Apps, as a small segmented pill
        Row {
            id: seg
            x: 14 * picker.u; y: 12 * picker.u
            spacing: 2 * picker.u
            Repeater {
                model: ["Emoji", "App Icons"]
                Rectangle {
                    required property int index
                    required property string modelData
                    width: 84 * picker.u; height: 24 * picker.tu; radius: height / 2
                    color: picker.page === index ? Qt.rgba(1, 1, 1, .18) : segHover.hovered ? Qt.rgba(1, 1, 1, .07) : "transparent"
                    Text {
                        anchors.centerIn: parent; text: modelData; color: "white"
                        font.family: picker.fontFamily; font.pixelSize: 12 * picker.tu
                    }
                    HoverHandler { id: segHover }
                    TapHandler { onTapped: picker.page = index }
                }
            }
        }
        TextField {
            id: filter
            visible: picker.page === 1
            anchors.right: parent.right; anchors.rightMargin: 14 * picker.u
            y: 10 * picker.u; width: 180 * picker.u; height: 28 * picker.tu
            placeholderText: "Search apps"
            font.family: picker.fontFamily; font.pixelSize: 12 * picker.tu
            onTextChanged: if (text) picker.page = 1
            Keys.onEscapePressed: picker.close()
        }

        GridView {
            id: grid
            objectName: picker.page === 0 ? "emojiGrid" : "appGrid"
            x: 10 * picker.u; y: 46 * picker.u
            width: parent.width - 20 * picker.u; height: parent.height - y - 46 * picker.u
            clip: true
            cellWidth: width / 8; cellHeight: cellWidth
            model: picker.page === 0 ? picker.emojis : picker.shownApps
            ScrollBar.vertical: GlassScrollBar { u: picker.u }
            delegate: Item {
                id: cell
                required property var modelData
                readonly property string value: picker.page === 0 ? "emoji:" + modelData : modelData.icon
                width: grid.cellWidth; height: grid.cellHeight
                Rectangle {
                    anchors.fill: parent; anchors.margins: 2 * picker.u; radius: 8 * picker.u
                    color: picker.current === cell.value ? Qt.rgba(10 / 255, 132 / 255, 255 / 255, .55)
                           : cellHover.hovered ? Qt.rgba(1, 1, 1, .12) : "transparent"
                }
                Text {
                    visible: picker.page === 0
                    anchors.centerIn: parent
                    text: picker.page === 0 ? cell.modelData : ""
                    font.family: "Noto Color Emoji"; font.pixelSize: 24 * picker.tu
                }
                K.Icon {
                    visible: picker.page === 1
                    anchors.centerIn: parent; width: 30 * picker.tu; height: width
                    source: picker.page === 1 ? cell.modelData.icon : ""
                    roundToIconSize: false
                }
                HoverHandler { id: cellHover }
                ToolTip.visible: picker.page === 1 && cellHover.hovered
                ToolTip.delay: 500
                ToolTip.text: picker.page === 1 ? cell.modelData.name : ""
                TapHandler { onTapped: { picker.picked(cell.value); picker.close() } }
            }
        }

        // the standard outline again
        Rectangle {
            anchors.left: parent.left; anchors.leftMargin: 14 * picker.u
            anchors.bottom: parent.bottom; anchors.bottomMargin: 10 * picker.u
            width: resetLabel.implicitWidth + 24 * picker.u; height: 26 * picker.tu; radius: height / 2
            color: resetHover.hovered ? Qt.rgba(1, 1, 1, .16) : Qt.rgba(1, 1, 1, .08)
            Text {
                id: resetLabel
                anchors.centerIn: parent; text: "Standard Icon"; color: "white"
                font.family: picker.fontFamily; font.pixelSize: 12 * picker.tu
            }
            HoverHandler { id: resetHover }
            TapHandler { onTapped: { picker.picked(""); picker.close() } }
        }
        Text {
            anchors.right: parent.right; anchors.rightMargin: 16 * picker.u
            anchors.bottom: parent.bottom; anchors.bottomMargin: 15 * picker.u
            text: picker.page === 1 ? picker.shownApps.length + " apps" : ""
            color: "white"; opacity: .45
            font.family: picker.fontFamily; font.pixelSize: 11 * picker.tu
        }
    }
}
