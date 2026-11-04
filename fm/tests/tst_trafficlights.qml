// The window controls (qml/TrafficLights.qml), off-screen with QML's test runner against a stand-in
// window: hover grows the group, every light can be clicked, and the group shrinks back.
// Once the lights took the capsule's hover away, so the group shrank under the pointer and only
// the close button could be hit. Run by `make test` (qmltestrunner -input tests/tst_trafficlights.qml).
import QtQuick
import QtTest
import "../qml" as Fm

Item {
    id: root
    width: 200; height: 80
    QtObject {   // stand-in for the window
        id: fakeWin
        property bool active: true
        property int visibility: 2
        property int closed: 0
        property int minimized: 0
        property int maximized: 0
        property int restored: 0
        function close() { closed++ }
        function showMinimized() { minimized++ }
        function showMaximized() { maximized++ }
        function showNormal() { restored++ }
    }
    Fm.TrafficLights { id: lights; x: 16; y: 10; window: fakeWin }

    TestCase {
        name: "trafficlights"; when: windowShown
        readonly property real mid: 10 + lights.height / 2
        function away() { mouseMove(root, 199, 79); tryCompare(lights, "grown", false, 1000); wait(400) }
        function grow() { mouseMove(root, 16 + 5, mid); tryCompare(lights, "grown", true, 1000); wait(500) }

        function test_click_data() {
            return [{ tag: "close", i: 0, f: "closed" }, { tag: "minimize", i: 1, f: "minimized" },
                    { tag: "maximize", i: 2, f: "maximized" }]
        }
        function test_click(d) {
            fakeWin.closed = 0; fakeWin.minimized = 0; fakeWin.maximized = 0
            away(); grow()
            var x = 16 + 14 + 1 / 3 + d.i * 22   // the grown light's centre
            mouseMove(root, x, mid); wait(300)
            verify(lights.grown, "still grown over the " + d.tag + " light")
            mouseClick(root, x, mid); wait(100)
            compare(fakeWin[d.f], 1, d.tag + " clicked")
        }
        function test_rest_area_only() {
            away()
            mouseMove(root, 16 + 60, mid); wait(300)   // right of the resting capsule (38.67 px wide)
            verify(!lights.grown, "the empty area right of the resting capsule doesn't grow it")
        }
        function test_maximized_restores() {
            fakeWin.visibility = Window.Maximized; fakeWin.restored = 0; fakeWin.maximized = 0
            away(); grow()
            var x = 16 + 14 + 1 / 3 + 2 * 22
            mouseMove(root, x, mid); wait(300)
            mouseClick(root, x, mid); wait(100)
            compare(fakeWin.restored, 1, "green on a maximized window restores it")
            compare(fakeWin.maximized, 0)
            fakeWin.visibility = 2
        }
        function test_shrinks_back() {
            grow(); away()
            verify(!lights.grown, "shrinks when the pointer leaves")
        }
    }
}
