import QtQuick
import QtQuick.Controls
import QtWebEngine
import org.kde.kirigami as K

// A web page in a tab, like the terminal tabs: Qt WebEngine (Chromium). Firefox itself can't be
// embedded on Wayland, so "Open in Firefox" hands the page over. It has the same small API as
// TerminalView (title, start(), fontPointSize / setFontSize / fitWidth, finished, …), so tabs,
// tab titles and the thumbwheel (page zoom: 12 pt = 100 %) treat both alike.
FocusScope {   // activeFocus while the page or the address has the keyboard
    id: b
    objectName: "browserView"
    property url startUrl: ""
    property var webProfile                  // Main's one WebProfile
    property real u: 1
    property real tu: 1
    property string fontFamily
    readonly property string title: view.title || (view.url.toString() ? view.url.toString() : "New Page")
    readonly property string currentDirectory: ""
    readonly property bool running: true
    property real fontPointSize: 12
    readonly property alias url: view.url
    readonly property alias loading: view.loading
    signal fontChanged()
    signal finished(int exitCode)
    onFontPointSizeChanged: fontChanged()

    function start() { view.url = startUrl.toString() ? startUrl : "https://duckduckgo.com/" }
    function setFontSize(points) { fontPointSize = Math.max(6, Math.min(30, points)) }
    function fitWidth() { setFontSize(12) }          // the home button: 100 %
    function zoomBy(f) { setFontSize(fontPointSize * f) }
    function changeDirectory(path) { return false }  // folders don't move a web page
    function sendText(text) {}
    function insertPaths(paths) { if (paths.length) view.url = "file://" + paths[0] }
    // What was typed: a URL, a path, or words to search for.
    function go(text) {
        const t = text.trim()
        if (!t) return
        if (t.startsWith("/")) view.url = "file://" + t
        else if (/^[a-z]+:\/\//i.test(t) || t.startsWith("about:")) view.url = t
        else if (/^[^\s]+\.[a-z]{2,}(\/.*)?$/i.test(t) || /^localhost(:\d+)?/.test(t)) view.url = "https://" + t
        else view.url = "https://duckduckgo.com/?q=" + encodeURIComponent(t)
        view.forceActiveFocus()
    }
    function focusAddress() { address.forceActiveFocus(); address.selectAll() }
    // fm's Back / Forward / Reload keys act on the page while it shows (Main routes them here).
    function goBack() { view.goBack() }
    function goForward() { view.goForward() }
    function reload() { view.reload() }

    // ---- the page's bar: ‹ › ↻, the address, Open in Firefox ---------------------------------
    Item {
        id: bar
        width: parent.width; height: 34 * b.tu
        Row {
            id: navButtons
            x: 4 * b.u; anchors.verticalCenter: parent.verticalCenter
            spacing: 2 * b.u
            Repeater {
                model: [
                    { glyph: "‹", tip: "Back (Alt+←)", enabled: view.canGoBack, act: () => view.goBack() },
                    { glyph: "›", tip: "Forward (Alt+→)", enabled: view.canGoForward, act: () => view.goForward() },
                    { glyph: view.loading ? "×" : "↻", tip: view.loading ? "Stop" : "Reload (Ctrl+R)", enabled: true,
                      act: () => view.loading ? view.stop() : view.reload() }
                ]
                Rectangle {
                    required property var modelData
                    width: 26 * b.u; height: 26 * b.tu; radius: 7 * b.u
                    color: navHover.hovered && modelData.enabled ? Qt.rgba(1, 1, 1, .12) : "transparent"
                    opacity: modelData.enabled ? .9 : .3
                    Text {
                        anchors.centerIn: parent; text: modelData.glyph; color: "white"
                        font.family: b.fontFamily; font.pixelSize: (modelData.glyph === "↻" ? 15 : 19) * b.tu
                    }
                    HoverHandler { id: navHover }
                    TapHandler { onTapped: if (modelData.enabled) modelData.act() }
                    ToolTip.visible: navHover.hovered; ToolTip.delay: 700; ToolTip.text: modelData.tip
                }
            }
        }
        TextField {
            id: address
            objectName: "browserAddress"
            anchors.left: navButtons.right; anchors.leftMargin: 6 * b.u
            anchors.right: firefoxButton.left; anchors.rightMargin: 8 * b.u
            anchors.verticalCenter: parent.verticalCenter
            height: 26 * b.tu
            text: activeFocus ? text : view.url.toString()
            placeholderText: "Search or enter address"
            font.family: b.fontFamily; font.pixelSize: 12.5 * b.tu
            color: "white"
            leftPadding: 12 * b.u; rightPadding: 12 * b.u
            background: Rectangle {
                radius: height / 2
                color: Qt.rgba(1, 1, 1, address.activeFocus ? .14 : .08)
                border.color: address.activeFocus ? Qt.rgba(10 / 255, 132 / 255, 1, .8) : "transparent"
                Rectangle {   // page load progress along the bottom
                    visible: view.loading
                    x: parent.radius / 2; y: parent.height - 2 * b.u; height: 2 * b.u; radius: 1 * b.u
                    width: (parent.width - parent.radius) * view.loadProgress / 100
                    color: "#0a84ff"
                }
            }
            onAccepted: b.go(text)
            Keys.onEscapePressed: { text = view.url.toString(); view.forceActiveFocus() }
        }
        Rectangle {
            id: firefoxButton
            objectName: "openInFirefox"
            anchors.right: parent.right; anchors.rightMargin: 4 * b.u
            anchors.verticalCenter: parent.verticalCenter
            width: ffRow.implicitWidth + 18 * b.u; height: 26 * b.tu; radius: height / 2
            color: ffHover.hovered ? Qt.rgba(1, 1, 1, .16) : Qt.rgba(1, 1, 1, .08)
            Row {
                id: ffRow
                anchors.centerIn: parent; spacing: 6 * b.u
                K.Icon { width: 16 * b.tu; height: width; source: "firefox"; anchors.verticalCenter: parent.verticalCenter }
                Text {
                    text: "Open in Firefox"; color: "white"; anchors.verticalCenter: parent.verticalCenter
                    font.family: b.fontFamily; font.pixelSize: 12 * b.tu
                }
            }
            HoverHandler { id: ffHover; cursorShape: Qt.PointingHandCursor }
            TapHandler { onTapped: Fm.openInFirefox(view.url.toString()) }
        }
    }

    // Downloads (into ~/Downloads, Fm.webProfile): "name 42 %", then "name ✓ — Show" for a while.
    property string downloadPath: ""
    property string downloadText: ""
    signal showFileRequested(string path)
    Timer { id: downloadHide; interval: 8000; onTriggered: b.downloadText = "" }
    Connections {
        target: b.webProfile
        enabled: b.visible
        function onFileDownloadProgress(path, percent) { b.downloadPath = path; b.downloadText = path.split("/").pop() + "  " + percent + " %"; downloadHide.stop() }
        function onFileDownloadFinished(path, ok) {
            b.downloadPath = path
            b.downloadText = path.split("/").pop() + (ok ? "  ✓  Show in Downloads" : "  — failed")
            downloadHide.restart()
        }
    }
    Rectangle {
        objectName: "downloadNotice"
        visible: b.downloadText !== ""
        z: 3
        anchors.right: parent.right; anchors.rightMargin: 12 * b.u
        y: bar.height + 12 * b.u
        width: dlText.implicitWidth + 24 * b.u; height: 28 * b.tu; radius: height / 2
        color: Qt.rgba(28 / 255, 30 / 255, 38 / 255, .95); border.color: Qt.rgba(1, 1, 1, .15)
        Text {
            id: dlText
            anchors.centerIn: parent; text: b.downloadText; color: "white"
            font.family: b.fontFamily; font.pixelSize: 12 * b.tu
        }
        TapHandler { onTapped: if (b.downloadText.includes("✓")) b.showFileRequested(b.downloadPath) }
    }

    // ---- the page ----------------------------------------------------------------------------
    Rectangle {   // rounded frame, so the page sits in the glass like the terminal does
        id: frame
        x: 0; y: bar.height + 4 * b.u; width: parent.width; height: parent.height - y
        radius: 8 * b.u; color: "#2e3440"; clip: true
        WebEngineView {
            id: view
            objectName: "webView"
            anchors.fill: parent
            profile: b.webProfile   // shared by all web tabs (WebProfile.qml)
            zoomFactor: b.fontPointSize / 12
            onNewWindowRequested: request => { url = request.requestedUrl }   // target=_blank: same tab
            settings.pluginsEnabled: true
            settings.fullScreenSupportEnabled: true
            onFullScreenRequested: request => request.accept()
        }
    }

}
