import QtQuick

// SGI thumbwheel: a ridged cylinder seen edge-on. Drag sideways or scroll to roll it.
Item {
    id: wheel
    property real value: .35          // 0..1
    property real u: 1
    readonly property int ridges: 44
    readonly property real step: Math.PI / ridges
    readonly property real phase: value * 9 * Math.PI

    function nudge(d) { value = Math.max(0, Math.min(1, value + d)) }

    Rectangle { anchors.fill: parent; radius: height / 2; color: Qt.rgba(11 / 255, 12 / 255, 16 / 255, .6) }
    Rectangle {
        id: drum
        anchors.fill: parent; anchors.margins: 3 * wheel.u
        radius: height / 2
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0; color: "#20222a" }
            GradientStop { position: .5; color: "#8a8f9c" }
            GradientStop { position: 1; color: "#20222a" }
        }
        clip: true
        Repeater {
            model: wheel.ridges
            Rectangle {
                required property int index
                readonly property real a: -Math.PI / 2 + ((index * wheel.step + wheel.phase) % Math.PI + Math.PI) % Math.PI
                x: drum.width / 2 + (drum.width / 2 - 4 * wheel.u) * Math.sin(a) - width / 2
                y: 3 * wheel.u
                width: 1.4 * wheel.u; height: drum.height - 6 * wheel.u
                color: "#0d0e12"
                opacity: .15 + .55 * Math.cos(a)
            }
        }
    }
    property real startValue: 0
    DragHandler {
        target: null
        yAxis.enabled: false
        onActiveChanged: if (active) wheel.startValue = wheel.value
        onTranslationChanged: wheel.value = Math.max(0, Math.min(1, wheel.startValue + translation.x / (wheel.width * 1.6)))
    }
    // Touchpad swipes (either direction) and mouse wheels roll it. A WheelHandler only sees one
    // orientation, so there is one per axis. About 400 px of swipe covers the whole range.
    function roll(event, pixel, angle) { nudge(pixel !== 0 ? pixel / 400 : angle / 120 * .06) }
    WheelHandler {
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        orientation: Qt.Vertical
        onWheel: event => wheel.roll(event, event.pixelDelta.y, event.angleDelta.y)
    }
    WheelHandler {
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        orientation: Qt.Horizontal
        onWheel: event => wheel.roll(event, event.pixelDelta.x, event.angleDelta.x)
    }
    HoverHandler { cursorShape: Qt.SizeHorCursor }
}
