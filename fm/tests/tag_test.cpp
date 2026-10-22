// Tags: xattr storage, Ctrl+Alt+N toggles, dots and tinted carpets, the sidebar Tags section
// (show, rename, colour, hide, delete, new, drop onto a tag), tag search across folders, Get Info,
// and tags surviving Duplicate. Off-screen, on the home disk (xattrs), with a private data folder.
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
} // namespace

int main(int argc, char *argv[])
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
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
    QDir(base).mkpath(QStringLiteral("Sub"));
    QDir(base).mkpath(QStringLiteral("Other"));
    writeFile(at(QStringLiteral("a.txt")), "a\n");
    writeFile(at(QStringLiteral("b.txt")), "b\n");
    writeFile(at(QStringLiteral("Other/c.txt")), "c\n");

    const QString assets = QStringLiteral(IRIXFM_SOURCE_DIR);
    Fm fm(base, assets);
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("Fm"), &fm);
    engine.load(QUrl::fromLocalFile(assets + QStringLiteral("/qml/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return 2;
    QObject *win = engine.rootObjects().constFirst();
    auto *window = qobject_cast<QQuickWindow *>(win);
    auto *grid = win->findChild<QQuickItem *>(QStringLiteral("grid"));
    auto *places = win->findChild<QQuickItem *>(QStringLiteral("places"));
    auto *search = win->findChild<QQuickItem *>(QStringLiteral("search"));

    auto select = [&](const QStringList &ps) {
        QVariantMap m;
        for (const QString &p : ps)
            m[p] = true;
        win->setProperty("selection", m);
    };
    auto call = [&](const char *fn, QVariantList args) {
        switch (args.size()) {
        case 1: QMetaObject::invokeMethod(win, fn, Q_ARG(QVariant, args[0])); break;
        case 2: QMetaObject::invokeMethod(win, fn, Q_ARG(QVariant, args[0]), Q_ARG(QVariant, args[1])); break;
        default: QMetaObject::invokeMethod(win, fn); break;
        }
        QTest::qWait(150);
    };
    auto cellFor = [&](const QString &path) -> QQuickItem * {
        for (QQuickItem *c : grid->property("contentItem").value<QQuickItem *>()->childItems())
            if (c->property("filePath").toString() == path)
                return c;
        return nullptr;
    };
    auto childNamed = [&](QQuickItem *root, const QString &name) -> QQuickItem * {
        std::function<QQuickItem *(QQuickItem *)> find = [&](QQuickItem *it) -> QQuickItem * {
            for (QQuickItem *c : it->childItems()) {
                if (c->objectName() == name)
                    return c;
                if (QQuickItem *r = find(c))
                    return r;
            }
            return nullptr;
        };
        return root ? find(root) : nullptr;
    };
    auto tagRows = [&] {
        QStringList out;
        for (const QVariant &r : win->property("placeRows").toList())
            if (r.toMap().value(QStringLiteral("kind")) == QStringLiteral("tag"))
                out << r.toMap().value(QStringLiteral("tag")).toMap().value(QStringLiteral("name")).toString();
        return out;
    };
    auto resultPaths = [&] {
        QStringList out;
        for (const QVariant &v : win->property("searchResults").toList())
            out << v.toMap().value(QStringLiteral("path")).toString();
        out.sort();
        return out;
    };

    QTimer::singleShot(1200, &app, [&] {
        window->requestActivate();
        QTest::qWaitForWindowActive(window);

        // --- storage
        fm.setTags(at(QStringLiteral("b.txt")), {QStringLiteral("Red"), QStringLiteral("Work")});
        check(fm.tags(at(QStringLiteral("b.txt"))) == QStringList{QStringLiteral("Red"), QStringLiteral("Work")}, QStringLiteral("tags are stored on the file (user.xdg.tags)"),
              fm.tags(at(QStringLiteral("b.txt"))).join(","));
        fm.addTag({at(QStringLiteral("b.txt"))}, QStringLiteral("red"));
        check(fm.tags(at(QStringLiteral("b.txt"))).size() == 2, QStringLiteral("adding a tag it already has (any case) doesn't duplicate"));
        fm.removeTag({at(QStringLiteral("b.txt"))}, QStringLiteral("WORK"));
        check(fm.tags(at(QStringLiteral("b.txt"))) == QStringList{QStringLiteral("Red")}, QStringLiteral("removing ignores case"));
        fm.clearTags({at(QStringLiteral("b.txt"))});
        check(fm.tags(at(QStringLiteral("b.txt"))).isEmpty(), QStringLiteral("Remove All Tags"));

        // --- sidebar defaults
        check(tagRows() == QStringList{"Red", "Orange", "Yellow", "Green", "Blue", "Purple", "Gray"}, QStringLiteral("sidebar shows the 7 default tags"),
              tagRows().join(", "));
        QVariant red;
        QMetaObject::invokeMethod(win, "tagColor", Q_RETURN_ARG(QVariant, red), Q_ARG(QVariant, QStringLiteral("red")));
        check(red.toString().compare(QStringLiteral("#BF616A"), Qt::CaseInsensitive) == 0, QStringLiteral("Red is Nord's red (#BF616A)"), red.toString());

        // --- Ctrl+Alt+1 toggles the first tag; dots + tinted carpet appear
        select({at(QStringLiteral("a.txt"))});
        QTest::keyClick(window, Qt::Key_1, Qt::ControlModifier | Qt::AltModifier);
        QTest::qWait(200);
        check(fm.tags(at(QStringLiteral("a.txt"))) == QStringList{QStringLiteral("Red")}, QStringLiteral("Ctrl+Alt+1 tags the selection Red"));
        QQuickItem *cell = cellFor(at(QStringLiteral("a.txt")));
        QQuickItem *dots = childNamed(cell, QStringLiteral("tagDots"));
        check(dots && dots->isVisible(), QStringLiteral("a tagged item shows colour dots"));
        select({});
        QTest::qWait(100);
        QQuickItem *tint = childNamed(cell, QStringLiteral("carpetTint"));
        check(tint && tint->isVisible(), QStringLiteral("…and its IRIX carpet takes the tag colour"));
        select({at(QStringLiteral("a.txt"))});
        QTest::keyClick(window, Qt::Key_1, Qt::ControlModifier | Qt::AltModifier);
        QTest::qWait(200);
        check(fm.tags(at(QStringLiteral("a.txt"))).isEmpty(), QStringLiteral("Ctrl+Alt+1 again removes it"));

        // --- right-click ▸ Tags (toggleTag) on several items, then find them everywhere
        select({at(QStringLiteral("a.txt")), at(QStringLiteral("Sub"))});
        call("toggleTag", {QStringLiteral("Blue")});
        fm.addTag({at(QStringLiteral("Other/c.txt"))}, QStringLiteral("Blue"));
        check(fm.tags(at(QStringLiteral("Sub"))) == QStringList{QStringLiteral("Blue")}, QStringLiteral("folders can be tagged too"));
        call("showTagged", {QStringLiteral("Blue")});
        check(waitFor([&] { return !win->property("searching").toBool() && resultPaths().size() == 3; }),
              QStringLiteral("clicking a tag shows its items from every folder"), resultPaths().join(", "));

        // --- rename, colour, hide, delete
        call("renameTag", {QStringLiteral("Blue"), QStringLiteral("Work")});
        check(fm.tags(at(QStringLiteral("Other/c.txt"))) == QStringList{QStringLiteral("Work")} && tagRows().contains(QStringLiteral("Work")),
              QStringLiteral("renaming a tag renames it on every file"), fm.tags(at(QStringLiteral("Other/c.txt"))).join(","));
        check(search->property("text").toString() == QStringLiteral("tag:Work"), QStringLiteral("…and the open tag view follows"),
              search->property("text").toString());
        call("setTagColor", {QStringLiteral("Work"), QStringLiteral("#88C0D0")});
        QVariant c;
        QMetaObject::invokeMethod(win, "tagColor", Q_RETURN_ARG(QVariant, c), Q_ARG(QVariant, QStringLiteral("work")));
        check(c.toString() == QStringLiteral("#88C0D0"), QStringLiteral("a tag's colour can be changed (Nord Cyan)"), c.toString());
        call("setTagShown", {QStringLiteral("Work"), false});
        check(!tagRows().contains(QStringLiteral("Work")), QStringLiteral("Remove from Sidebar hides it"));
        call("setTagShown", {QStringLiteral("Work"), true});
        check(tagRows().contains(QStringLiteral("Work")), QStringLiteral("Show in Sidebar brings it back"));
        call("deleteTag", {QStringLiteral("Work")});
        check(fm.tags(at(QStringLiteral("a.txt"))).isEmpty() && fm.tags(at(QStringLiteral("Other/c.txt"))).isEmpty() && !tagRows().contains(QStringLiteral("Work")),
              QStringLiteral("Delete Tag removes it from all items and the sidebar"));
        search->setProperty("text", QString());

        // --- new tag from the selection
        select({at(QStringLiteral("b.txt"))});
        call("newTag", {true});
        check(tagRows().contains(QStringLiteral("New Tag")) && fm.tags(at(QStringLiteral("b.txt"))) == QStringList{QStringLiteral("New Tag")}
                  && win->property("renamingTag").toString() == QStringLiteral("New Tag"),
              QStringLiteral("New Tag… makes a tag, tags the selection and opens it for renaming"));
        win->setProperty("renamingTag", QString());

        // --- drop files onto a tag in the sidebar
        QQuickItem *redRow = nullptr;
        for (QQuickItem *r : places->property("contentItem").value<QQuickItem *>()->childItems())
            if (r->property("modelData").toMap().value(QStringLiteral("tag")).toMap().value(QStringLiteral("name")) == QStringLiteral("Red"))
                redRow = r;
        if (redRow) {
            places->setProperty("contentY", redRow->y() - 20);
            QTest::qWait(150);
            const QPoint p = redRow->mapToScene(QPointF(redRow->width() / 2, redRow->height() / 2)).toPoint();
            QMimeData mime;
            mime.setUrls({QUrl::fromLocalFile(at(QStringLiteral("a.txt"))), QUrl::fromLocalFile(at(QStringLiteral("b.txt")))});
            QDragEnterEvent enter(p, Qt::CopyAction | Qt::LinkAction, &mime, Qt::LeftButton, Qt::NoModifier);
            QCoreApplication::sendEvent(window, &enter);
            QDragMoveEvent move(p, Qt::CopyAction | Qt::LinkAction, &mime, Qt::LeftButton, Qt::NoModifier);
            QCoreApplication::sendEvent(window, &move);
            QDropEvent drop(p, Qt::CopyAction | Qt::LinkAction, &mime, Qt::LeftButton, Qt::NoModifier);
            QCoreApplication::sendEvent(window, &drop);
            QTest::qWait(200);
        }
        check(redRow && fm.tags(at(QStringLiteral("a.txt"))).contains(QStringLiteral("Red")) && fm.tags(at(QStringLiteral("b.txt"))).contains(QStringLiteral("Red")),
              QStringLiteral("dropping files onto a tag in the sidebar tags them"));

        // --- Get Info, Duplicate keeps tags, the index survives in the data folder
        check(fm.info(at(QStringLiteral("a.txt"))).value(QStringLiteral("tags")).toStringList() == QStringList{QStringLiteral("Red")},
              QStringLiteral("Get Info shows the tags"));
        const QStringList dup = fm.duplicate({at(QStringLiteral("a.txt"))});
        check(waitFor([&] { return QFile::exists(dup.value(0)); }) && waitFor([&] { return fm.tags(dup.value(0)).contains(QStringLiteral("Red")); }, 2000),
              QStringLiteral("a duplicate keeps the tags"), fm.tags(dup.value(0)).join(","));
        check(QFile::exists(data.path() + QStringLiteral("/irixfm/tags.json")), QStringLiteral("fm keeps a tag index next to its data"));

        app.exit(failures ? 1 : 0);
    });
    const int rc = app.exec();
    QDir().rmdir(scratch);
    return rc;
}
