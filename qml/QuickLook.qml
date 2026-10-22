import QtQuick
import QtQuick.Controls
import QtQuick.Pdf
import QtMultimedia
import org.kde.kirigami as K

// Quick Look (Space): a glass panel over the window showing the selected item. Images, PDFs,
// audio/video, text, folders (size) and everything else (big icon + details).
// The window drives it: `path` follows the selection, arrow keys keep working while it's open.
Item {
    id: ql
    objectName: "quickLook"
    property string path
    property real u: 1
    property real tu: 1
    property string fontFamily
    property var assetFor        // function(kind) -> icon url
    signal closeRequested()
    signal openRequested(string path)

    readonly property var info: path ? Fm.info(path) : ({})
    readonly property string kind: !path ? "" : info.isDir ? "folder"
                                   : info.remote ? "other"   // KIO items aren't downloaded for Quick Look
                                   : info.mime.startsWith("image/") ? "image"
                                   : info.mime === "application/pdf" ? "pdf"
                                   : info.mime.startsWith("video/") ? "video"
                                   : info.mime.startsWith("audio/") ? "audio"
                                   : textBody.length ? "text" : "other"
    readonly property string textBody: path && !info.isDir && !info.remote && !info.mime.startsWith("image/") && !info.mime.startsWith("video/")
                                       && !info.mime.startsWith("audio/") && info.mime !== "application/pdf"
                                       ? Fm.textPreview(path) : ""
    // Markdown rendered, code highlighted in Nord (Fm.richPreview); plain text otherwise.
    readonly property var rich: kind === "text" ? Fm.richPreview(path) : null
    property string folderSizeText: ""
    onPathChanged: {
        folderSizeText = ""
        if (path && info.isDir) { folderSizeText = "Calculating…"; Fm.folderSize(path) }
    }
    Connections {
        target: Fm
        function onFolderSizeReady(p, bytes, files, folders) {
            if (p === ql.path) ql.folderSizeText = bytes < 0 ? "--" : Fm.humanSize(bytes) + " · " + (files + folders).toLocaleString() + " items"
        }
    }

    // Dim the window behind; clicking outside closes.
    Rectangle { anchors.fill: parent; color: Qt.rgba(0, 0, 0, .35) }
    MouseArea { anchors.fill: parent; onClicked: ql.closeRequested(); onWheel: wheel => wheel.accepted = true }

    Item {
        id: panel
        anchors.centerIn: parent
        width: Math.min(parent.width - 80 * ql.u, 900 * ql.u)
        height: Math.min(parent.height - 60 * ql.u, 640 * ql.u)
        MouseArea { anchors.fill: parent }   // swallow clicks inside the panel
        Rectangle { anchors.fill: parent; radius: 20 * ql.u; color: Qt.rgba(20 / 255, 22 / 255, 29 / 255, .94) }
        EdgeRing { anchors.fill: parent; radius: 20 * ql.u }

        // Title bar: close, name, "Open with …"
        Rectangle {
            x: 16 * ql.u; y: 14 * ql.u; width: 13 * ql.tu; height: width; radius: width / 2; color: "#ff5f57"
            TapHandler { onTapped: ql.closeRequested() }
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            y: 12 * ql.u; width: parent.width - 320 * ql.u
            horizontalAlignment: Text.AlignHCenter; elide: Text.ElideMiddle
            text: ql.info.name || ""; color: "white"
            font.family: ql.fontFamily; font.pixelSize: 13.5 * ql.tu; font.weight: Font.DemiBold
        }
        Text {   // "Markdown" / the code's language
            visible: !!ql.rich && ql.rich.kind !== "plain"
            x: 40 * ql.u; y: 14 * ql.u
            text: ql.rich ? (ql.rich.kind === "markdown" ? "Markdown" : ql.rich.language || "") : ""
            color: "#88c0d0"; opacity: .9
            font.family: ql.fontFamily; font.pixelSize: 11.5 * ql.tu; font.weight: Font.DemiBold
        }
        Pill {
            visible: !ql.info.isDir && !!ql.info.openWith
            anchors.right: parent.right; anchors.rightMargin: 12 * ql.u
            y: 8 * ql.u; height: 26 * ql.tu; width: openLabel.implicitWidth + 28 * ql.u
            Text {
                id: openLabel
                anchors.centerIn: parent
                text: "Open with " + (ql.info.openWith || ""); color: "white"
                font.family: ql.fontFamily; font.pixelSize: 12 * ql.tu
            }
            TapHandler { onTapped: ql.openRequested(ql.path) }
        }

        Item {
            id: body
            x: 16 * ql.u; y: 44 * ql.u
            width: parent.width - 32 * ql.u; height: parent.height - y - 16 * ql.u
            clip: true

            Image {
                visible: ql.kind === "image"
                anchors.fill: parent
                source: ql.kind === "image" ? "file://" + ql.path : ""
                fillMode: Image.PreserveAspectFit; asynchronous: true; smooth: true; mipmap: true
                sourceSize: Qt.size(width * 2, height * 2)
            }
            Loader {
                anchors.fill: parent
                active: ql.kind === "pdf"
                sourceComponent: PdfMultiPageView {
                    document: PdfDocument { source: "file://" + ql.path }
                }
            }
            Loader {
                anchors.fill: parent
                active: ql.kind === "video" || ql.kind === "audio"
                sourceComponent: Item {
                    MediaPlayer {
                        id: player
                        source: "file://" + ql.path
                        videoOutput: video
                        audioOutput: AudioOutput {}
                        Component.onCompleted: play()
                    }
                    VideoOutput { id: video; anchors.fill: parent; visible: ql.kind === "video" }
                    Image {
                        visible: ql.kind === "audio"
                        anchors.centerIn: parent; width: 220 * ql.u; height: width
                        source: ql.assetFor ? ql.assetFor("doc") : ""; sourceSize: Qt.size(width * 2, height * 2)
                    }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter; anchors.bottom: parent.bottom
                        text: (player.playbackState === MediaPlayer.PlayingState ? "❚❚  " : "▶  ")
                              + Qt.formatTime(new Date(player.position), "m:ss") + " / " + Qt.formatTime(new Date(player.duration), "m:ss")
                        color: "white"; opacity: .8
                        font.family: ql.fontFamily; font.pixelSize: 13 * ql.tu
                    }
                    TapHandler { onTapped: player.playbackState === MediaPlayer.PlayingState ? player.pause() : player.play() }
                }
            }
            ScrollView {
                visible: ql.kind === "text"
                anchors.fill: parent
                ScrollBar.vertical: GlassScrollBar { u: ql.u; parent: textView; x: textView.width - width; height: textView.height }
                ScrollBar.horizontal: GlassScrollBar { u: ql.u; parent: textView; y: textView.height - height; width: textView.width }
                id: textView
                TextArea {
                    objectName: "quickLookText"
                    readOnly: true; selectByMouse: true
                    textFormat: TextEdit.RichText
                    text: ql.rich ? ql.rich.html : ""
                    readonly property bool markdown: !!ql.rich && ql.rich.kind === "markdown"
                    color: "#d8dee9"
                    wrapMode: markdown ? TextArea.Wrap : TextArea.NoWrap
                    leftPadding: (markdown ? 22 : 10) * ql.u; rightPadding: leftPadding; topPadding: (markdown ? 16 : 8) * ql.u
                    font.family: markdown ? ql.fontFamily : "SF Mono"; font.pixelSize: (markdown ? 14 : 12) * ql.tu
                    onLinkActivated: link => Qt.openUrlExternally(link)
                    background: Rectangle { color: "#2e3440"; opacity: .85; radius: 10 * ql.u }
                }
            }
            // Folders and anything without a preview: big icon and the details.
            Column {
                visible: ql.kind === "folder" || ql.kind === "other"
                anchors.centerIn: parent
                spacing: 8 * ql.tu
                Image {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: 220 * ql.u; height: width
                    source: ql.assetFor ? ql.assetFor(ql.kind === "folder" ? "folder" : ql.info.executable ? "exe" : "doc") : ""
                    sourceSize: Qt.size(width * 2, height * 2)
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: ql.info.name || ""; color: "white"
                    font.family: ql.fontFamily; font.pixelSize: 17 * ql.tu; font.weight: Font.DemiBold
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: (ql.info.kindText || "") + " · " + (ql.kind === "folder" ? ql.folderSizeText : ql.info.sizeText || "")
                    color: "white"; opacity: .6
                    font.family: ql.fontFamily; font.pixelSize: 12.5 * ql.tu
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: "Modified " + (ql.info.modified || "")
                    color: "white"; opacity: .45
                    font.family: ql.fontFamily; font.pixelSize: 12 * ql.tu
                }
            }
        }
    }
}
