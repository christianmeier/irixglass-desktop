// Terminal tabs: a shell and Neovim in tabs (libvterm + KPty), on both sides of the split view.
// Typing reaches the program (including Ctrl+C), its output shows, and the tab closes when the
// program exits. Off-screen, run with `make test`.
#include "fm.h"
#include "terminal.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QFontMetricsF>
#include <QKeyEvent>
#include <QMimeData>
#include <QPainter>
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
    std::printf("%s %-58s %s\n", ok ? "PASS" : "FAIL", qPrintable(what), ok ? "" : qPrintable("got: " + got.left(300)));
    failures += ok ? 0 : 1;
}
bool waitFor(const std::function<bool()> &cond, int ms = 8000)
{
    for (int t = 0; t < ms && !cond(); t += 50)
        QTest::qWait(50);
    return cond();
}
// IRIXFM_TERM_BENCH=<dir>: timings instead of the tests. Throughput (big outputs until the prompt
// is back), paint() time for a full 200×60 screen, scrollback memory, and PNGs of the painted
// screens in <dir> to compare before / after a change.
class BenchTerm : public TerminalView
{
public:
    int paints = 0;
    qint64 paintNs = 0;
    void paint(QPainter *p) override
    {
        QElapsedTimer t;
        t.start();
        TerminalView::paint(p);
        paintNs += t.nsecsElapsed();
        ++paints;
    }
};

long rssKb()
{
    QFile f(QStringLiteral("/proc/self/status"));
    if (!f.open(QIODevice::ReadOnly))
        return 0;
    for (const QByteArray &l : f.readAll().split('\n'))
        if (l.startsWith("VmRSS:"))
            return l.mid(6).trimmed().split(' ').value(0).toLong();
    return 0;
}

int runBench(const QString &out)
{
    QDir().mkpath(out);
    QQuickWindow window;
    QFont font(QStringLiteral("Noto Sans Mono"));
    font.setPointSizeF(12);
    const QFontMetricsF fm(font);
    const qreal cw = fm.horizontalAdvance(QLatin1Char('M')), ch = std::ceil(fm.lineSpacing());
    const QSize size(int(8 + 200 * cw + 1), int(6 + 60 * ch + 1));   // 200×60 cells
    window.resize(size);
    window.show();
    int seqNo = 0;
    auto newTerm = [&] {
        auto *t = new BenchTerm;
        t->setParentItem(window.contentItem());
        t->setSize(size);
        t->setProperty("program", QStringLiteral("/bin/bash"));
        t->setProperty("arguments", QStringList{QStringLiteral("--norc"), QStringLiteral("--noprofile")});
        t->start();
        waitFor([&] { return t->screenText().contains(QStringLiteral("fm$")); });
        return t;
    };
    // Runs a command and waits until the marker it prints after it shows. Looks every 10 ms from a
    // timer, so the event loop runs freely in between, as in the app.
    auto run = [&](BenchTerm *t, const QString &cmd, int ms = 60000) {
        const int n = ++seqNo;
        const QString mark = QStringLiteral("DONE%1X").arg(n + 1);
        t->sendText(cmd + QStringLiteral("; echo DONE$((%1+1))X\r").arg(n));
        QElapsedTimer e;
        e.start();
        QEventLoop loop;
        QTimer poll;
        QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
            if (e.elapsed() > ms || t->screenText().contains(mark))
                loop.quit();
        });
        poll.start(10);
        loop.exec();
        return e.elapsed();
    };
    auto paintAvg = [&](BenchTerm *t, const QString &name, qreal dpr = 1) {
        QImage img(size * dpr, QImage::Format_ARGB32_Premultiplied);
        img.setDevicePixelRatio(dpr);   // the user's screen is at 1.5x
        const int n = 200;
        QElapsedTimer e;
        e.start();
        for (int i = 0; i < n; ++i) {
            img.fill(Qt::transparent);
            QPainter p(&img);
            t->TerminalView::paint(&p);
        }
        const double us = e.nsecsElapsed() / 1000.0 / n;
        img.save(out + QLatin1Char('/') + name + QStringLiteral(".png"));
        return us;
    };
    std::printf("bench: %d×%d cells, %dx%d px\n", 200, 60, size.width(), size.height());

    // memory: 12000 lines of colourful output (ls --color) fill the 10000-line scrollback
    {
        auto *t = newTerm();
        const long started = rssKb();
        run(t, QStringLiteral("for i in $(seq 1 30); do ls --color=always -la /usr/bin; done | head -n 12000"));
        QTest::qWait(200);
        std::printf("bench: memory, 10000-line scrollback (ls --color, %d cols): %+ld KB RSS\n", t->columns(), rssKb() - started);
        delete t;
    }
    QTemporaryDir tmp;
    const QString big = tmp.path() + QStringLiteral("/big.txt");
    {
        QFile f(big);
        if (f.open(QIODevice::WriteOnly))
            for (int i = 0; i < 100000; ++i)
                f.write(QStringLiteral("%1 the quick brown fox jumps over the lazy dog, again and again and again\n").arg(i, 6).toUtf8());
    }
    auto *t = newTerm();
    struct Case { const char *name; QString cmd; };
    const QList<Case> cases = {
        {"seq 1 200000", QStringLiteral("seq 1 200000")},
        {"cat 100000 lines (8 MB)", QStringLiteral("cat ") + big},
        {"ls --color -la /usr/bin x5", QStringLiteral("for i in 1 2 3 4 5; do ls --color=always -la /usr/bin; done")},
    };
    for (const Case &c : cases) {
        t->paints = 0;
        t->paintNs = 0;
        const qint64 ms = run(t, c.cmd);
        std::printf("bench: throughput %-28s %6lld ms  (%d paints, %.0f us each)\n", c.name, ms, t->paints,
                    t->paints ? t->paintNs / 1000.0 / t->paints : 0.0);
    }
    // paint: a typical colourful screen, the worst case (colours change every cell), plain text
    run(t, QStringLiteral("clear; ls --color=always -la /usr/bin | tail -n 58"));
    std::printf("bench: paint, ls --color -la /usr/bin screen: %.0f us\n", paintAvg(t, QStringLiteral("ls")));
    std::printf("bench: paint, the same at 1.5x: %.0f us\n", paintAvg(t, QStringLiteral("ls-1.5x"), 1.5));
    run(t, QStringLiteral("clear; for r in $(seq 0 57); do for c in $(seq 0 199); do printf '\\e[3%d;4%d%sm%x' $((c%8)) $(((c/8+r)%8)) $([ $((c%3)) = 0 ] && echo ';1') $((c%16)); done; printf '\\e[0m\\n'; done"));
    std::printf("bench: paint, every cell a different colour: %.0f us\n", paintAvg(t, QStringLiteral("grid")));
    run(t, QStringLiteral("clear; for i in $(seq 1 57); do printf '%0.s-' $(seq 1 199); echo; done"));
    std::printf("bench: paint, plain text: %.0f us\n", paintAvg(t, QStringLiteral("plain")));
    run(t, QStringLiteral("clear; fastfetch --pipe false"));
    paintAvg(t, QStringLiteral("fastfetch"));
    window.grabWindow().save(out + QStringLiteral("/window.png"));   // as rendered, with partial updates
    // scrolled back into the history (painted from the scrollback, not the screen)
    for (int i = 0; i < 20; ++i) {
        QKeyEvent k(QEvent::KeyPress, Qt::Key_PageUp, Qt::ShiftModifier);
        QCoreApplication::sendEvent(t, &k);
    }
    std::printf("bench: paint, scrolled back %d lines: %.0f us\n", t->scrollOffset(), paintAvg(t, QStringLiteral("history")));
    delete t;
    return 0;
}
} // namespace

int main(int argc, char *argv[])
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    if (qEnvironmentVariableIsSet("IRIXFM_TERM_BENCH")) {
        qputenv("PS1", "fm$ ");
        QApplication app(argc, argv);
        return runBench(qEnvironmentVariable("IRIXFM_TERM_BENCH"));
    }
    qputenv("IRIXFM_SHELL", "/bin/bash");          // a plain, predictable shell for the test
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
            check(qAbs(win->property("termFontSize").toReal() - 12) < .01, QStringLiteral("the size is kept in the settings"),
                  QString::number(win->property("termFontSize").toReal()));
            check(afterMinus < pt * 1.5 * 1.1 && qAbs(t->property("fontPointSize").toReal() - 12) < .01,
                  QStringLiteral("Ctrl+- / Ctrl+0 shrink and reset the font"), QString::number(afterMinus));
            // the bottom thumbwheel sizes the terminal's text while it shows (not the icons)
            {
                auto *wheel = win->findChild<QQuickItem *>(QStringLiteral("thumbwheel"));
                const double icons = win->property("wheelValue").toDouble();
                wheel->setProperty("value", .5);
                QTest::qWait(100);
                check(qAbs(t->property("fontPointSize").toReal() - 18) < .05 && qAbs(win->property("wheelValue").toDouble() - icons) < 1e-6,
                      QStringLiteral("the thumbwheel sets the terminal's text size, not the icon size"),
                      QString::number(t->property("fontPointSize").toReal()));
                auto *home = win->findChild<QQuickItem *>(QStringLiteral("homeButton"));
                QTest::mouseClick(window, Qt::LeftButton, {}, home->mapToScene(QPointF(home->width() / 2, home->height() / 2)).toPoint());
                QTest::qWait(100);
                QTest::qWait(400);
                check(qAbs(t->property("fontPointSize").toReal() - 12) < .05 && qAbs(wheel->property("value").toDouble() - .25) < 1e-3,
                      QStringLiteral("…its home button: 12 pt when the output is narrow"), QString::number(t->property("fontPointSize").toReal()));
                t->sendText(QStringLiteral("clear; printf '%0.s=' $(seq 1 180); echo\r"));
                QTest::qWait(500);
                check(t->columns() < 180, QStringLiteral("(a 180-character line wraps at 12 pt)"), QString::number(t->columns()));
                QTest::mouseClick(window, Qt::LeftButton, {}, home->mapToScene(QPointF(home->width() / 2, home->height() / 2)).toPoint());
                QTest::qWait(600);
                // (wrapped lines don't re-flow: libvterm's reflow aborts; printed again, it fits)
                t->sendText(QStringLiteral("clear; printf '%0.s=' $(seq 1 180); echo\r"));
                waitFor([&] { return t->screenText().contains(QString(180, QLatin1Char('='))); }, 3000);
                check(t->columns() >= 181 && t->property("fontPointSize").toReal() < 12 && t->screenText().contains(QString(180, QLatin1Char('='))),
                      QStringLiteral("…and smaller, so the output fits without line breaks"),
                      QString::number(t->columns()) + " cols, " + QString::number(t->property("fontPointSize").toReal()) + " pt");
                t->setFontSize(12);
                t->forceActiveFocus();
            }
            // box drawing fills the cell: a column of "│" is one unbroken line (tmux's pane borders
            // came out dashed from the font's glyph)
            {
                t->sendText(QStringLiteral("clear; for i in $(seq 1 10); do printf '\\u2502\\n'; done\r"));
                QTest::qWait(600);
                const QImage img = window->grabWindow();
                const qreal dpr = img.devicePixelRatio();
                // the first column's "│": cell 0, from row 1 (row 0 holds the command) for 8 rows
                const QPointF cell = t->mapToScene(QPointF(4 + 0.5 * t->width() / t->columns() * 0, 3));
                int bestX = -1, bestRun = 0;
                const int x0 = int(t->mapToScene(QPointF(4, 0)).x() * dpr), y0 = int((cell.y() + 2.2 * t->height() / t->rows()) * dpr);
                const int y1 = int((cell.y() + 8.8 * t->height() / t->rows()) * dpr);
                for (int x = x0; x < x0 + int(12 * dpr); ++x) {
                    int run = 0;
                    for (int y = y0; y < y1; ++y)
                        run += qGray(img.pixel(x, y)) > 180 ? 1 : 0;
                    if (run > bestRun) { bestRun = run; bestX = x; }
                }
                check(bestX >= 0 && bestRun == y1 - y0, QStringLiteral("a column of \"│\" is one unbroken line"),
                      QString::number(bestRun) + " of " + QString::number(y1 - y0) + " px lit at x " + QString::number(bestX));
            }
            // stress: random resizes (window and text size) while long lines wrap — libvterm's reflow
            // used to abort the whole program here
            {
                t->sendText(QStringLiteral("clear; for i in $(seq 1 40); do printf '%0.s#' $(seq 1 $((RANDOM % 300))); echo; done\r"));
                QTest::qWait(300);
                const QSizeF orig = t->size();
                quint32 seed = 12345;
                auto rnd = [&](int n) { seed = seed * 1103515245 + 12345; return int((seed >> 16) % n); };
                for (int i = 0; i < 300; ++i) {
                    if (i % 3 == 0)
                        t->setFontSize(6 + rnd(25));
                    else
                        t->setSize(QSizeF(40 + rnd(int(orig.width())), 30 + rnd(int(orig.height()))));
                    if (i % 25 == 0) {
                        t->sendText(QStringLiteral("printf '%0.s~' $(seq 1 250); echo\r"));
                        QTest::qWait(30);
                    }
                    QCoreApplication::processEvents();
                }
                t->setSize(orig);
                t->setFontSize(12);
                t->sendText(QStringLiteral("clear; echo still-alive\r"));
                check(waitFor([&] { return t->screenText().contains(QStringLiteral("still-alive")); }), QStringLiteral("300 random resizes while lines wrap: no crash"));
            }
            // two-finger touchpad scrolling: many small pixel deltas scroll back through the history
            {
                t->sendText(QStringLiteral("for i in $(seq 1 150); do echo line-$i; done\r"));
                QTest::qWait(600);
                const QPointF mid = t->mapToScene(QPointF(t->width() / 2, t->height() / 2));
                QPointingDevice touchpad(QStringLiteral("touchpad"), 42, QInputDevice::DeviceType::TouchPad, QPointingDevice::PointerType::Finger,
                                         QInputDevice::Capability::Position | QInputDevice::Capability::PixelScroll, 2, 3);
                auto swipe = [&](int dy, int n) {
                    for (int i = 0; i < n; ++i) {
                        QWheelEvent ev(mid, window->mapToGlobal(mid), QPoint(0, dy), QPoint(0, dy * 3), Qt::NoButton, Qt::NoModifier,
                                       i == 0 ? Qt::ScrollBegin : Qt::ScrollUpdate, false, Qt::MouseEventNotSynthesized, &touchpad);
                        QCoreApplication::sendEvent(window, &ev);
                    }
                };
                swipe(4, 60);   // 240 px of small steps
                check(t->scrollOffset() >= 10, QStringLiteral("touchpad scrolling goes back through the output"), QString::number(t->scrollOffset()));
                swipe(-4, 200);
                check(t->scrollOffset() == 0, QStringLiteral("…and forward to the prompt again"), QString::number(t->scrollOffset()));
            }
            // navigating the folder view (path bar, ‹ ↑) moves the shell there
            {
                const QString sub = base + QStringLiteral("/sub dir");
                QDir().mkpath(sub);
                QMetaObject::invokeMethod(win, "go", Q_ARG(QVariant, sub), Q_ARG(QVariant, false));
                t->sendText(QStringLiteral("clear; pwd\r"));
                check(waitFor([&] { return t->screenText().contains(QStringLiteral("\n") + sub) || t->screenText().startsWith(sub); }),
                      QStringLiteral("changing the folder cds the shell there"), t->screenText());
                t->sendText(QStringLiteral("sleep 3\r"));
                QTest::qWait(300);
                check(!t->changeDirectory(base), QStringLiteral("…but not while a program runs in it"));
                QTest::keyClick(window, Qt::Key_C, Qt::ControlModifier);
                QTest::qWait(300);
                // …and the other way round: cd in the shell, the path bar follows
                t->sendText(QStringLiteral("cd ..\r"));
                check(waitFor([&] { return win->property("dir").toString() == base; }, 3000),
                      QStringLiteral("cd in the shell moves the path bar along"), win->property("dir").toString());
                t->sendText(QStringLiteral("cd 'sub dir'\r"));
                check(waitFor([&] { return win->property("dir").toString() == sub; }, 3000) && win->property("activeTermId").toInt() != 0,
                      QStringLiteral("…every time, and the terminal stays in front"), win->property("dir").toString());
            }
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
            // as a real drag arrives: it enters the window elsewhere (the sidebar), then moves over the terminal
            {
                QMimeData m2;
                m2.setUrls({QUrl::fromLocalFile(odd)});
                t->sendText(QStringLiteral("clear; printf '{%s}\\n' "));
                const QPoint side(60, 300);
                QDragEnterEvent e1(side, Qt::CopyAction | Qt::MoveAction, &m2, Qt::LeftButton, {});
                QCoreApplication::sendEvent(window, &e1);
                for (const QPoint p : {side + QPoint(100, 0), at - QPoint(50, 0), at}) {
                    QDragMoveEvent mv(p, Qt::CopyAction | Qt::MoveAction, &m2, Qt::LeftButton, {});
                    QCoreApplication::sendEvent(window, &mv);
                    QTest::qWait(30);
                }
                QDropEvent dr(at, Qt::CopyAction | Qt::MoveAction, &m2, Qt::LeftButton, {});
                QCoreApplication::sendEvent(window, &dr);
                t->sendText(QStringLiteral("\r"));
                check(waitFor([&] { return t->screenText().contains("{" + odd + "}"); }) && dr.dropAction() == Qt::CopyAction && QFileInfo::exists(odd),
                      QStringLiteral("a drag that comes in over the sidebar and ends on the terminal types the path"), t->screenText());
            }
            t->sendText(QStringLiteral("exit\r"));
            check(waitFor([&] { return tabs().size() == 1; }), QStringLiteral("exiting the shell closes its tab"));
            check(win->property("activeTermId").toInt() == 0, QStringLiteral("…and the folder view is back"));
        }

        // only damaged cells are repainted: what's on screen must still match a full paint
        {
            QQuickWindow w2;
            w2.setColor(Qt::black);
            w2.resize(700, 400);
            w2.show();
            auto *c = new TerminalView(w2.contentItem());
            c->setSize(QSizeF(700, 400));
            c->setProperty("program", QStringLiteral("/bin/bash"));
            c->setProperty("arguments", QStringList{QStringLiteral("--norc"), QStringLiteral("--noprofile")});
            c->start();
            c->forceActiveFocus();
            waitFor([&] { return c->screenText().contains(QStringLiteral("fm$")); });
            c->sendText(QStringLiteral("ls --color=always -la /usr/bin | tail -n 8; printf '\\e[1;31mbold red\\e[0m\\n'\r"));
            QTest::qWait(400);
            for (const char *typed : {"echo ab", "\x7f\x7f", "xyz \x1b[D\x1b[D", "q"}) {   // typing, erasing, cursor moves
                c->sendText(QString::fromLatin1(typed));
                QTest::qWait(120);
            }
            QTest::qWait(200);
            const QImage shown = w2.grabWindow().convertToFormat(QImage::Format_RGB32);
            QImage full(shown.size(), QImage::Format_RGB32);
            full.setDevicePixelRatio(shown.devicePixelRatio());
            full.fill(Qt::black);
            {
                QPainter p(&full);
                p.setRenderHint(QPainter::Antialiasing);
                c->paint(&p);
            }
            int differ = 0;
            for (int y = 0; y < full.height(); ++y)
                for (int x = 0; x < full.width(); ++x)
                    differ += qAbs(qGray(full.pixel(x, y)) - qGray(shown.pixel(x, y))) > 24;
            check(!shown.isNull() && differ < 50, QStringLiteral("partial repaints show the same as a full paint"), QString::number(differ) + " pixels differ");
            delete c;
            w2.close();
            window->requestActivate();
            (void)QTest::qWaitForWindowActive(window);
        }

        // crashes from the user's core dumps
        {
            auto *c = new TerminalView;
            c->setParentItem(window->contentItem());
            c->setSize(QSizeF(800, 500));
            c->setProperty("program", QStringLiteral("/bin/bash"));
            c->setProperty("arguments", QStringList{QStringLiteral("--norc"), QStringLiteral("--noprofile")});
            c->start();
            waitFor([&] { return c->screenText().contains(QStringLiteral("fm$")); });
            // libvterm aborted when re-flowing pushed the line the cursor ends in off the top
            c->sendText(QStringLiteral("printf '%0.s=' $(seq 1 3000); sleep 30\r"));
            QTest::qWait(500);
            c->setFontSize(48);
            c->setFontSize(30);
            c->setSize(QSizeF(0, 0));   // a hidden tab
            c->fitWidth();
            QTest::qWait(400);
            c->setSize(QSizeF(800, 500));
            c->setFontSize(12);
            check(c->running() && c->columns() > 10 && c->screenText().contains(QStringLiteral("=====")),
                  QStringLiteral("re-sizing with a long wrapped line at the cursor doesn't crash"), c->screenText());
            // deleting a terminal whose shell ignores SIGTERM (interactive bash) crashed in ~KPtyProcess
            delete c;
            check(true, QStringLiteral("closing a terminal with a running shell doesn't crash"));
        }

        // Folder names can't run commands when the shell follows the folder view (cd): quoting
        // for bash and fish (fish reads \\ and \' inside '…'), and names with control characters
        // (Ctrl+C would cancel the line and run the rest) aren't typed at all.
        {
            QStringList shells{QStringLiteral("/bin/bash")};
            const QString fishBin = QStandardPaths::findExecutable(QStringLiteral("fish"));
            if (!fishBin.isEmpty()) {   // a wrapper named fish: without the user's config
                QDir().mkpath(base + QStringLiteral("/shells"));
                QFile w(base + QStringLiteral("/shells/fish"));
                if (w.open(QIODevice::WriteOnly))
                    w.write("#!/bin/sh\nexec " + fishBin.toUtf8() + " --no-config \"$@\"\n");
                w.close();
                w.setPermissions(w.permissions() | QFileDevice::ExeOwner);
                shells << w.fileName();
            }
            const QByteArray oldShell = qgetenv("IRIXFM_SHELL");
            int n = 0;
            for (const QString &sh : shells) {
                const QString safe = base + QStringLiteral("/cdtest%1").arg(++n);
                const QString quoteDir = safe + QStringLiteral("/x\\'; touch INJECTED; echo \\'");
                const QString ctrlDir = safe + QStringLiteral("/c\x03 touch INJECTED #");
                QDir().mkpath(quoteDir);
                QDir().mkpath(ctrlDir);
                qputenv("IRIXFM_SHELL", sh.toUtf8());
                auto *c = new TerminalView;
                c->setParentItem(window->contentItem());
                c->setSize(QSizeF(800, 500));
                c->setProperty("workingDirectory", safe);
                c->start();
                QTest::qWait(1500);   // the prompt
                const QString name = QFileInfo(sh).fileName();
                check(c->changeDirectory(quoteDir),
                      name + QStringLiteral(": cd into a folder with quotes and backslashes"));
                QTest::qWait(1000);
                check(!c->changeDirectory(ctrlDir), name + QStringLiteral(": a name with a control character isn't typed"));
                QTest::qWait(800);
                check(!QFileInfo::exists(safe + QStringLiteral("/INJECTED")) && !QFileInfo::exists(quoteDir + QStringLiteral("/INJECTED")),
                      name + QStringLiteral(": …and no folder name ran a command"));
                delete c;
            }
            qputenv("IRIXFM_SHELL", oldShell);
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
