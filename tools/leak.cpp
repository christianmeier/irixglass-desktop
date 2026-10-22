// Leak test: repeat navigation, tab, terminal-tab or web-tab open/close cycles off-screen and print
// the RSS after each round (10 cycles per round; nav: 20 rounds, the others 6). Growth that keeps
// going round after round is a leak; a step in the first rounds is caches warming up.
//   make build/leak
//   XDG_CONFIG_HOME=$(mktemp -d) XDG_CACHE_HOME=$(mktemp -d) XDG_DATA_HOME=$(mktemp -d)
//       QT_QPA_PLATFORM=offscreen build/leak <repo> nav|tabs|term|web
// Terminals run bash without history (IRIXFM_SHELL); web tabs run Chromium with --disable-gpu.
#include "fm.h"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTest>
#include <QTimer>

#include <cstdio>

static long rssKb()
{
    QFile f("/proc/self/status");
    if (!f.open(QIODevice::ReadOnly))
        return 0;
    for (const QByteArray &l : f.readAll().split('\n'))
        if (l.startsWith("VmRSS:"))
            return l.mid(6).trimmed().split(' ').first().toLong();
    return 0;
}

int main(int argc, char *argv[])
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    const QString mode = argc > 2 ? argv[2] : "";
    if (argc < 3 || !QStringList{"nav", "tabs", "term", "web"}.contains(mode)) {
        std::fprintf(stderr, "usage: %s <repo> nav|tabs|term|web\n", argv[0]);
        return 2;
    }
    qputenv("IRIXFM_SHELL", "/bin/bash");
    qputenv("HISTFILE", "/dev/null");
    qputenv("QTWEBENGINE_CHROMIUM_FLAGS", "--disable-gpu");
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    QApplication app(argc, argv);
    app.setOrganizationName("irixfm-leak");
    QQuickStyle::setStyle("org.kde.desktop");
    const QString repo = argv[1];
    Fm fm(QDir::homePath(), repo);
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("Fm", &fm);
    engine.load(QUrl::fromLocalFile(repo + "/qml/Main.qml"));
    if (engine.rootObjects().isEmpty())
        return 1;
    QObject *win = engine.rootObjects().constFirst();
    auto *window = qobject_cast<QQuickWindow *>(win);
    QTimer::singleShot(1500, &app, [&] {
        const long start = rssKb();
        std::printf("%s start %ld MB\n", qPrintable(mode), start / 1024);
        for (int round = 1; round <= (mode == "nav" ? 20 : 6); ++round) {
            for (int i = 0; i < 10; ++i) {
                if (mode == "nav") {
                    for (const char *d : {"/usr/bin", "/usr/lib", "/usr/share/icons"}) {
                        QMetaObject::invokeMethod(win, "go", Q_ARG(QVariant, QString(d)), Q_ARG(QVariant, false));
                        QTest::qWait(150);
                        window->grabWindow();
                    }
                } else if (mode == "tabs") {
                    QMetaObject::invokeMethod(win, "newTab", Q_ARG(QVariant, QString("/usr/bin")), Q_ARG(QVariant, true));
                    QTest::qWait(100);
                    window->grabWindow();
                    QTest::keyClick(window, Qt::Key_W, Qt::ControlModifier);
                    QTest::qWait(50);
                } else {   // term, web: open the tab, render it, Ctrl+Shift+W closes it
                    QMetaObject::invokeMethod(win, "openTerminal", Q_ARG(QVariant, QString(mode == "term" ? "shell" : "web")));
                    QTest::qWait(mode == "term" ? 200 : 300);
                    window->grabWindow();
                    QTest::keyClick(window, Qt::Key_W, Qt::ControlModifier | Qt::ShiftModifier);
                    QTest::qWait(mode == "term" ? 100 : 150);
                }
            }
            QTest::qWait(300);
            std::printf("%s round %d: %ld MB (+%ld)\n", qPrintable(mode), round, rssKb() / 1024, (rssKb() - start) / 1024);
        }
        app.quit();
    });
    return app.exec();
}
