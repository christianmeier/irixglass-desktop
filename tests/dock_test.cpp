// The floating dock: fm reads its height from Plasma's settings (live) and, when maximized,
// keeps that much free at the bottom so tabs and the status bar aren't hidden under it.
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
void writePanels(const QString &cfg, int thickness)
{
    // Rewritten like KConfig does: a new file replaces the old one.
    QFile a(cfg + QStringLiteral("/plasma-org.kde.plasma.desktop-appletsrc.new"));
    if (a.open(QIODevice::WriteOnly))
        a.write("[Containments][7]\nlocation=4\nplugin=org.kde.panel\n\n[Containments][9]\nlocation=3\nplugin=org.kde.panel\n");
    a.close();
    QFile::remove(cfg + QStringLiteral("/plasma-org.kde.plasma.desktop-appletsrc"));
    QFile::rename(a.fileName(), cfg + QStringLiteral("/plasma-org.kde.plasma.desktop-appletsrc"));
    QFile s(cfg + QStringLiteral("/plasmashellrc.new"));
    if (s.open(QIODevice::WriteOnly))
        s.write(QStringLiteral("[PlasmaViews][Panel 7]\nfloating=1\npanelVisibility=3\n\n[PlasmaViews][Panel 7][Defaults]\nthickness=%1\n\n"
                               "[PlasmaViews][Panel 9]\nfloating=0\n\n[PlasmaViews][Panel 9][Defaults]\nthickness=24\n").arg(thickness).toUtf8());
    s.close();
    QFile::remove(cfg + QStringLiteral("/plasmashellrc"));
    QFile::rename(s.fileName(), cfg + QStringLiteral("/plasmashellrc"));
}
} // namespace

int main(int argc, char *argv[])
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    const QString cfg = QString::fromLocal8Bit(qgetenv("XDG_CONFIG_HOME"));
    if (cfg.isEmpty())
        return 3;   // run through `make test` (private XDG_CONFIG_HOME)
    QDir().mkpath(cfg);
    writePanels(cfg, 44);
    QApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("irixfm-test"));
    QQuickStyle::setStyle(QStringLiteral("org.kde.desktop"));
    QTemporaryDir tmp;
    const QString assets = QStringLiteral(IRIXFM_SOURCE_DIR);
    Fm fm(tmp.path(), assets);
    check(fm.dockReserve() == 44 + 16, QStringLiteral("reads the floating dock's height from Plasma"), QString::number(fm.dockReserve()));

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("Fm"), &fm);
    engine.load(QUrl::fromLocalFile(assets + QStringLiteral("/qml/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return 2;
    QObject *win = engine.rootObjects().constFirst();
    auto *window = qobject_cast<QQuickWindow *>(win);
    auto *strip = win->findChild<QQuickItem *>(QStringLiteral("tabStrip"));
    auto *status = win->findChild<QQuickItem *>(QStringLiteral("status"));

    QTimer::singleShot(1000, &app, [&] {
        const double normalStripY = window->height() - strip->y();
        check(win->property("dockInset").toDouble() == 0, QStringLiteral("a normal window keeps its layout"));
        window->showMaximized();
        waitFor([&] { return win->property("maximized").toBool(); });
        QTest::qWait(200);
        const double bottom = window->height();
        check(win->property("dockInset").toDouble() == 60 && bottom - strip->y() >= normalStripY + 59,
              QStringLiteral("maximized: the tabs move up above the dock"), QString::number(bottom - strip->y()));
        check(status->y() + status->height() <= bottom - 60, QStringLiteral("…and the status bar too"),
              QString::number(status->y() + status->height()) + " vs " + QString::number(bottom - 60));
        writePanels(cfg, 64);
        check(waitFor([&] { return fm.dockReserve() == 80; }), QStringLiteral("the dock grows: fm notices"), QString::number(fm.dockReserve()));
        QTest::qWait(100);
        check(strip->y() + strip->height() <= window->height() - 80, QStringLiteral("…and the tabs move up with it"));
        window->showNormal();
        waitFor([&] { return !win->property("maximized").toBool(); });
        check(win->property("dockInset").toDouble() == 0, QStringLiteral("back to a normal window: no gap"));
        app.exit(failures ? 1 : 0);
    });
    return app.exec();
}
