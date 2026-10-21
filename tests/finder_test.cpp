// Finder features (phase 1): sorting, type to select, Quick Look, Get Info, Duplicate, Make Alias,
// Show Original, Move Item Here, Undo and the Finder shortcuts, driven with real key presses on
// an off-screen window in a throwaway folder. Run with `make test`.
#include "fm.h"

#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QKeyEvent>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <cstdio>

namespace
{
int failures = 0;

void check(bool ok, const QString &what, const QString &got = {})
{
    std::printf("%s %-56s %s\n", ok ? "PASS" : "FAIL", qPrintable(what), ok ? "" : qPrintable("got: " + got));
    failures += ok ? 0 : 1;
}

void writeFile(const QString &path, const QByteArray &data)
{
    QFile f(path);
    if (f.open(QIODevice::WriteOnly))
        f.write(data);
}

bool waitFor(const std::function<bool()> &cond, int ms = 3000)
{
    for (int t = 0; t < ms && !cond(); t += 50)
        QTest::qWait(50);
    return cond();
}
} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("irixfm-test"));
    QQuickStyle::setStyle(QStringLiteral("org.kde.desktop"));

    QTemporaryDir tmp;
    const QString base = QDir(tmp.path()).canonicalPath();
    QDir(base).mkdir(QStringLiteral("Sub"));
    writeFile(base + QStringLiteral("/a.txt"), "hello finder\n");
    writeFile(base + QStringLiteral("/notes.md"), QByteArray(4000, 'x'));
    QImage img(64, 48, QImage::Format_RGB32);
    img.fill(Qt::darkCyan);
    img.save(base + QStringLiteral("/pic.png"));
    auto at = [&](const char *n) { return base + QLatin1Char('/') + QString::fromLatin1(n); };

    const QString assets = QStringLiteral(IRIXFM_SOURCE_DIR);
    Fm fm(base, assets);
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("Fm"), &fm);
    engine.load(QUrl::fromLocalFile(assets + QStringLiteral("/qml/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return 2;
    QObject *win = engine.rootObjects().constFirst();
    auto *window = qobject_cast<QQuickWindow *>(win);
    // Quick Look is loaded on first use.
    auto qlKind = [&] {
        auto *ql = win->findChild<QQuickItem *>(QStringLiteral("quickLook"));
        return ql ? ql->property("kind").toString() : QStringLiteral("(not loaded)");
    };

    // Through QTest, so key combinations reach QML Shortcuts like real key presses do.
    auto key = [&](Qt::Key k, Qt::KeyboardModifiers m = Qt::NoModifier) {
        QTest::keyClick(window, k, m);
        QTest::qWait(30);
    };
    auto type = [&](const QString &s) { for (const QChar c : s) key(Qt::Key(c.toUpper().unicode())); };
    auto selected = [&] { return win->property("selection").toMap().keys(); };
    auto selectOnly = [&](const QString &path) { win->setProperty("selection", QVariantMap{{path, true}}); };
    auto first = [&] {
        QVariant v;
        QObject *model = nullptr;
        for (QObject *o : win->findChildren<QObject *>())
            if (QString::fromLatin1(o->metaObject()->className()).contains(QLatin1String("FolderListModel")))
                model = o;
        QMetaObject::invokeMethod(model, "get", Q_RETURN_ARG(QVariant, v), Q_ARG(int, 0), Q_ARG(QString, QStringLiteral("fileName")));
        return v.toString();
    };

    QTimer::singleShot(1200, &app, [&] {
        window->requestActivate();
        QTest::qWaitForWindowActive(window);
        QTest::qWait(300);

        // --- sorting
        // FolderListModel.SortField: Unsorted 0, Name 1, Time 2, Size 3, Type 4
        win->setProperty("sortField", 3);
        win->setProperty("sortReversed", true);
        QTest::qWait(300);
        check(first() == QStringLiteral("Sub") || first() == QStringLiteral("notes.md"), QStringLiteral("Sort By Size, largest first (folders first)"), first());
        win->setProperty("sortField", 1);
        win->setProperty("sortReversed", false);
        QTest::qWait(300);

        // --- type to select
        type(QStringLiteral("no"));
        check(selected() == QStringList{at("notes.md")}, QStringLiteral("typing \"no\" selects notes.md"), selected().join(", "));
        QTest::qWait(1100);
        type(QStringLiteral("p"));
        check(selected() == QStringList{at("pic.png")}, QStringLiteral("after a pause, typing starts over (\"p\" -> pic.png)"), selected().join(", "));

        // --- Quick Look (after the type-to-select pause; Space while typing is part of the name)
        QTest::qWait(1100);
        key(Qt::Key_Space);
        check(win->property("quickLook").toString() == at("pic.png") && qlKind() == QStringLiteral("image"),
              QStringLiteral("Space opens Quick Look with the picture"), qlKind());
        key(Qt::Key_Left);   // by name: Sub, a.txt, notes.md, pic.png
        check(win->property("quickLook").toString() == at("notes.md") && qlKind() == QStringLiteral("text"),
              QStringLiteral("arrow keys step through items in Quick Look (text)"), win->property("quickLook").toString());
        key(Qt::Key_Left);
        key(Qt::Key_Left);
        check(qlKind() == QStringLiteral("folder"), QStringLiteral("Quick Look of a folder"), qlKind());
        key(Qt::Key_Escape);
        check(win->property("quickLook").toString().isEmpty(), QStringLiteral("Esc closes Quick Look"));

        // --- Get Info
        const int before = QGuiApplication::topLevelWindows().size();
        key(Qt::Key_I, Qt::ControlModifier);
        QTest::qWait(200);
        QWindow *infoWindow = nullptr;
        for (QWindow *w : QGuiApplication::topLevelWindows())
            if (w->title() == QStringLiteral("Sub Info"))
                infoWindow = w;
        check(QGuiApplication::topLevelWindows().size() > before && infoWindow, QStringLiteral("Ctrl+I opens a Get Info window"));
        // Like Finder, the Info window takes the keyboard; close it and go back to the main window.
        if (infoWindow)
            infoWindow->close();
        window->requestActivate();
        QTest::qWaitForWindowActive(window);
        const QVariantMap info = fm.info(at("a.txt"));
        check(info.value(QStringLiteral("sizeText")).toString().contains(QStringLiteral("13 bytes")) && !info.value(QStringLiteral("kindText")).toString().isEmpty(),
              QStringLiteral("Get Info: size and kind"), info.value(QStringLiteral("sizeText")).toString());
        QSignalSpy sizeSpy(&fm, &Fm::folderSizeReady);
        fm.folderSize(base);
        check(sizeSpy.wait(3000) && sizeSpy.first().at(2).toLongLong() >= 3, QStringLiteral("Get Info: folder size is counted"));

        // --- Duplicate, Make Alias, Show Original, Undo
        selectOnly(at("a.txt"));
        key(Qt::Key_D, Qt::ControlModifier);
        check(waitFor([&] { return QFile::exists(at("a copy.txt")); }), QStringLiteral("Ctrl+D duplicates to \"a copy.txt\""));
        check(waitFor([&] { return selected() == QStringList{at("a copy.txt")}; }), QStringLiteral("the duplicate gets selected"), selected().join(", "));
        selectOnly(at("a.txt"));
        key(Qt::Key_D, Qt::ControlModifier);
        check(waitFor([&] { return QFile::exists(at("a copy 2.txt")); }), QStringLiteral("a second duplicate is \"a copy 2.txt\""));
        selectOnly(at("a.txt"));
        QMetaObject::invokeMethod(win, "aliasSelected");
        check(waitFor([&] { return QFileInfo(at("a.txt alias")).isSymLink(); }), QStringLiteral("Make Alias creates \"a.txt alias\""));
        waitFor([&] { return selected() == QStringList{at("a.txt alias")}; });
        key(Qt::Key_R, Qt::ControlModifier);
        check(waitFor([&] { return selected() == QStringList{at("a.txt")}; }), QStringLiteral("Ctrl+R shows the original"), selected().join(", "));
        check(fm.canUndo(), QStringLiteral("Undo is available"), fm.undoText());
        key(Qt::Key_Z, Qt::ControlModifier);
        check(waitFor([&] { return !QFileInfo(at("a.txt alias")).isSymLink(); }), QStringLiteral("Ctrl+Z undoes the alias"));
        key(Qt::Key_Z, Qt::ControlModifier);
        check(waitFor([&] { return !QFile::exists(at("a copy 2.txt")); }), QStringLiteral("Ctrl+Z again undoes the second duplicate"));

        // --- Move Item Here (Ctrl+Alt+V), Ctrl+Down / Ctrl+Up
        fm.copyToClipboard({at("a copy.txt")});
        selectOnly(at("Sub"));
        win->setProperty("anchorIndex", 0);
        key(Qt::Key_Down, Qt::ControlModifier);
        check(waitFor([&] { return win->property("dir").toString() == at("Sub"); }), QStringLiteral("Ctrl+Down opens the selected folder"),
              win->property("dir").toString());
        key(Qt::Key_V, Qt::ControlModifier | Qt::AltModifier);
        check(waitFor([&] { return QFile::exists(at("Sub/a copy.txt")) && !QFile::exists(at("a copy.txt")); }),
              QStringLiteral("Ctrl+Alt+V moves the copied item here"));
        key(Qt::Key_Up, Qt::ControlModifier);
        check(waitFor([&] { return win->property("dir").toString() == base; }), QStringLiteral("Ctrl+Up goes to the enclosing folder"));

        app.exit(failures ? 1 : 0);
    });
    return app.exec();
}
