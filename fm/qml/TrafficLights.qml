import QtQuick
import QtQuick.Shapes

// iPadOS 26 window controls (WWDC25 session 208), the same on every window of the desktop: Firefox
// (configs/firefox/customChrome.css), the IrixGlass title bars (theme/gen-decoration.py) and GTK
// (theme/gen-gtk.py) use these numbers too.
// Measured from Apple's video frames. At rest a grey glass capsule (38.67 × 20.67 px, see-through
// with a 1 device px light rim) holds three small muted dots (6 px, 9.33 px apart). Under the pointer
// it grows to the right (72.67 × 28.67 px), the dots become the vivid 16 px lights (22 px apart),
// and the glyphs grow out of a point; a little spring overshoot. Only the light under the pointer
// glows and lifts. Inactive windows: grey dots. The item has the grown size; x is the capsule's left edge.
Item {
    id: lights
    property var window
    readonly property bool inactive: !!window && !window.active
    property bool forceGrown: false   // tests and screenshots: show the grown state
    // Hover is tracked on this item, not the capsule: the lights lie on top of the capsule and
    // take its hover away, which shrank the group under the pointer and made clicks miss. Only
    // the capsule's current outline counts, so the empty part right of the resting capsule doesn't.
    readonly property bool grown: forceGrown || (hover.hovered
        && hover.point.position.x <= capsule.width
        && Math.abs(hover.point.position.y - height / 2) <= capsule.height / 2)
    HoverHandler { id: hover }
    // 0 at rest, 1 grown; everything below follows it
    property real t: grown ? 1 : 0
    Behavior on t {
        NumberAnimation {
            duration: lights.grown ? 380 : 250
            easing.type: lights.grown ? Easing.OutBack : Easing.OutCubic
            easing.overshoot: 1.3
        }
    }
    function mix(a, b) { return a + (b - a) * t }

    width: 72 + 2 / 3; height: 28 + 2 / 3

    Rectangle {   // the capsule: Apple's dark one, see-through grey with a light rim
        id: capsule
        anchors.verticalCenter: parent.verticalCenter
        width: lights.mix(38 + 2 / 3, 72 + 2 / 3)
        height: lights.mix(20 + 2 / 3, 28 + 2 / 3)
        radius: height / 2
        color: Qt.rgba(1, 1, 1, lights.mix(.08, .10))
        border.width: 2 / 3; border.color: Qt.rgba(1, 1, 1, .14)
    }

    Repeater {
        model: [
            { dot: "#df5356", light: "#e90018", glyph: "#520000", path: "M13.7 13.7L26.3 26.3M26.3 13.7L13.7 26.3", fill: false },
            { dot: "#eebb46", light: "#f5cb00", glyph: "#5b2301", path: "M13.8 20H26.2", fill: false, stroke: 3.3 },
            { dot: "#5ecf4b", light: "#01e33e", glyph: "#0d7100", path: "M12.5 12.5H23L12.5 23ZM27.5 27.5H17L27.5 17Z", fill: true }
        ]
        Item {
            id: light
            required property var modelData
            required property int index
            readonly property real d: lights.mix(6, 16)
            readonly property real cx: lights.mix(10 + index * (9 + 1 / 3), 14 + 1 / 3 + index * 22)
            readonly property color restColor: lights.inactive ? Qt.rgba(1, 1, 1, .30) : modelData.dot
            x: cx - d / 2; y: lights.height / 2 - d / 2
            width: d; height: d
            // the light under the pointer lifts a little (iPadOS pointer effect)
            scale: lightHover.hovered && lights.grown ? (tap.pressed ? 1.02 : 1.08) : 1
            Behavior on scale { NumberAnimation { duration: 300; easing.type: Easing.OutBack } }
            HoverHandler { id: lightHover }

            Shape {   // coloured drop glow, wider than the light and a bit lower (Firefox: 0 2px 7px)
                readonly property real rr: light.d / 2 + 7
                x: light.d / 2 - rr; y: light.d / 2 - rr + 2
                width: 2 * rr; height: 2 * rr
                // only the light under the pointer glows (iPadOS pointer effect)
                opacity: lightHover.hovered ? Math.min(1, Math.max(0, lights.t)) : 0
                Behavior on opacity { NumberAnimation { duration: 180 } }
                ShapePath {
                    strokeColor: "transparent"
                    fillGradient: RadialGradient {
                        centerX: light.d / 2 + 7; centerY: centerX; centerRadius: centerX
                        focalX: centerX; focalY: centerY
                        GradientStop { position: 0; color: Qt.alpha(light.modelData.light, .5) }
                        GradientStop { position: (light.d / 2) / (light.d / 2 + 7); color: Qt.alpha(light.modelData.light, .35) }
                        GradientStop { position: 1; color: Qt.alpha(light.modelData.light, 0) }
                    }
                    PathAngleArc {
                        centerX: light.d / 2 + 7; centerY: centerX; radiusX: centerX; radiusY: centerX
                        startAngle: 0; sweepAngle: 360
                    }
                }
            }
            Rectangle {
                anchors.fill: parent; radius: width / 2
                color: Qt.tint(light.restColor, Qt.rgba(Qt.color(light.modelData.light).r,
                                                         Qt.color(light.modelData.light).g,
                                                         Qt.color(light.modelData.light).b,
                                                         Math.min(1, Math.max(0, lights.t))))
                Rectangle {   // pressed
                    anchors.fill: parent; radius: width / 2
                    color: "black"; opacity: tap.pressed ? .18 : 0
                }
            }
            Shape {   // the glyph, in a 40-unit box over the light, growing out of a point
                width: 40; height: 40
                anchors.centerIn: parent
                scale: light.d / 40 * Math.max(0, lights.t)
                opacity: Math.min(1, Math.max(0, lights.t * 2 - .4))
                preferredRendererType: Shape.CurveRenderer
                ShapePath {
                    strokeColor: light.modelData.glyph
                    strokeWidth: light.modelData.fill ? 1.6 : (light.modelData.stroke || 4)
                    capStyle: ShapePath.RoundCap; joinStyle: ShapePath.RoundJoin
                    fillColor: light.modelData.fill ? light.modelData.glyph : "transparent"
                    PathSvg { path: light.modelData.path }
                }
            }
            TapHandler {
                id: tap
                onTapped: {
                    if (light.index === 0) lights.window.close()
                    else if (light.index === 1) lights.window.showMinimized()
                    else lights.window.visibility === Window.Maximized ? lights.window.showNormal() : lights.window.showMaximized()
                }
            }
        }
    }
}
