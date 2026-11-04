// macOS keeps new windows inside the free area: below the menu bar, above the dock. KWin doesn't
// shrink a window that restores a size bigger than that area (e.g. saved before the dock reserve
// panel existed, scripts/panels.js), so its edges end up on the screen edges or under the dock and
// can't be grabbed to resize it. This shrinks (down to the window's minimum size) and moves such
// windows inside the area when they open, and once for all open windows when the script loads.
// Maximized and full-screen windows are left alone; KWin already fits them.

function fit(w) {
    if (!w || !w.normalWindow || w.fullScreen || !w.resizeable || w.maximizeMode !== 0)
        return;
    var a = workspace.clientArea(KWin.PlacementArea, w);
    var g = w.frameGeometry;
    if (g.x >= a.x && g.y >= a.y && g.x + g.width <= a.x + a.width && g.y + g.height <= a.y + a.height)
        return;
    // frame minus client: the decoration's share of the minimum size
    var dw = g.width - w.clientGeometry.width, dh = g.height - w.clientGeometry.height;
    var width = Math.max(Math.min(g.width, a.width), w.minSize.width + dw);
    var height = Math.max(Math.min(g.height, a.height), w.minSize.height + dh);
    var x = Math.min(Math.max(g.x, a.x), a.x + a.width - width);
    var y = Math.min(Math.max(g.y, a.y), a.y + a.height - height);
    w.frameGeometry = { x: Math.max(x, a.x), y: Math.max(y, a.y), width: width, height: height };
}

workspace.windowAdded.connect(function (w) {
    // let the app restore its size first
    var t = new QTimer();
    t.singleShot = true;
    t.interval = 300;
    t.timeout.connect(function () { fit(w); });
    t.start();
});
workspace.windowList().forEach(fit);
