import QtQuick
import QtQuick.Window
import QtQuick.Layouts

// Get Info (Ctrl+I): one small window per item, like Finder. Folder sizes are counted in the
// background (Fm.folderSize).
Window {
    id: infoWin
    property string path
    property real tu: 1
    property string fontFamily
    property var assetFor
    readonly property var info: Fm.info(path)
    property string sizeText: info.isDir ? "Calculating…" : info.sizeText

    title: info.name + " Info"
    width: 300 * tu; height: column.implicitHeight + 32 * tu
    minimumWidth: 260 * tu
    color: "#1e2028"
    visible: true

    Component.onCompleted: if (info.isDir) Fm.folderSize(path)
    Connections {
        target: Fm
        function onFolderSizeReady(p, bytes, files, folders) {
            if (p === infoWin.path)
                infoWin.sizeText = bytes < 0 ? "--" : Fm.humanSize(bytes) + " on disk (" + (files + folders).toLocaleString() + " items)"
        }
    }
    Shortcut { sequences: ["Ctrl+W", "Escape"]; onActivated: infoWin.close() }

    ColumnLayout {
        id: column
        x: 16 * infoWin.tu; y: 16 * infoWin.tu
        width: parent.width - 32 * infoWin.tu
        spacing: 10 * infoWin.tu

        RowLayout {
            spacing: 10 * infoWin.tu
            Image {
                Layout.preferredWidth: 56 * infoWin.tu; Layout.preferredHeight: 56 * infoWin.tu
                source: infoWin.assetFor ? infoWin.assetFor(infoWin.info.isDir ? "folder"
                                                            : infoWin.info.mime.startsWith("image/") ? "image"
                                                            : infoWin.info.executable ? "exe" : "doc") : ""
                sourceSize: Qt.size(112, 112)
            }
            ColumnLayout {
                spacing: 2 * infoWin.tu
                Text {
                    Layout.fillWidth: true; elide: Text.ElideMiddle
                    text: infoWin.info.name; color: "white"
                    font.family: infoWin.fontFamily; font.pixelSize: 14 * infoWin.tu; font.weight: Font.DemiBold
                }
                Text {
                    text: infoWin.sizeText; color: "white"; opacity: .6
                    font.family: infoWin.fontFamily; font.pixelSize: 12 * infoWin.tu
                }
            }
        }
        Rectangle { Layout.fillWidth: true; height: 1; color: Qt.rgba(1, 1, 1, .1) }

        GridLayout {
            columns: 2; columnSpacing: 10 * infoWin.tu; rowSpacing: 5 * infoWin.tu
            Layout.fillWidth: true
            Repeater {
                model: [
                    ["Kind:", infoWin.info.kindText + (infoWin.info.isLink ? " (alias)" : "")],
                    ["Size:", infoWin.sizeText],
                    ["Where:", infoWin.info.where],
                    ["Original:", infoWin.info.isLink ? infoWin.info.target : ""],
                    ["Created:", infoWin.info.created],
                    ["Modified:", infoWin.info.modified],
                    ["Opened:", infoWin.info.accessed],
                    ["Open with:", infoWin.info.openWith],
                    ["Owner:", infoWin.info.owner + " — " + infoWin.info.permOwner],
                    ["Group:", infoWin.info.group + " — " + infoWin.info.permGroup],
                    ["Everyone:", infoWin.info.permOther]
                ].filter(r => r[1])
                delegate: Item {
                    required property var modelData
                    required property int index
                    Layout.columnSpan: 2; Layout.fillWidth: true
                    implicitHeight: Math.max(k.implicitHeight, v.implicitHeight)
                    Text {
                        id: k
                        width: 76 * infoWin.tu; horizontalAlignment: Text.AlignRight
                        text: modelData[0]; color: "white"; opacity: .5
                        font.family: infoWin.fontFamily; font.pixelSize: 12 * infoWin.tu
                    }
                    Text {
                        id: v
                        x: k.width + 10 * infoWin.tu; width: parent.width - x
                        text: modelData[1]; color: "white"; wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                        font.family: infoWin.fontFamily; font.pixelSize: 12 * infoWin.tu
                    }
                }
            }
        }
    }
}
