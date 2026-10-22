import QtQuick

// Preview pane (View ▸ Show Preview, Ctrl+Shift+P): the focused item (or the folder) with a
// preview on top and its details below, like Finder's preview column.
Item {
    id: pane
    property string path
    property real u: 1
    property real tu: 1
    property string fontFamily
    property var assetFor
    property var sizes: ({})          // folder sizes from the window (Fm.folderSize)
    signal openRequested(string path)
    signal infoRequested()

    readonly property var info: path ? Fm.info(path) : ({})
    readonly property string mime: info.mime || ""
    // KIO items (info.remote) aren't downloaded for a preview: folder or icon only
    readonly property string kind: info.isDir ? "folder" : info.remote ? "other" : mime.startsWith("image/") ? "image" : mime === "application/pdf" ? "pdf"
                                   : textBody.length ? "text" : "other"
    readonly property string textBody: path && !info.isDir && !info.remote && !mime.startsWith("image/") && mime !== "application/pdf"
                                       && !mime.startsWith("video/") && !mime.startsWith("audio/") ? Fm.textPreview(path, 4096) : ""
    onPathChanged: if (info.isDir && sizes[path] === undefined) Fm.folderSize(path)

    Rectangle { anchors.fill: parent; radius: 18 * pane.u; color: Qt.rgba(42 / 255, 45 / 255, 56 / 255, .35) }
    EdgeRing { anchors.fill: parent; radius: 18 * pane.u }

    Item {
        id: shot
        x: 14 * pane.u; y: 14 * pane.u
        width: parent.width - 28 * pane.u; height: Math.min(parent.height * .45, 220 * pane.u)
        Image {
            visible: pane.kind === "image"
            anchors.fill: parent
            source: pane.kind === "image" ? "file://" + pane.path : ""
            fillMode: Image.PreserveAspectFit; asynchronous: true; smooth: true; mipmap: true
            sourceSize: Qt.size(width * 2, height * 2)
        }
        Loader {   // first page of a PDF
            anchors.fill: parent
            active: pane.kind === "pdf"
            source: "PdfPage.qml"
            // item is PdfPage.qml (it has `path`); qmllint only sees a QObject
            onLoaded: item.path = Qt.binding(() => pane.path)   // qmllint disable missing-property
        }
        Rectangle {   // the start of the text: Markdown rendered, code highlighted (Nord)
            visible: pane.kind === "text"
            anchors.fill: parent; radius: 8 * pane.u; color: "#2e3440"; opacity: .9; clip: true
            Text {
                objectName: "previewText"
                readonly property var rich: pane.kind === "text" ? Fm.richPreview(pane.path, 4096) : null
                readonly property bool markdown: !!rich && rich.kind === "markdown"
                x: 8 * pane.u; y: 6 * pane.u; width: parent.width - 16 * pane.u
                textFormat: Text.RichText
                text: rich ? rich.html.replace(/<img[^>]*>/g, "") : ""   // no pictures in the small preview
                color: "#d8dee9"; wrapMode: Text.Wrap
                font.family: markdown ? pane.fontFamily : "SF Mono"; font.pixelSize: (markdown ? 10.5 : 9.5) * pane.tu
            }
        }
        Image {
            visible: pane.kind === "folder" || pane.kind === "other"
            anchors.centerIn: parent; width: Math.min(parent.width, parent.height) * .9; height: width
            source: pane.assetFor ? pane.assetFor(pane.kind === "folder" ? "folder" : pane.info.executable ? "exe" : "doc") : ""
            sourceSize: Qt.size(width * 2, height * 2)
        }
    }

    Column {
        x: 14 * pane.u; y: shot.y + shot.height + 12 * pane.u
        width: parent.width - 28 * pane.u
        spacing: 4 * pane.tu
        Text {
            width: parent.width; elide: Text.ElideMiddle; horizontalAlignment: Text.AlignHCenter
            text: pane.info.name || ""; color: "white"
            font.family: pane.fontFamily; font.pixelSize: 14 * pane.tu; font.weight: Font.DemiBold
        }
        Text {
            width: parent.width; horizontalAlignment: Text.AlignHCenter
            text: (pane.info.kindText || "") + " · " + (pane.info.isDir ? (pane.sizes[pane.path] || Fm.cachedFolderSize(pane.path) || "…") : (pane.info.size >= 0 ? Fm.humanSize(pane.info.size) : ""))
            color: "white"; opacity: .6
            font.family: pane.fontFamily; font.pixelSize: 12 * pane.tu
        }
        Item { width: 1; height: 8 * pane.tu }
        Rectangle { width: parent.width; height: 1; color: Qt.rgba(1, 1, 1, .1) }
        Item { width: 1; height: 4 * pane.tu }
        Repeater {
            model: [["Tags", (pane.info.tags || []).join(", ")], ["Created", pane.info.created], ["Modified", pane.info.modified],
                    ["Where", pane.info.where]].filter(r => r[1])
            Column {
                required property var modelData
                width: parent.width
                Text {
                    text: modelData[0]; color: "white"; opacity: .5
                    font.family: pane.fontFamily; font.pixelSize: 11 * pane.tu
                }
                Text {
                    width: parent.width; wrapMode: Text.WrapAtWordBoundaryOrAnywhere; maximumLineCount: 2; elide: Text.ElideRight
                    text: modelData[1] || ""; color: "white"
                    font.family: pane.fontFamily; font.pixelSize: 12 * pane.tu
                }
            }
        }
        Item { width: 1; height: 8 * pane.tu }
        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 8 * pane.u
            Repeater {
                model: pane.info.isDir ? ["Get Info"] : ["Open", "Get Info"]
                Pill {
                    required property string modelData
                    height: 24 * pane.tu; width: label.implicitWidth + 24 * pane.u
                    Text {
                        id: label
                        anchors.centerIn: parent
                        text: modelData; color: "white"
                        font.family: pane.fontFamily; font.pixelSize: 12 * pane.tu
                    }
                    TapHandler { onTapped: modelData === "Open" ? pane.openRequested(pane.path) : pane.infoRequested() }
                }
            }
        }
    }
}
