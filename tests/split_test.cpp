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

        // the keyboard works on the active side: arrows, Space = Quick Look of its item
        {
            win->setProperty("activePane", 1);
            pane->setProperty("selection", QVariantMap{});
            pane->setProperty("anchorIndex", -1);
            key(Qt::Key_Down);
            const QString first = pane->property("selection").toMap().keys().value(0);
            check(!first.isEmpty() && first.startsWith(at(QStringLiteral("Right"))), QStringLiteral("arrow keys move in the active split side"), first);
            key(Qt::Key_Space);
            check(win->property("quickLook").toString() == first, QStringLiteral("Space opens Quick Look of the split side's item"),
                  win->property("quickLook").toString());
            key(Qt::Key_Down);
            check(win->property("quickLook").toString() != first && !win->property("quickLook").toString().isEmpty(),
                  QStringLiteral("…and follows the arrow keys there"));
            key(Qt::Key_Escape);
            check(win->property("quickLook").toString().isEmpty(), QStringLiteral("Esc closes it"));
            win->setProperty("previewShown", true);
            QTest::qWait(300);
            auto *preview = win->findChild<QQuickItem *>(QStringLiteral("previewPane"));
            const QString sel = pane->property("selection").toMap().keys().value(0);
            check(preview && preview->property("path").toString() == sel && !sel.isEmpty(),
                  QStringLiteral("the preview pane shows the split side's item while it's active"), preview ? preview->property("path").toString() : QString());
            win->setProperty("previewShown", false);
            win->setProperty("activePane", 0);
        }

        // each side has its own view: the buttons / Ctrl+1/2/3 act on the active side
        {
            const int mainView = win->property("viewMode").toInt();
            win->setProperty("activePane", 1);
            key(Qt::Key_1, Qt::ControlModifier);
            auto *sgrid = pane->findChild<QQuickItem *>(QStringLiteral("splitGrid"));
            check(win->property("splitViewMode").toInt() == 0 && win->property("viewMode").toInt() == mainView && sgrid && sgrid->isVisible()
                      && sgrid->property("count").toInt() > 0,
                  QStringLiteral("Ctrl+1 on the split side shows its icons, the main side keeps its view"));
            win->setProperty("activePane", 0);
            key(Qt::Key_3, Qt::ControlModifier);
            check(win->property("viewMode").toInt() == 2 && win->property("splitViewMode").toInt() == 0,
                  QStringLiteral("Ctrl+3 on the main side changes only the main side"));
            // clicking the toolbar's view buttons: first click into the split side, then on "list"
            QTest::mouseClick(window, Qt::LeftButton, {}, pane->mapToScene(QPointF(pane->width() / 2, pane->height() - 30)).toPoint());
            QTest::qWait(100);
            check(win->property("activePane").toInt() == 1, QStringLiteral("clicking into the split side makes it active"));
            auto *viewPill = win->findChild<QQuickItem *>(QStringLiteral("viewPill"));
            QTest::mouseClick(window, Qt::LeftButton, {}, viewPill->mapToScene(QPointF((16 + 41 + 7) * .85, 18 * .85)).toPoint());
            QTest::qWait(150);
            check(win->property("splitViewMode").toInt() == 1 && win->property("viewMode").toInt() == 2,
                  QStringLiteral("…then the list button switches the split side to the list"),
                  QString::number(win->property("splitViewMode").toInt()));
            // the view buttons show the clicked side's view: list on the split side, previews on the main side
            auto *hl = win->findChild<QQuickItem *>(QStringLiteral("viewHighlight"));
            auto hlMode = [&] { return qRound((hl->x() / .85 - 4) / 41); };
            QTest::qWait(300);
            check(hlMode() == 1, QStringLiteral("the view buttons show the split side's view while it's active"), QString::number(hlMode()));
            QTest::mouseClick(window, Qt::LeftButton, {}, grid->mapToScene(QPointF(grid->width() / 2, grid->height() - 30)).toPoint());
            QTest::qWait(300);
            check(win->property("activePane").toInt() == 0 && hlMode() == 2,
                  QStringLiteral("clicking into the main side switches them to its view"), QString::number(hlMode()));
            win->setProperty("activePane", 0);
            key(Qt::Key_1, Qt::ControlModifier);
        }
        // list columns (Finder's View Options): the split side has Kind too, both sides follow the
        // chosen columns, narrow sides drop the extra ones first, the choice is saved
        {
            win->setProperty("splitViewMode", 1);
            win->setProperty("viewMode", 1);
            QTest::qWait(300);
            auto *splitHeading = pane->findChild<QQuickItem *>(QStringLiteral("splitListHeading"));
            check(splitHeading && splitHeading->property("cols").toStringList().contains(QStringLiteral("kind")),
                  QStringLiteral("the split side's list shows Kind"), splitHeading ? splitHeading->property("cols").toStringList().join(",") : QStringLiteral("no heading"));
            for (const char *k : {"created", "permissions", "owner"})
                QMetaObject::invokeMethod(win, "toggleColumn", Q_ARG(QVariant, QString::fromLatin1(k)));
            QTest::qWait(200);
            const QStringList chosen = win->property("listColumns").toStringList();
            check(chosen.contains(QStringLiteral("created")) && chosen.contains(QStringLiteral("owner")) && win->property("listColumnsJson").toString().contains(QStringLiteral("permissions")),
                  QStringLiteral("columns can be added (and are saved)"), chosen.join(","));
            const QStringList narrow = splitHeading->property("cols").toStringList();
            check(narrow.contains(QStringLiteral("kind")) && narrow.contains(QStringLiteral("size")) && !narrow.contains(QStringLiteral("owner")),
                  QStringLiteral("a narrow side drops the extra columns first, keeps Size and Kind"), narrow.join(","));
            const QVariant wide = [&] { QVariant v; QMetaObject::invokeMethod(win, "columnsFor", Q_RETURN_ARG(QVariant, v), Q_ARG(QVariant, 2000.0)); return v; }();
            check(wide.toStringList().size() == 6, QStringLiteral("…and a wide one shows them all"), wide.toStringList().join(","));
            for (const char *k : {"created", "permissions", "owner", "kind"})
                QMetaObject::invokeMethod(win, "toggleColumn", Q_ARG(QVariant, QString::fromLatin1(k)));
            QTest::qWait(200);
            check(!splitHeading->property("cols").toStringList().contains(QStringLiteral("kind")) && !win->property("listColumns").toStringList().contains(QStringLiteral("created")),
                  QStringLiteral("columns can be removed again"), splitHeading->property("cols").toStringList().join(","));
            QMetaObject::invokeMethod(win, "toggleColumn", Q_ARG(QVariant, QStringLiteral("kind")));
            win->setProperty("viewMode", 0);
        }
        // View ▸ Transparency: the glass tint follows
        {
            const QColor before = win->property("bodyTint").value<QColor>();
            key(Qt::Key_Up, Qt::ControlModifier | Qt::AltModifier);
            const QColor after = win->property("bodyTint").value<QColor>();
            check(after.alphaF() < before.alphaF() - .05, QStringLiteral("Ctrl+Alt+Up makes the window more transparent"),
                  QString::number(before.alphaF()) + " -> " + QString::number(after.alphaF()));
            key(Qt::Key_Down, Qt::ControlModifier | Qt::AltModifier);
        }

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
        // each side has its own icon size: Ctrl+/- and the home button act on the active side only
        {
            win->setProperty("activePane", 0);
            QMetaObject::invokeMethod(win, "setActiveWheel", Q_ARG(QVariant, .352));
            win->setProperty("activePane", 1);
            QMetaObject::invokeMethod(win, "setActiveWheel", Q_ARG(QVariant, .352));
            const double mainCell = win->property("k").toDouble(), paneK = pane->property("k").toDouble();
            key(Qt::Key_Plus, Qt::ControlModifier);
            key(Qt::Key_Plus, Qt::ControlModifier);
            check(pane->property("k").toDouble() > paneK * 1.1 && qAbs(win->property("k").toDouble() - mainCell) < 1e-6,
                  QStringLiteral("Ctrl++ on the split side scales only its icons"),
                  QString::number(pane->property("k").toDouble()) + " main " + QString::number(win->property("k").toDouble()));
            auto *wheel = win->findChild<QQuickItem *>(QStringLiteral("thumbwheel"));
            const double splitWheel = win->property("splitWheelValue").toDouble();
            check(qAbs(wheel->property("value").toDouble() - splitWheel) < 1e-6, QStringLiteral("…and the thumbwheel shows that side's size"));
            win->setProperty("activePane", 0);
            QTest::qWait(50);
            check(qAbs(wheel->property("value").toDouble() - .352) < 1e-6 && qAbs(win->property("splitWheelValue").toDouble() - splitWheel) < 1e-6,
                  QStringLiteral("switching sides moves the thumbwheel to the main side's size"),
                  QString::number(wheel->property("value").toDouble()));
            key(Qt::Key_Minus, Qt::ControlModifier);
            check(win->property("k").toDouble() < mainCell && qAbs(win->property("splitWheelValue").toDouble() - splitWheel) < 1e-6,
                  QStringLiteral("Ctrl+- on the main side leaves the split side alone"));
            win->setProperty("activePane", 1);
            auto *home = win->findChild<QQuickItem *>(QStringLiteral("homeButton"));
            QTest::mouseClick(window, Qt::LeftButton, {}, home->mapToScene(QPointF(home->width() / 2, home->height() / 2)).toPoint());
            QTest::qWait(100);
            check(qAbs(pane->property("k").toDouble() - paneK) < 1e-6 && win->property("k").toDouble() < mainCell,
                  QStringLiteral("the home button resets only the active side"));
            win->setProperty("activePane", 0);
            QMetaObject::invokeMethod(win, "setActiveWheel", Q_ARG(QVariant, .352));
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
