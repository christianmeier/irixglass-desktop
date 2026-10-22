// Path bar test: resolving and completing typed paths (C++), and typing paths into the bar (QML),
// on an off-screen window in a throwaway folder tree. Run with `make test`.
#include "fm.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QKeyEvent>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <cstdio>

namespace
{
int failures = 0;

// QTest::keyClicks only takes widgets; Qt Quick windows get one key event per character.
void typeText(QWindow *w, const QString &text)
{
    for (const QChar c : text) {
        const int key = c.isLetter() ? c.toUpper().unicode() : c.unicode();
        QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier, QString(c));
        QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier, QString(c));
        QCoreApplication::sendEvent(w, &press);
        QCoreApplication::sendEvent(w, &release);
    }
}

void check(bool ok, const QString &what, const QString &got = {})
{
    std::printf("%s %-52s %s\n", ok ? "PASS" : "FAIL", qPrintable(what), ok ? "" : qPrintable("got: " + got));
    failures += ok ? 0 : 1;
}
} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("irixfm-test"));
    QQuickStyle::setStyle(QStringLiteral("org.kde.desktop"));

    QTemporaryDir tmp;
    const QString base = QDir(tmp.path()).canonicalPath();
    for (const char *d : {"Alpha", "Album", "beta", ".hidden"})
        QDir(base).mkdir(QString::fromLatin1(d));
    QFile f(base + QStringLiteral("/notes.txt"));
    if (f.open(QIODevice::WriteOnly))
        f.close();

    const QString assets = QStringLiteral(IRIXFM_SOURCE_DIR);
    Fm fm(base, assets);

    // --- C++: resolving and completing
    auto dirOf = [&](const QString &t) { return fm.resolvePath(t, base).value(QStringLiteral("dir")).toString(); };
    check(dirOf(QStringLiteral("~")) == QDir::homePath(), QStringLiteral("~ is the home folder"), dirOf(QStringLiteral("~")));
    check(dirOf(QStringLiteral("Alpha")) == base + QStringLiteral("/Alpha"), QStringLiteral("relative path"), dirOf(QStringLiteral("Alpha")));
    check(dirOf(QStringLiteral("Alpha/../beta/")) == base + QStringLiteral("/beta"), QStringLiteral(".. and trailing / are cleaned up"));
    check(dirOf(QStringLiteral("file://") + base + QStringLiteral("/Album")) == base + QStringLiteral("/Album"), QStringLiteral("file:// URL"));
    check(dirOf(QStringLiteral("  ") + base + QStringLiteral("/beta  ")) == base + QStringLiteral("/beta"), QStringLiteral("spaces around are ignored"));
    const QVariantMap file = fm.resolvePath(QStringLiteral("notes.txt"), base);
    check(file.value(QStringLiteral("dir")).toString() == base && file.value(QStringLiteral("file")).toString() == base + QStringLiteral("/notes.txt"),
          QStringLiteral("a file resolves to its folder + the file"));
    check(!fm.resolvePath(QStringLiteral("nope"), base).value(QStringLiteral("ok")).toBool(), QStringLiteral("a missing path is rejected"));
    auto complete = [&](const QString &t) { return fm.completePath(t, base); };
    check(complete(QStringLiteral("Alp")) == QStringLiteral("Alpha/"), QStringLiteral("Tab completes a unique folder"), complete(QStringLiteral("Alp")));
    check(complete(QStringLiteral("al")) == QStringLiteral("al"), QStringLiteral("Tab keeps an ambiguous prefix (Alpha/Album)"), complete(QStringLiteral("al")));
    check(complete(QStringLiteral("B")) == QStringLiteral("beta/"), QStringLiteral("completion ignores case"), complete(QStringLiteral("B")));
    check(complete(base + QStringLiteral("/Alb")) == base + QStringLiteral("/Album/"), QStringLiteral("completion in an absolute path"));
    check(complete(QStringLiteral(".h")) == QStringLiteral(".hidden/"), QStringLiteral("hidden folders complete after a dot"));
    check(complete(QStringLiteral("x")) == QStringLiteral("x"), QStringLiteral("no match leaves the text alone"));

    // --- QML: typing into the path bar
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("Fm"), &fm);
    engine.load(QUrl::fromLocalFile(assets + QStringLiteral("/qml/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return 2;
    QObject *win = engine.rootObjects().constFirst();
    auto *window = qobject_cast<QQuickWindow *>(win);
    auto *input = win->findChild<QQuickItem *>(QStringLiteral("pathInput"));
    auto ctrlL = [&] { QTest::keyClick(window, Qt::Key_L, Qt::ControlModifier); };
    auto type = [&](const QString &text) {
        ctrlL();
        QTest::keyClick(window, Qt::Key_A, Qt::ControlModifier);
        typeText(window, text);
        QTest::keyClick(window, Qt::Key_Return);
        QTest::qWait(300);
    };
    auto dir = [&] { return win->property("dir").toString(); };

    QTimer::singleShot(1200, &app, [&] {
        // Keyboard focus needs an active window, also off-screen.
        window->requestActivate();
        if (!QTest::qWaitForWindowActive(window))
            std::printf("note: window did not become active\n");
        ctrlL();
        check(win->property("editingPath").toBool() && input->hasActiveFocus(), QStringLiteral("Ctrl+L / click opens the text field"));
        check(input->property("text").toString() == base + QStringLiteral("/"), QStringLiteral("the field starts with the current path"),
              input->property("text").toString());
        QTest::keyClick(window, Qt::Key_Escape);
        check(!win->property("editingPath").toBool() && dir() == base, QStringLiteral("Esc cancels"));

        type(base + QStringLiteral("/Alpha"));
        check(dir() == base + QStringLiteral("/Alpha") && !win->property("editingPath").toBool(), QStringLiteral("typing a path + Enter goes there"), dir());
        type(QStringLiteral(".."));
        check(dir() == base, QStringLiteral(".. goes up"), dir());
        type(QStringLiteral("notes.txt"));
        QTest::qWait(500);
        const QVariantMap sel = win->property("selection").toMap();
        check(dir() == base && sel.contains(base + QStringLiteral("/notes.txt")), QStringLiteral("a file path opens its folder and selects it"));
        type(QStringLiteral("nope"));
        check(dir() == base && win->property("editingPath").toBool(), QStringLiteral("a wrong path keeps the field open"), dir());
        QTest::keyClick(window, Qt::Key_Escape);

        ctrlL();
        QTest::keyClick(window, Qt::Key_A, Qt::ControlModifier);
        typeText(window, QStringLiteral("Alp"));
        check(input->property("ghost").toString() == QStringLiteral("ha/"), QStringLiteral("the rest of a match shows greyed out"),
              input->property("ghost").toString());
        QTest::keyClick(window, Qt::Key_Tab);
        check(input->property("text").toString() == QStringLiteral("Alp") + QStringLiteral("ha/"), QStringLiteral("Tab in the field completes"),
              input->property("text").toString());
        QTest::keyClick(window, Qt::Key_Return);
        QTest::qWait(300);
        check(dir() == base + QStringLiteral("/Alpha"), QStringLiteral("completed path + Enter goes there"), dir());

        app.exit(failures ? 1 : 0);
    });
    return app.exec();
}
