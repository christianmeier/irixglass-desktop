// Terminal tabs: a shell and Neovim in tabs (libvterm + KPty), on both sides of the split view.
// Typing reaches the program (including Ctrl+C), its output shows, and the tab closes when the
// program exits. Off-screen, run with `make test`.
#include "fm.h"
#include "terminal.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
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
    std::printf("%s %-58s %s\n", ok ? "PASS" : "FAIL", qPrintable(what), ok ? "" : qPrintable("got: " + got.left(300)));
    failures += ok ? 0 : 1;
}
bool waitFor(const std::function<bool()> &cond, int ms = 8000)
{
    for (int t = 0; t < ms && !cond(); t += 50)
        QTest::qWait(50);
    return cond();
}
} // namespace

int main(int argc, char *argv[])
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    qputenv("SHELL", "/bin/bash");                 // a plain, predictable shell for the test
    qputenv("PS1", "fm$ ");
    qputenv("HISTFILE", "/dev/null");             // keep the test out of the user's shell history
    QApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("irixfm-test"));
    QQuickStyle::setStyle(QStringLiteral("org.kde.desktop"));
    QTemporaryDir tmp;
    const QString base = QDir(tmp.path()).canonicalPath();
    QFile f(base + QStringLiteral("/hello.txt"));
    if (f.open(QIODevice::WriteOnly))
        f.write("line one from hello.txt\n");
    f.close();

    const QString assets = QStringLiteral(IRIXFM_SOURCE_DIR);
    Fm fm(base, assets);
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("Fm"), &fm);
    engine.load(QUrl::fromLocalFile(assets + QStringLiteral("/qml/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return 2;
    QObject *win = engine.rootObjects().constFirst();
    auto *window = qobject_cast<QQuickWindow *>(win);
    auto tabs = [&] { return win->property("tabs").toList(); };
    auto activeTerm = [&]() -> TerminalView * {
        const int id = win->property("activeTermId").toInt();
        return id ? qobject_cast<TerminalView *>(win->property("terminals").toMap().value(QString::number(id)).value<QObject *>()) : nullptr;
    };

    QTimer::singleShot(1000, &app, [&] {
        window->requestActivate();
        (void)QTest::qWaitForWindowActive(window);

        QTest::keyClick(window, Qt::Key_F4);
        QTest::qWait(300);
        TerminalView *t = activeTerm();
        check(tabs().size() == 2 && t && t->running() && t->isVisible(), QStringLiteral("F4 opens a terminal tab running the shell"));
        check(t && t->hasActiveFocus(), QStringLiteral("…and the terminal has the keyboard"));
        if (t) {
            t->sendText(QStringLiteral("echo hello-$((40+2))\r"));
            check(waitFor([&] { return t->screenText().contains(QStringLiteral("hello-42")); }), QStringLiteral("typed commands run and their output shows"),
                  t->screenText());
            t->sendText(QStringLiteral("pwd\r"));
            check(waitFor([&] { return t->screenText().contains(base); }), QStringLiteral("it starts in the folder that was open"), t->screenText());
            t->sendText(QStringLiteral("sleep 30\r"));
            QTest::qWait(300);
            QTest::keyClick(window, Qt::Key_C, Qt::ControlModifier);   // must reach the terminal, not copy files
            t->sendText(QStringLiteral("echo after-ctrl-c\r"));
            check(waitFor([&] { return t->screenText().contains(QStringLiteral("after-ctrl-c")); }, 4000),
                  QStringLiteral("Ctrl+C goes to the program (interrupts sleep)"), t->screenText());
            // pinch on the touchpad: the font follows; fewer columns fit, the shell is told (stty size)
            const qreal pt = t->property("fontPointSize").toReal();
            const int cols = t->columns();
            const QPointF mid = t->mapToScene(QPointF(t->width() / 2, t->height() / 2));
            auto gesture = [&](Qt::NativeGestureType type, qreal value) {
                QNativeGestureEvent ev(type, QPointingDevice::primaryPointingDevice(), 2, mid, mid, window->mapToGlobal(mid), value, {});
                QCoreApplication::sendEvent(window, &ev);
            };
            gesture(Qt::BeginNativeGesture, 0);
            for (int i = 0; i < 10; ++i)
                gesture(Qt::ZoomNativeGesture, .05);
            gesture(Qt::EndNativeGesture, 0);
            QTest::qWait(200);
            check(t->property("fontPointSize").toReal() > pt * 1.5 && t->columns() < cols * .75,
                  QStringLiteral("pinching out makes the terminal font bigger"),
                  QString::number(t->property("fontPointSize").toReal()) + " cols " + QString::number(t->columns()));
            t->sendText(QStringLiteral("stty size\r"));
            check(waitFor([&] { return t->screenText().contains(QStringLiteral(" ") + QString::number(t->columns())); }),
                  QStringLiteral("…and the program gets the new size"), t->screenText());
            QTest::keyClick(window, Qt::Key_Minus, Qt::ControlModifier);
            const qreal afterMinus = t->property("fontPointSize").toReal();
            QTest::keyClick(window, Qt::Key_0, Qt::ControlModifier);
            check(afterMinus < pt * 1.5 * 1.1 && qAbs(t->property("fontPointSize").toReal() - 12) < .01,
                  QStringLiteral("Ctrl+- / Ctrl+0 shrink and reset the font"), QString::number(afterMinus));
            // dropping files types their paths, quoted; nothing is moved
            const QString odd = base + QStringLiteral("/it's a folder");
            QDir().mkpath(odd);
            QMimeData mime;
            mime.setUrls({QUrl::fromLocalFile(odd), QUrl::fromLocalFile(base + QStringLiteral("/hello.txt"))});
            const QPoint at = t->mapToScene(QPointF(t->width() / 2, t->height() / 2)).toPoint();
            t->sendText(QStringLiteral("clear; printf '<%s>\\n' "));
            QDragEnterEvent enter(at, Qt::CopyAction | Qt::MoveAction, &mime, Qt::LeftButton, {});
            QCoreApplication::sendEvent(window, &enter);
            QDragMoveEvent move(at, Qt::CopyAction | Qt::MoveAction, &mime, Qt::LeftButton, {});
            QCoreApplication::sendEvent(window, &move);
            QDropEvent drop(at, Qt::CopyAction | Qt::MoveAction, &mime, Qt::LeftButton, {});
            QCoreApplication::sendEvent(window, &drop);
            t->sendText(QStringLiteral("\r"));
            check(waitFor([&] { return t->screenText().contains("<" + odd + ">") && t->screenText().contains("<" + base + "/hello.txt>"); }),
                  QStringLiteral("dropping files onto the terminal types their quoted paths"), t->screenText());
            check(QFileInfo::exists(odd) && QFileInfo::exists(base + QStringLiteral("/hello.txt")), QStringLiteral("…and leaves the files where they are"));
            t->sendText(QStringLiteral("exit\r"));
            check(waitFor([&] { return tabs().size() == 1; }), QStringLiteral("exiting the shell closes its tab"));
            check(win->property("activeTermId").toInt() == 0, QStringLiteral("…and the folder view is back"));
        }

        // Neovim on the selected file
        win->setProperty("selection", QVariantMap{{base + QStringLiteral("/hello.txt"), true}});
        QTest::keyClick(window, Qt::Key_F4, Qt::ShiftModifier);
        QTest::qWait(300);
        TerminalView *nv = activeTerm();
        check(nv && waitFor([&] { return nv->screenText().contains(QStringLiteral("line one from hello.txt")); }),
              QStringLiteral("Shift+F4 opens the selected file in Neovim"), nv ? nv->screenText() : QString());
        if (nv) {
            nv->sendText(QStringLiteral(":q\r"));
            check(waitFor([&] { return tabs().size() == 1; }), QStringLiteral(":q closes the Neovim tab"));
        }

        // the split side
        QMetaObject::invokeMethod(win, "toggleSplit");
        QTest::qWait(300);
        win->setProperty("activePane", 1);
        QTest::keyClick(window, Qt::Key_F4);
        QTest::qWait(300);
        auto *pane = win->findChild<QQuickItem *>(QStringLiteral("splitPane"));
        const int paneTerm = pane->property("activeTermId").toInt();
        check(pane->property("tabs").toList().size() == 2 && paneTerm != 0 && tabs().size() == 1,
              QStringLiteral("F4 on the split side opens the terminal there"));
        auto *pt = qobject_cast<TerminalView *>(win->property("terminals").toMap().value(QString::number(paneTerm)).value<QObject *>());
        if (pt) {
            pt->sendText(QStringLiteral("exit\r"));
            check(waitFor([&] { return pane->property("tabs").toList().size() == 1; }), QStringLiteral("…and closes its tab there on exit"));
        }
        app.exit(failures ? 1 : 0);
    });
    return app.exec();
}
