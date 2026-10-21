import QtQuick
import QtQuick.Pdf

// The first page of a PDF for the preview pane. A file of its own, loaded by URL: with QtQuick.Pdf
// in the start-up graph Qt's QML disk cache never matches for Main.qml (see Performance).
PdfPageImage {
    property string path
    document: PdfDocument { source: path ? "file://" + path : "" }
    currentFrame: 0
    fillMode: Image.PreserveAspectFit
    sourceSize: Qt.size(width * 2, height * 2)
}
