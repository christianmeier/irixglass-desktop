// Split view (F3): a second side with its own folder; F5 / F6 copy / move the active side's
// selection to the other side; drags between the sides move on the same disk (Ctrl copies);
// dropping onto a folder in the split list puts the items in it. Off-screen, `make test`.
#include "fm.h"

#include <QApplication>
#include <QDir>
#include <QDropEvent>
#include <QFile>
#include <QMimeData>
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

void touch(const QString &path)
{
    QFile f(path);
    if (f.open(QIODevice::WriteOnly))
        f.write("x");
}
} // namespace

int main(int argc, char *argv[])
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    qputenv("IRIXFM_NO_DIALOGS", "1");
    QApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("irixfm-test"));
    QQuickStyle::setStyle(QStringLiteral("org.kde.desktop"));

    QTemporaryDir tmp;
    const QString base = QDir(tmp.path()).canonicalPath();
    auto at = [&](const QString &n) { return base + QLatin1Char('/') + n; };
    QDir(base).mkpath(QStringLiteral("Right/Inner"));
    for (const char *f : {"a.txt", "c.txt", "e.txt", "g.txt"})
        touch(at(QString::fromLatin1(f)));
    touch(at(QStringLiteral("Right/d.txt")));

    const QString assets = QStringLiteral(IRIXFM_SOURCE_DIR);
    Fm fm(base, assets);
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("Fm"), &fm);
    engine.load(QUrl::fromLocalFile(assets + QStringLiteral("/qml/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return 2;
    QObject *win = engine.rootObjects().constFirst();
    auto *window = qobject_cast<QQuickWindow *>(win);
    auto *pane = win->findChild<QQuickItem *>(QStringLiteral("splitPane"));
    auto *grid = win->findChild<QQuickItem *>(QStringLiteral("grid"));
    auto key = [&](Qt::Key k, Qt::KeyboardModifiers m = Qt::NoModifier) { QTest::keyClick(window, k, m); QTest::qWait(250); };
    auto exists = [&](const QString &n) { return QFile::exists(at(n)); };

    auto dropOn = [&](QQuickItem *item, QPointF local, const QStringList &files) {
        const QPoint p = item->mapToScene(local).toPoint();
        QMimeData mime;
        QList<QUrl> urls;
        for (const QString &f : files)
            urls << QUrl::fromLocalFile(f);
        mime.setUrls(urls);
        QDragEnterEvent enter(p, Qt::CopyAction | Qt::MoveAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(window, &enter);
        QDragMoveEvent move(p, Qt::CopyAction | Qt::MoveAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(window, &move);
        QDropEvent drop(p, Qt::CopyAction | Qt::MoveAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(window, &drop);
        QTest::qWait(150);
    };

    QTimer::singleShot(1200, &app, [&] {
        window->requestActivate();
        QTest::qWaitForWindowActive(window);
        const double gridW = grid->width();

        key(Qt::Key_F3);
        QTest::qWait(300);
        check(win->property("splitView").toBool() && pane->isVisible() && pane->property("dir").toString() == base,
              QStringLiteral("F3 opens the split view on the same folder"));
        check(grid->width() < gridW * .7, QStringLiteral("…and the main side makes room"), QString::number(grid->width()));
        QMetaObject::invokeMethod(pane, "go", Q_ARG(QVariant, at(QStringLiteral("Right"))));
        QTest::qWait(300);
        check(pane->property("dir").toString() == at(QStringLiteral("Right")), QStringLiteral("the split side navigates on its own"));

        // F5 / F6 from the main side
        win->setProperty("activePane", 0);
        win->setProperty("selection", QVariantMap{{at(QStringLiteral("a.txt")), true}});
        key(Qt::Key_F5);
        check(waitFor([&] { return exists(QStringLiteral("Right/a.txt")) && exists(QStringLiteral("a.txt")); }), QStringLiteral("F5 copies the selection to the other side"));
        win->setProperty("selection", QVariantMap{{at(QStringLiteral("c.txt")), true}});
        key(Qt::Key_F6);
        check(waitFor([&] { return exists(QStringLiteral("Right/c.txt")) && !exists(QStringLiteral("c.txt")); }), QStringLiteral("F6 moves it"));

        // F6 from the split side back
        win->setProperty("activePane", 1);
        pane->setProperty("selection", QVariantMap{{at(QStringLiteral("Right/d.txt")), true}});
        key(Qt::Key_F6);
        check(waitFor([&] { return exists(QStringLiteral("d.txt")) && !exists(QStringLiteral("Right/d.txt")); }), QStringLiteral("F6 from the split side moves back"));

        // a drag that started in fm, dropped on the split side: moves (same disk)
        win->setProperty("internalDrag", true);
        dropOn(pane, QPointF(pane->width() / 2, pane->height() - 20), {at(QStringLiteral("e.txt"))});
        check(waitFor([&] { return exists(QStringLiteral("Right/e.txt")) && !exists(QStringLiteral("e.txt")); }),
              QStringLiteral("dragging between the sides moves the item"));
        // onto a folder row in the split list: into that folder
        QQuickItem *inner = nullptr;
        auto *list = pane->findChild<QQuickItem *>(QStringLiteral("splitList"));
        waitFor([&] {
            for (QQuickItem *c : list->property("contentItem").value<QQuickItem *>()->childItems())
                if (c->property("filePath").toString() == at(QStringLiteral("Right/Inner")))
                    inner = c;
            return inner != nullptr;
        });
        if (inner)
            dropOn(inner, QPointF(inner->width() / 2, inner->height() / 2), {at(QStringLiteral("g.txt"))});
        check(inner && waitFor([&] { return exists(QStringLiteral("Right/Inner/g.txt")) && !exists(QStringLiteral("g.txt")); }),
              QStringLiteral("dropping onto a folder in the split list puts it inside"));
        win->setProperty("internalDrag", false);

        // a drag from another app keeps its own action (copy here)
        touch(at(QStringLiteral("ext.txt")));
        dropOn(pane, QPointF(pane->width() / 2, pane->height() - 20), {at(QStringLiteral("ext.txt"))});
        check(waitFor([&] { return exists(QStringLiteral("Right/ext.txt")); }) && exists(QStringLiteral("ext.txt")),
              QStringLiteral("a drag from another app copies"));

        // the split side has its own tabs, independent of the main side's
        {
            auto paneTabs = [&] { return pane->property("tabs").toList().size(); };
            auto mainTabs = [&] { return win->property("tabs").toList().size(); };
            auto *strip = win->findChild<QQuickItem *>(QStringLiteral("splitTabStrip"));
            check(strip && strip->isVisible() && paneTabs() == 1, QStringLiteral("the split side has its own tab bar"));
            win->setProperty("activePane", 1);
            key(Qt::Key_T, Qt::ControlModifier);
            check(paneTabs() == 2 && mainTabs() == 1, QStringLiteral("Ctrl+T on the split side adds a tab there only"),
                  QString::number(paneTabs()) + "/" + QString::number(mainTabs()));
            QMetaObject::invokeMethod(pane, "go", Q_ARG(QVariant, at(QStringLiteral("Right/Inner"))));
            QTest::qWait(200);
            key(Qt::Key_PageUp, Qt::ControlModifier);
            check(pane->property("tabIndex").toInt() == 0 && pane->property("dir").toString() == at(QStringLiteral("Right"))
                      && win->property("dir").toString() == base,
                  QStringLiteral("Ctrl+PgUp switches the split side's tab, not the main one"), pane->property("dir").toString());
            key(Qt::Key_PageDown, Qt::ControlModifier);
            check(pane->property("dir").toString() == at(QStringLiteral("Right/Inner")), QStringLiteral("…and each split tab keeps its folder"));
            QVariant fav = QVariantMap{{QStringLiteral("label"), QStringLiteral("base")}, {QStringLiteral("path"), base}};
            QMetaObject::invokeMethod(win, "activatePlace", Q_ARG(QVariant, fav));
            QTest::qWait(200);
            check(paneTabs() == 3 && pane->property("dir").toString() == base && mainTabs() == 1,
                  QStringLiteral("a sidebar place opens on the active (split) side"), QString::number(paneTabs()));
            key(Qt::Key_W, Qt::ControlModifier);
            key(Qt::Key_W, Qt::ControlModifier);
            check(paneTabs() == 1 && win->property("splitView").toBool(), QStringLiteral("Ctrl+W closes split tabs"));
            win->setProperty("activePane", 0);
            key(Qt::Key_T, Qt::ControlModifier);
            check(mainTabs() == 2 && paneTabs() == 1, QStringLiteral("Ctrl+T on the main side adds a main tab"));
            key(Qt::Key_W, Qt::ControlModifier);
        }

        // drag the divider: the sides resize, and the share is kept; double-click centres it
        {
            auto *divider = win->findChild<QQuickItem *>(QStringLiteral("splitDivider"));
            const double paneW = pane->width();
            const QPoint from = divider->mapToScene(QPointF(divider->width() / 2, divider->height() / 2)).toPoint();
            QTest::mousePress(window, Qt::LeftButton, {}, from);
            for (int dx = 10; dx <= 120; dx += 10)
                QTest::mouseMove(window, from + QPoint(dx, 0), 10);
            QTest::mouseRelease(window, Qt::LeftButton, {}, from + QPoint(120, 0));
            QTest::qWait(250);
            check(pane->width() < paneW - 60 && win->property("splitRatio").toDouble() > .55, QStringLiteral("dragging the divider resizes the sides"),
                  QString::number(pane->width()) + " ratio " + QString::number(win->property("splitRatio").toDouble()));
            const QPoint now = divider->mapToScene(QPointF(divider->width() / 2, divider->height() / 2)).toPoint();
            QTest::mouseDClick(window, Qt::LeftButton, {}, now);
            QTest::qWait(300);
            check(qAbs(win->property("splitRatio").toDouble() - .5) < .001, QStringLiteral("double-clicking the divider centres it"));
        }
        auto *button = win->findChild<QQuickItem *>(QStringLiteral("splitButton"));
        QTest::mouseClick(window, Qt::LeftButton, {}, button->mapToScene(QPointF(button->width() / 2, button->height() / 2)).toPoint());
        QTest::qWait(400);
        check(!win->property("splitView").toBool() && qAbs(grid->width() - gridW) < 1, QStringLiteral("the split button in the toolbar closes it again"));
        QTest::mouseClick(window, Qt::LeftButton, {}, button->mapToScene(QPointF(button->width() / 2, button->height() / 2)).toPoint());
        QTest::qWait(300);
        check(win->property("splitView").toBool(), QStringLiteral("…and opens it"));
        key(Qt::Key_F3);
        QTest::qWait(400);
        check(!win->property("splitView").toBool() && qAbs(grid->width() - gridW) < 1, QStringLiteral("F3 closes the split view again"));
        check(win->property("splitDir").toString() == at(QStringLiteral("Right")), QStringLiteral("the split side's folder is remembered"));
        app.exit(failures ? 1 : 0);
    });
    return app.exec();
}
