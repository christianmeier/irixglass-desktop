// Finder features, phase 2: search (this folder / subfolders), Trash (sidebar, Put Back, Empty,
// Delete Immediately), Open With, removable drives, spring-loaded folders, hide sidebar, preview
// pane, folder sizes, Compress / Expand and Recent Folders. Off-screen, with its own XDG data
// folder so the user's Trash is never touched. Run with `make test`.
#include "fm.h"

#include <QApplication>
#include <QDir>
#include <QDropEvent>
#include <QFile>
#include <QImage>
#include <QMimeData>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QStandardPaths>
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

void writeFile(const QString &path, const QByteArray &data)
{
    QFile f(path);
    if (f.open(QIODevice::WriteOnly))
        f.write(data);
}

bool waitFor(const std::function<bool()> &cond, int ms = 5000)
{
    for (int t = 0; t < ms && !cond(); t += 50)
        QTest::qWait(50);
    return cond();
}

QStringList resultNames(QObject *win)
{
    QStringList out;
    for (const QVariant &v : win->property("searchResults").toList())
        out << v.toMap().value(QStringLiteral("name")).toString();
    out.sort();
    return out;
}
} // namespace

int main(int argc, char *argv[])
{
    // A private data folder: Fm::trashDir() and KIO's trash worker both use $XDG_DATA_HOME/Trash.
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    // On the home disk, not /tmp: KIO doesn't trash files on tmpfs (it asks to delete them instead).
    const QString scratch = QDir::homePath() + QStringLiteral("/.cache/irixfm-test");
    QDir().mkpath(scratch);
    QTemporaryDir data(scratch + QStringLiteral("/data-XXXXXX"));
    qputenv("XDG_DATA_HOME", data.path().toUtf8());
    qputenv("IRIXFM_NO_DIALOGS", "1");
    QApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("irixfm-test"));
    QQuickStyle::setStyle(QStringLiteral("org.kde.desktop"));

    QTemporaryDir tmp(scratch + QStringLiteral("/files-XXXXXX"));
    const QString base = QDir(tmp.path()).canonicalPath();
    auto at = [&](const QString &n) { return base + QLatin1Char('/') + n; };
    QDir(base).mkpath(QStringLiteral("Sub/deep"));
    QDir(base).mkdir(QStringLiteral("NeedleDir"));
    QDir(base).mkdir(QStringLiteral("Work"));
    writeFile(at(QStringLiteral("a.txt")), "hello\n");
    writeFile(at(QStringLiteral("needle-top.txt")), "top\n");
    writeFile(at(QStringLiteral("Sub/deep/Needle.txt")), QByteArray(5000, 'n'));
    QImage img(32, 32, QImage::Format_RGB32);
    img.fill(Qt::red);
    img.save(at(QStringLiteral("pic.png")));

    const QString assets = QStringLiteral(IRIXFM_SOURCE_DIR);
    Fm fm(base, assets);
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("Fm"), &fm);
    engine.load(QUrl::fromLocalFile(assets + QStringLiteral("/qml/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return 2;
    QObject *win = engine.rootObjects().constFirst();
    auto *window = qobject_cast<QQuickWindow *>(win);
    auto *search = win->findChild<QQuickItem *>(QStringLiteral("search"));
    auto *grid = win->findChild<QQuickItem *>(QStringLiteral("grid"));
    auto *preview = win->findChild<QQuickItem *>(QStringLiteral("previewPane"));
    auto *status = win->findChild<QQuickItem *>(QStringLiteral("status"));
    auto dir = [&] { return win->property("dir").toString(); };
    auto go = [&](const QString &p) { QMetaObject::invokeMethod(win, "go", Q_ARG(QVariant, p), Q_ARG(QVariant, false)); QTest::qWait(300); };
    auto select = [&](const QStringList &ps) {
        QVariantMap m;
        for (const QString &p : ps)
            m[p] = true;
        win->setProperty("selection", m);
    };
    auto key = [&](Qt::Key k, Qt::KeyboardModifiers m = Qt::NoModifier) { QTest::keyClick(window, k, m); QTest::qWait(30); };

    QTimer::singleShot(1200, &app, [&] {
        window->requestActivate();
        QTest::qWaitForWindowActive(window);

        // --- search
        search->setProperty("text", QStringLiteral("needle"));
        check(waitFor([&] { return !win->property("searching").toBool() && !resultNames(win).isEmpty(); }), QStringLiteral("search runs"));
        check(resultNames(win) == QStringList{QStringLiteral("Needle.txt"), QStringLiteral("NeedleDir"), QStringLiteral("needle-top.txt")},
              QStringLiteral("default scope finds matches in subfolders, folders too"), resultNames(win).join(", "));
        check(status->property("text").toString().startsWith(QStringLiteral("3 results")), QStringLiteral("status shows the result count"),
              status->property("text").toString());
        win->setProperty("searchScope", 0);
        waitFor([&] { return !win->property("searching").toBool() && resultNames(win).size() == 2; });
        check(resultNames(win) == QStringList{QStringLiteral("NeedleDir"), QStringLiteral("needle-top.txt")},
              QStringLiteral("\"This folder\" scope stays in the folder"), resultNames(win).join(", "));
        win->setProperty("searchScope", 1);
        search->setProperty("text", QStringLiteral("zzz"));
        QTest::qWait(600);
        check(resultNames(win).isEmpty(), QStringLiteral("no match shows no results (folders are filtered too)"), resultNames(win).join(", "));
        search->setProperty("text", QStringLiteral("deep"));
        waitFor([&] { return resultNames(win) == QStringList{QStringLiteral("deep")}; });
        window->requestActivate();
        QMetaObject::invokeMethod(win, "selectResult", Q_ARG(QVariant, 0));
        key(Qt::Key_Return);
        check(waitFor([&] { return dir() == at(QStringLiteral("Sub/deep")); }), QStringLiteral("Enter on a folder result opens it"), dir());
        check(search->property("text").toString().isEmpty(), QStringLiteral("…and clears the search"));
        go(base);

        // --- Trash
        check(fm.trashDir().startsWith(QDir(data.path()).canonicalPath()) || fm.trashDir().startsWith(data.path()), QStringLiteral("test uses a private Trash"), fm.trashDir());
        fm.trash({at(QStringLiteral("a.txt"))});
        check(waitFor([&] { return QFile::exists(fm.trashDir() + QStringLiteral("/a.txt")); }), QStringLiteral("Move to Trash puts it in the Trash"));
        bool trashInSidebar = false;
        for (const QVariant &r : win->property("placeRows").toList())
            trashInSidebar |= r.toMap().value(QStringLiteral("item")).toMap().value(QStringLiteral("action")) == QStringLiteral("trash");
        check(trashInSidebar, QStringLiteral("Trash is in the sidebar"));
        const QVariant trashItem = QVariantMap{{QStringLiteral("label"), QStringLiteral("Trash")}, {QStringLiteral("action"), QStringLiteral("trash")}};
        QMetaObject::invokeMethod(win, "activatePlace", Q_ARG(QVariant, trashItem));
        QTest::qWait(300);
        check(win->property("inTrash").toBool() && dir() == fm.trashDir(), QStringLiteral("clicking Trash opens it"), dir());
        check(waitFor([&] { return status->property("text").toString().contains(QStringLiteral("in Trash")); }), QStringLiteral("status says \"in Trash\""),
              status->property("text").toString());
        select({fm.trashDir() + QStringLiteral("/a.txt")});
        QMetaObject::invokeMethod(win, "putBack");
        check(waitFor([&] { return QFile::exists(at(QStringLiteral("a.txt"))); }), QStringLiteral("Put Back restores it to where it was"));
        fm.trash({at(QStringLiteral("a.txt")), at(QStringLiteral("needle-top.txt"))});
        waitFor([&] { return fm.trashCount() == 2; });
        select({fm.trashDir() + QStringLiteral("/needle-top.txt")});
        QMetaObject::invokeMethod(win, "deleteNow");
        check(waitFor([&] { return fm.trashCount() == 1; }), QStringLiteral("Delete Immediately removes one item"), QString::number(fm.trashCount()));
        QMetaObject::invokeMethod(win, "emptyTrashNow");
        check(waitFor([&] { return fm.trashCount() == 0; }), QStringLiteral("Empty Trash empties it"), QString::number(fm.trashCount()));
        go(base);

        // --- Open With
        const QVariantList apps = fm.appsFor(at(QStringLiteral("pic.png")));
        check(!apps.isEmpty() && !apps.first().toMap().value(QStringLiteral("id")).toString().isEmpty(), QStringLiteral("Open With lists apps for a picture"),
              QString::number(apps.size()));

        // --- removable drives (a fake one; real ones come from Solid)
        win->setProperty("volumes", QVariantList{QVariantMap{{QStringLiteral("udi"), QStringLiteral("/test/usb")}, {QStringLiteral("label"), QStringLiteral("USB Stick")},
                                                             {QStringLiteral("mounted"), true}, {QStringLiteral("path"), at(QStringLiteral("Work"))}}});
        QVariantMap volRow;
        for (const QVariant &r : win->property("placeRows").toList())
            if (r.toMap().value(QStringLiteral("kind")) == QStringLiteral("volume"))
                volRow = r.toMap();
        check(volRow.value(QStringLiteral("item")).toMap().value(QStringLiteral("label")) == QStringLiteral("USB Stick"), QStringLiteral("a drive shows in Locations"));
        QMetaObject::invokeMethod(win, "activatePlace", Q_ARG(QVariant, volRow.value(QStringLiteral("item"))));
        QTest::qWait(300);
        check(dir() == at(QStringLiteral("Work")), QStringLiteral("clicking a mounted drive opens it"), dir());
        win->setProperty("volumes", QVariantList{});
        go(base);

        // --- spring-loaded folders: drag a file over "Sub" and wait
        QTest::qWait(400);
        QQuickItem *subCell = nullptr;
        for (QQuickItem *c : grid->property("contentItem").value<QQuickItem *>()->childItems())
            if (c->property("filePath").toString() == at(QStringLiteral("Sub")))
                subCell = c;
        check(subCell, QStringLiteral("found the Sub folder icon"));
        if (subCell) {
            const QPointF p = subCell->mapToScene(QPointF(subCell->width() / 2, subCell->height() * .35));
            QMimeData mime;
            mime.setUrls({QUrl::fromLocalFile(at(QStringLiteral("pic.png")))});
            QDragEnterEvent enter(p.toPoint(), Qt::CopyAction | Qt::MoveAction, &mime, Qt::LeftButton, Qt::NoModifier);
            QCoreApplication::sendEvent(window, &enter);
            QDragMoveEvent move(p.toPoint(), Qt::CopyAction | Qt::MoveAction, &mime, Qt::LeftButton, Qt::NoModifier);
            QCoreApplication::sendEvent(window, &move);
            check(waitFor([&] { return dir() == at(QStringLiteral("Sub")); }, 2500), QStringLiteral("hovering a drag over a folder opens it"), dir());
            QDragLeaveEvent leave;
            QCoreApplication::sendEvent(window, &leave);
        }
        go(base);

        // --- hide sidebar, preview pane
        const double gx = grid->x();
        key(Qt::Key_S, Qt::ControlModifier | Qt::AltModifier);
        QTest::qWait(400);
        check(!win->property("sidebarShown").toBool() && grid->x() < gx - 50, QStringLiteral("Ctrl+Alt+S hides the sidebar, content moves left"),
              QString::number(grid->x()) + " vs " + QString::number(gx));
        key(Qt::Key_S, Qt::ControlModifier | Qt::AltModifier);
        QTest::qWait(400);
        check(win->property("sidebarShown").toBool() && qAbs(grid->x() - gx) < 1, QStringLiteral("…and shows it again"));
        select({at(QStringLiteral("pic.png"))});
        key(Qt::Key_P, Qt::ControlModifier | Qt::ShiftModifier);
        QTest::qWait(400);
        check(preview->isVisible() && preview->property("path").toString() == at(QStringLiteral("pic.png"))
                  && preview->property("kind").toString() == QStringLiteral("image"),
              QStringLiteral("Ctrl+Shift+P shows the preview pane for the selection"), preview->property("path").toString());
        key(Qt::Key_P, Qt::ControlModifier | Qt::ShiftModifier);

        // --- folder sizes in the list
        win->setProperty("viewMode", 1);
        check(waitFor([&] { return win->property("folderSizes").toMap().contains(at(QStringLiteral("Sub"))); }),
              QStringLiteral("list view counts folder sizes"));
        check(win->property("folderSizes").toMap().value(at(QStringLiteral("Sub"))).toString().contains(QStringLiteral("5")),
              QStringLiteral("…with the real size (5 kB)"), win->property("folderSizes").toMap().value(at(QStringLiteral("Sub"))).toString());
        // The Size / Date Modified / Kind values must be inside the visible list, not pushed off to
        // the right (a hidden rename field used to take up the row's space).
        {
            auto *list = win->findChild<QQuickItem *>(QStringLiteral("list"));
            QStringList seen;
            bool inside = false;
            std::function<void(QQuickItem *)> walk = [&](QQuickItem *it) {
                for (QQuickItem *c : it->childItems()) {
                    const QString t = c->property("text").toString();
                    if (c->isVisible() && (t == QStringLiteral("Folder") || t.contains(QStringLiteral("PNG")))) {
                        const QPointF p = c->mapToItem(list, QPointF(0, 0));
                        seen << t;
                        inside |= p.x() > 0 && p.x() + 10 < list->width();
                    }
                    walk(c);
                }
            };
            walk(list->property("contentItem").value<QQuickItem *>());
            check(inside, QStringLiteral("list view shows the Kind column inside the window"), seen.join(", "));
        }
        // List icons follow the icon-size thumbwheel; rows grow with them.
        {
            const double small = (win->setProperty("wheelValue", 0.1), QTest::qWait(100), win->property("listIcon").toDouble());
            const double rowSmall = win->property("listRowH").toDouble();
            win->setProperty("wheelValue", 0.9);
            QTest::qWait(100);
            const double big = win->property("listIcon").toDouble(), rowBig = win->property("listRowH").toDouble();
            check(big > small * 1.8 && rowBig > rowSmall, QStringLiteral("list icons and rows scale with the icon size"),
                  QString::number(small) + " -> " + QString::number(big));
            win->setProperty("wheelValue", 0.352);
        }
        win->setProperty("viewMode", 0);

        // --- sidebar: hovering an entry shows a remove button that removes it
        {
            auto *places = win->findChild<QQuickItem *>(QStringLiteral("places"));
            QQuickItem *docs = nullptr;
            for (QQuickItem *c : places->property("contentItem").value<QQuickItem *>()->childItems())
                if (c->property("modelData").toMap().value(QStringLiteral("item")).toMap().value(QStringLiteral("label")) == QStringLiteral("Documents"))
                    docs = c;
            QQuickItem *remove = nullptr;
            for (QQuickItem *c : docs ? docs->childItems() : QList<QQuickItem *>())
                if (c->objectName() == QStringLiteral("removeButton"))
                    remove = c;
            QTest::mouseMove(window, docs->mapToScene(QPointF(docs->width() / 2, docs->height() / 2)).toPoint());
            QTest::qWait(150);
            check(remove && remove->isVisible(), QStringLiteral("hovering a sidebar entry shows the remove button"));
            if (remove && remove->isVisible())
                QTest::mouseClick(window, Qt::LeftButton, {}, remove->mapToScene(QPointF(remove->width() / 2, remove->height() / 2)).toPoint());
            QTest::qWait(200);
            bool gone = true;
            for (const QVariant &r : win->property("placeRows").toList())
                gone &= r.toMap().value(QStringLiteral("item")).toMap().value(QStringLiteral("label")) != QStringLiteral("Documents");
            check(gone, QStringLiteral("…and clicking it removes the entry"));
        }

        // --- compress / expand
        select({at(QStringLiteral("pic.png")), at(QStringLiteral("Sub"))});
        QMetaObject::invokeMethod(win, "compressSelected");
        check(waitFor([&] { return QFile::exists(at(QStringLiteral("Archive.zip"))); }), QStringLiteral("Compress 2 items makes Archive.zip"));
        check(waitFor([&] { return win->property("selection").toMap().keys() == QStringList{at(QStringLiteral("Archive.zip"))}; }),
              QStringLiteral("…and selects it"));
        check(fm.isArchive(at(QStringLiteral("Archive.zip"))), QStringLiteral("the zip is recognised as an archive"));
        QMetaObject::invokeMethod(win, "expandSelected");
        check(waitFor([&] { return QFile::exists(at(QStringLiteral("Archive/Sub/deep/Needle.txt"))) && QFile::exists(at(QStringLiteral("Archive/pic.png"))); }),
              QStringLiteral("Expand Here unpacks into \"Archive\""));

        // --- recent folders
        go(at(QStringLiteral("Work")));
        go(base);
        check(win->property("recentJson").toString().contains(at(QStringLiteral("Work"))), QStringLiteral("Recent Folders remembers visited folders"),
              win->property("recentJson").toString());
        QMetaObject::invokeMethod(win, "clearRecent");
        check(win->property("recentJson").toString() == QStringLiteral("[]"), QStringLiteral("Clear Menu empties it"));

        app.exit(failures ? 1 : 0);
    });
    const int rc = app.exec();
    QDir().rmdir(scratch);   // only if empty: the temporary dirs remove themselves
    return rc;
}
