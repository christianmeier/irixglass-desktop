// Tabs (IRIX notebook tabs with Finder behaviour) and the thumbwheel's IRIX home button:
// new / close / switch / reopen / move, per-tab folder, history, selection, scroll and search,
// middle-click and Ctrl+double-click, titles, spring-loading, hiding the bar, and restoring the
// session. Off-screen, run with `make test`.
#include "fm.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <cstdio>
#include <functional>
#include <memory>

namespace
{
int failures = 0;

void check(bool ok, const QString &what, const QString &got = {})
{
    std::printf("%s %-58s %s\n", ok ? "PASS" : "FAIL", qPrintable(what), ok ? "" : qPrintable("got: " + got));
    failures += ok ? 0 : 1;
}

bool waitFor(const std::function<bool()> &cond, int ms = 4000)
{
    for (int t = 0; t < ms && !cond(); t += 50)
        QTest::qWait(50);
    return cond();
}

QQuickItem *findNamed(QQuickItem *root, const QString &name)
{
    for (QQuickItem *c : root->childItems()) {
        if (c->objectName() == name)
            return c;
        if (QQuickItem *r = findNamed(c, name))
            return r;
    }
    return nullptr;
}
} // namespace

int main(int argc, char *argv[])
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    QApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("irixfm-test"));
    QQuickStyle::setStyle(QStringLiteral("org.kde.desktop"));

    QTemporaryDir tmp;
    const QString base = QDir(tmp.path()).canonicalPath();
    auto at = [&](const QString &n) { return base + QLatin1Char('/') + n; };
    for (const char *d : {"A", "B", "C", "Many"})
        QDir(base).mkdir(QString::fromLatin1(d));
    for (int i = 0; i < 160; ++i) {
        QFile f(at(QStringLiteral("Many/file%1.txt").arg(i, 3, 10, QLatin1Char('0'))));
        if (f.open(QIODevice::WriteOnly))
            f.write("x");
    }
    QFile a(at(QStringLiteral("a.txt")));
    if (a.open(QIODevice::WriteOnly))
        a.write("a");
    a.close();

    const QString assets = QStringLiteral(IRIXFM_SOURCE_DIR);
    auto fm = std::make_unique<Fm>(base, assets, nullptr, true);   // a session window (saves its tabs)
    auto engine = std::make_unique<QQmlApplicationEngine>();
    engine->rootContext()->setContextProperty(QStringLiteral("Fm"), fm.get());
    engine->load(QUrl::fromLocalFile(assets + QStringLiteral("/qml/Main.qml")));
    if (engine->rootObjects().isEmpty())
        return 2;
    QObject *win = engine->rootObjects().constFirst();
    auto *window = qobject_cast<QQuickWindow *>(win);
    auto *grid = win->findChild<QQuickItem *>(QStringLiteral("grid"));
    auto *search = win->findChild<QQuickItem *>(QStringLiteral("search"));
    auto *strip = win->findChild<QQuickItem *>(QStringLiteral("tabStrip"));

    auto tabs = [&] { return win->property("tabs").toList().size(); };
    auto idx = [&] { return win->property("tabIndex").toInt(); };
    auto dir = [&] { return win->property("dir").toString(); };
    auto key = [&](Qt::Key k, Qt::KeyboardModifiers m = Qt::NoModifier) { QTest::keyClick(window, k, m); QTest::qWait(150); };
    auto call = [&](const char *fn, QVariantList args) {
        switch (args.size()) {
        case 1: QMetaObject::invokeMethod(win, fn, Q_ARG(QVariant, args[0])); break;
        case 2: QMetaObject::invokeMethod(win, fn, Q_ARG(QVariant, args[0]), Q_ARG(QVariant, args[1])); break;
        default: QMetaObject::invokeMethod(win, fn); break;
        }
        QTest::qWait(200);
    };
    auto title = [&](int i) { QVariant v; QMetaObject::invokeMethod(win, "tabTitle", Q_RETURN_ARG(QVariant, v), Q_ARG(QVariant, i)); return v.toString(); };
    auto cellFor = [&](const QString &path) -> QQuickItem * {
        for (QQuickItem *c : grid->property("contentItem").value<QQuickItem *>()->childItems())
            if (c->property("filePath").toString() == path)
                return c;
        return nullptr;
    };

    QTimer::singleShot(1200, &app, [&] {
        window->requestActivate();
        QTest::qWaitForWindowActive(window);

        check(tabs() == 1 && strip && strip->isVisible(), QStringLiteral("one tab at start, notebook tab bar shown"));
        key(Qt::Key_T, Qt::ControlModifier);
        check(tabs() == 2 && idx() == 1 && dir() == base, QStringLiteral("Ctrl+T opens the current folder in a new tab"));
        call("go", {at(QStringLiteral("A")), false});
        key(Qt::Key_PageUp, Qt::ControlModifier);
        check(idx() == 0 && dir() == base, QStringLiteral("Ctrl+PgUp goes back to the first tab and its folder"), dir());
        check(win->property("backStack").toList().isEmpty(), QStringLiteral("each tab has its own history"));
        key(Qt::Key_PageDown, Qt::ControlModifier);
        check(idx() == 1 && dir() == at(QStringLiteral("A")), QStringLiteral("Ctrl+PgDown: the second tab kept its folder"), dir());
        check(win->property("backStack").toList().size() == 1, QStringLiteral("…and its own back history"));

        // selection survives a switch
        key(Qt::Key_PageUp, Qt::ControlModifier);
        win->setProperty("selection", QVariantMap{{at(QStringLiteral("a.txt")), true}});
        key(Qt::Key_PageDown, Qt::ControlModifier);
        key(Qt::Key_PageUp, Qt::ControlModifier);
        check(waitFor([&] { return win->property("selection").toMap().contains(at(QStringLiteral("a.txt"))); }), QStringLiteral("a tab remembers its selection"));

        // scroll position survives a switch
        call("go", {at(QStringLiteral("Many")), false});
        waitFor([&] { return grid->property("count").toInt() == 160; });
        QTest::qWait(200);
        grid->setProperty("contentY", 400.0);
        key(Qt::Key_PageDown, Qt::ControlModifier);
        key(Qt::Key_PageUp, Qt::ControlModifier);
        check(waitFor([&] { return qAbs(grid->property("contentY").toDouble() - 400) < 2; }), QStringLiteral("a tab remembers its scroll position"),
              QString::number(grid->property("contentY").toDouble()));

        // search is per tab
        key(Qt::Key_PageDown, Qt::ControlModifier);
        search->setProperty("text", QStringLiteral("zzz"));
        key(Qt::Key_PageUp, Qt::ControlModifier);
        check(search->property("text").toString().isEmpty(), QStringLiteral("another tab has its own (empty) search"));
        key(Qt::Key_PageDown, Qt::ControlModifier);
        check(search->property("text").toString() == QStringLiteral("zzz") && title(1) == QStringLiteral("Search “zzz”"),
              QStringLiteral("…and the search comes back, with a matching tab title"), title(1));
        search->setProperty("text", QString());

        // Alt+digit, Ctrl+digit unchanged
        call("newTab", {at(QStringLiteral("B")), false});
        check(tabs() == 3 && idx() == 1, QStringLiteral("a background tab opens without switching"));
        key(Qt::Key_9, Qt::AltModifier);
        check(idx() == 2 && dir() == at(QStringLiteral("B")), QStringLiteral("Alt+9 goes to the last tab"), dir());
        key(Qt::Key_1, Qt::AltModifier);
        check(idx() == 0, QStringLiteral("Alt+1 goes to the first tab"));
        key(Qt::Key_2, Qt::ControlModifier);
        check(idx() == 0 && win->property("viewMode").toInt() == 1, QStringLiteral("Ctrl+2 still switches the view, not the tab"));
        key(Qt::Key_1, Qt::ControlModifier);

        // middle-click and Ctrl+double-click on a folder
        call("go", {base, false});
        waitFor([&] { return cellFor(at(QStringLiteral("C"))) != nullptr; });
        QTest::qWait(200);
        QQuickItem *c = cellFor(at(QStringLiteral("C")));
        const QPoint cp = c ? c->mapToScene(QPointF(c->width() / 2, c->height() * .35)).toPoint() : QPoint();
        QTest::mouseClick(window, Qt::MiddleButton, {}, cp);
        QTest::qWait(200);
        check(tabs() == 4 && idx() == 0 && win->property("tabs").toList().last().toMap().value(QStringLiteral("dir")) == at(QStringLiteral("C")),
              QStringLiteral("middle-click on a folder opens it in a background tab"));
        // Opening a folder gives it a tab: here C already has one (the middle-click), so fm switches to it.
        QTest::mouseDClick(window, Qt::LeftButton, Qt::NoModifier, cp);
        QTest::qWait(300);
        check(tabs() == 4 && idx() == 3 && dir() == at(QStringLiteral("C")), QStringLiteral("opening a folder that has a tab switches to that tab"), dir());
        key(Qt::Key_1, Qt::AltModifier);
        call("closeTab", {3});
        QTest::mouseDClick(window, Qt::LeftButton, Qt::NoModifier, cp);
        QTest::qWait(300);
        check(tabs() == 4 && idx() == 3 && dir() == at(QStringLiteral("C")), QStringLiteral("opening a folder adds a tab for it"), dir());
        key(Qt::Key_1, Qt::AltModifier);
        QVariant fav = QVariantMap{{QStringLiteral("label"), QStringLiteral("B")}, {QStringLiteral("path"), at(QStringLiteral("B"))}};
        QMetaObject::invokeMethod(win, "activatePlace", Q_ARG(QVariant, fav));
        QTest::qWait(200);
        check(dir() == at(QStringLiteral("B")) && idx() == 2, QStringLiteral("a sidebar favourite switches to its existing tab"), QString::number(idx()));
        key(Qt::Key_1, Qt::AltModifier);
        const int before = tabs();
        QTest::mouseDClick(window, Qt::LeftButton, Qt::ControlModifier, cp);
        QTest::qWait(300);
        check(tabs() == before && idx() == 0 && dir() == at(QStringLiteral("C")), QStringLiteral("Ctrl+double-click opens it in the same tab"), dir());
        call("go", {base, false});
        call("openInNewTab", {QVariantList{at(QStringLiteral("C"))}, true});
        check(tabs() == 5 && idx() == 4 && dir() == at(QStringLiteral("C")), QStringLiteral("Open in New Tab always adds one"), dir());

        // close, reopen, close others, move
        key(Qt::Key_W, Qt::ControlModifier);
        check(tabs() == 4 && window->isVisible(), QStringLiteral("Ctrl+W closes the tab, not the window"));
        key(Qt::Key_T, Qt::ControlModifier | Qt::ShiftModifier);
        check(tabs() == 5 && dir() == at(QStringLiteral("C")), QStringLiteral("Ctrl+Shift+T reopens the closed tab"), dir());
        call("moveTab", {4, 0});
        check(idx() == 0 && win->property("tabs").toList().first().toMap().value(QStringLiteral("dir")) == at(QStringLiteral("C")),
              QStringLiteral("a tab can be moved (and stays active)"));
        call("closeOtherTabs", {0});
        check(tabs() == 1 && dir() == at(QStringLiteral("C")), QStringLiteral("Close Other Tabs"));

        // titles
        call("newTab", {fm->trashDir(), false});
        check(title(1) == QStringLiteral("Trash"), QStringLiteral("the Trash tab is titled \"Trash\""), title(1));
        search->setProperty("text", QStringLiteral("tag:red"));
        check(title(0) == QStringLiteral("Red"), QStringLiteral("a tag view is titled with the tag"), title(0));
        search->setProperty("text", QString());

        // spring-loaded: a drag hovering a tab switches to it (its timer)
        QQuickItem *tab1 = findNamed(strip, QStringLiteral("tab1"));
        QObject *spring = tab1 ? tab1->findChild<QObject *>(QStringLiteral("tabSpring")) : nullptr;
        if (spring)
            QMetaObject::invokeMethod(spring, "triggered");
        QTest::qWait(200);
        check(spring && idx() == 1, QStringLiteral("hovering a drag over a tab switches to it"));
        key(Qt::Key_W, Qt::ControlModifier);

        // hiding the tab bar gives the files the room; the status bar stays put
        const double gh = grid->height();
        win->setProperty("tabBarShown", false);
        QTest::qWait(100);
        check(grid->height() > gh + 10 && !strip->isVisible(), QStringLiteral("hiding the tab bar gives the room to the files"));
        win->setProperty("tabBarShown", true);

        // home button (after the wheel has been rolled by hand, which used to detach it)
        auto *wheel = win->findChild<QQuickItem *>(QStringLiteral("thumbwheel"));
        if (wheel) QMetaObject::invokeMethod(wheel, "nudge", Q_ARG(QVariant, 0.3));
        QTest::qWait(50);
        QQuickItem *home = win->findChild<QQuickItem *>(QStringLiteral("homeButton"));
        QTest::mouseClick(window, Qt::LeftButton, {}, home->mapToScene(QPointF(home->width() / 2, home->height() / 2)).toPoint());
        QTest::qWait(100);
        check(wheel && qAbs(win->property("wheelValue").toDouble() - win->property("defaultWheel").toDouble()) < .001
                  && qAbs(wheel->property("value").toDouble() - win->property("defaultWheel").toDouble()) < .001,
              QStringLiteral("the IRIX home button resets the icon size and the wheel"));
        // a search tab's selection survives switching back and forth (no delayed re-search wiping it)
        win->setProperty("selection", QVariantMap{{at(QStringLiteral("a.txt")), true}});
        call("newTab", {base, true});
        search->setProperty("text", QStringLiteral("zz"));
        QTest::qWait(400);
        key(Qt::Key_PageUp, Qt::ControlModifier);
        QTest::qWait(500);
        check(win->property("selection").toMap().contains(at(QStringLiteral("a.txt"))), QStringLiteral("coming back from a search tab keeps the selection"));
        key(Qt::Key_PageDown, Qt::ControlModifier);
        key(Qt::Key_W, Qt::ControlModifier);

        // session restore: a new window started without a folder gets the tabs back
        call("newTab", {at(QStringLiteral("A")), false});
        call("newTab", {at(QStringLiteral("B")), true});
        engine.reset();
        auto fm2 = std::make_unique<Fm>(base, assets, nullptr, true);
        QQmlApplicationEngine engine2;
        engine2.rootContext()->setContextProperty(QStringLiteral("Fm"), fm2.get());
        engine2.load(QUrl::fromLocalFile(assets + QStringLiteral("/qml/Main.qml")));
        QObject *win2 = engine2.rootObjects().value(0);
        QTest::qWait(500);
        QStringList dirs;
        for (const QVariant &t : win2 ? win2->property("tabs").toList() : QVariantList())
            dirs << t.toMap().value(QStringLiteral("dir")).toString();
        check(dirs == QStringList{at(QStringLiteral("C")), at(QStringLiteral("A")), at(QStringLiteral("B"))} && win2->property("tabIndex").toInt() == 2
                  && win2->property("dir").toString() == at(QStringLiteral("B")),
              QStringLiteral("tabs and the active tab come back after a restart"), dirs.join(", "));
        app.exit(failures ? 1 : 0);
    });
    return app.exec();
}
