import QtQuick
import QtQuick.Shapes

Row {
    id: lights
    // Same size and look as the IrixGlass window decoration's lights on the other windows:
    // 13.3 px circles, 26 px apart centre to centre.
    property real diameter: 40 / 3
    property var window
    // Grey all the time (the user's choice), like the IrixGlass decoration, GTK and Firefox lights;
    // the active window shows the glyphs on hover so the three can be told apart.
    readonly property bool inactive: !!window && !window.active
    spacing: 38 / 3
    HoverHandler { id: hover }
    Repeater {
        model: [{ c: "#ff5f57", g: "×" }, { c: "#febc2e", g: "−" }, { c: "#28c840", g: "+" }]
        Rectangle {
            required property var modelData
            required property int index
            width: lights.diameter; height: width; radius: width / 2
            color: "transparent"
            // Drawn as true circles (CurveRenderer), like the window decoration's SVG lights:
            // the colour fill and a faint 0.5 px dark outline straddling its edge.
            Shape {
                anchors.fill: parent
                preferredRendererType: Shape.CurveRenderer
                ShapePath {
                    fillColor: Qt.rgba(1, 1, 1, .22)
                    strokeColor: Qt.rgba(0, 0, 0, .25); strokeWidth: .5
                    PathAngleArc {
                        centerX: lights.diameter / 2; centerY: lights.diameter / 2
                        radiusX: lights.diameter / 2; radiusY: lights.diameter / 2
                        startAngle: 0; sweepAngle: 360
                    }
                }
            }
            Text {
                anchors.centerIn: parent
                anchors.verticalCenterOffset: -.5
                visible: hover.hovered && !lights.inactive
                text: modelData.g
                color: Qt.rgba(0, 0, 0, .7)
                font.pixelSize: lights.diameter * .8; font.bold: true
            }
            TapHandler {
                onTapped: {
                    if (index === 0) lights.window.close()
                    else if (index === 1) lights.window.showMinimized()
                    else lights.window.visibility === Window.Maximized ? lights.window.showNormal() : lights.window.showMaximized()
                }
            }
        }
    }
}
