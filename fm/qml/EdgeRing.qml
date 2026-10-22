import QtQuick
import QtQuick.Shapes

// Glass edge: a thin ring that is brightest at the top (Liquid Glass light from above).
Shape {
    id: ring
    property real radius: 18
    property real lineWidth: 1.2
    property real topAlpha: .65
    property real bottomAlpha: .16    // the window and sidebar use a bright bottom too, so all corners match
    preferredRendererType: Shape.CurveRenderer
    ShapePath {
        strokeColor: "transparent"
        fillRule: ShapePath.OddEvenFill
        fillGradient: LinearGradient {
            x1: 0; y1: 0; x2: 0; y2: ring.height
            GradientStop { position: 0; color: Qt.rgba(1, 1, 1, ring.topAlpha) }
            GradientStop { position: .25; color: Qt.rgba(1, 1, 1, .10) }
            GradientStop { position: .75; color: Qt.rgba(1, 1, 1, .10) }
            GradientStop { position: 1; color: Qt.rgba(1, 1, 1, ring.bottomAlpha) }
        }
        PathRectangle { x: 0; y: 0; width: ring.width; height: ring.height; radius: ring.radius }
        PathRectangle {
            x: ring.lineWidth; y: ring.lineWidth
            width: ring.width - 2 * ring.lineWidth; height: ring.height - 2 * ring.lineWidth
            radius: Math.max(0, ring.radius - ring.lineWidth)
        }
    }
}
