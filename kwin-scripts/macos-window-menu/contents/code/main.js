// macOS-style "Move & Resize" / "Arrange" entries for the KWin window menu
// (right-click the title bar or Alt+F3).

function activate(window) {
    workspace.activeWindow = window;
}

// Most recently used normal window on the same screen, other than `window`.
function previousWindow(window) {
    var order = workspace.stackingOrder;
    for (var i = order.length - 1; i >= 0; --i) {
        var w = order[i];
        if (w !== window && w.normalWindow && !w.minimized && w.output === window.output) {
            return w;
        }
    }
    return null;
}

function tile(window, slot) {
    activate(window);
    if (window.fullScreen) {
        window.fullScreen = false;
    }
    workspace[slot]();
}

function center(window) {
    activate(window);
    var area = workspace.clientArea(KWin.MaximizeArea, window);
    var g = window.frameGeometry;
    window.setMaximize(false, false);
    window.frameGeometry = {
        x: Math.round(area.x + (area.width - g.width) / 2),
        y: Math.round(area.y + (area.height - g.height) / 2),
        width: g.width,
        height: g.height
    };
}

function arrange(window, firstSlot, secondSlot) {
    var other = previousWindow(window);
    tile(window, firstSlot);
    if (other) {
        tile(other, secondSlot);
        activate(window);
    }
}

registerUserActionsMenu(function (window) {
    if (!window.normalWindow) {
        return null;
    }
    return {
        text: "Move && Resize",
        items: [
            { text: "Fill", triggered: function () { tile(window, "slotWindowMaximize"); } },
            { text: "Center", triggered: function () { center(window); } },
            { text: "Left", triggered: function () { tile(window, "slotWindowQuickTileLeft"); } },
            { text: "Right", triggered: function () { tile(window, "slotWindowQuickTileRight"); } },
            { text: "Top", triggered: function () { tile(window, "slotWindowQuickTileTop"); } },
            { text: "Bottom", triggered: function () { tile(window, "slotWindowQuickTileBottom"); } },
            { text: "Top Left", triggered: function () { tile(window, "slotWindowQuickTileTopLeft"); } },
            { text: "Top Right", triggered: function () { tile(window, "slotWindowQuickTileTopRight"); } },
            { text: "Bottom Left", triggered: function () { tile(window, "slotWindowQuickTileBottomLeft"); } },
            { text: "Bottom Right", triggered: function () { tile(window, "slotWindowQuickTileBottomRight"); } },
            { text: "Arrange: Left && Right", triggered: function () { arrange(window, "slotWindowQuickTileLeft", "slotWindowQuickTileRight"); } },
            { text: "Arrange: Right && Left", triggered: function () { arrange(window, "slotWindowQuickTileRight", "slotWindowQuickTileLeft"); } },
            { text: "Arrange: Top && Bottom", triggered: function () { arrange(window, "slotWindowQuickTileTop", "slotWindowQuickTileBottom"); } },
            { text: "Full Screen", checkable: true, checked: window.fullScreen, triggered: function () { window.fullScreen = !window.fullScreen; } }
        ]
    };
});
