#include "fm.h"

#include <QApplication>
#include <KSignalHandler>
#include <csignal>
#include <QIcon>
#include <QDir>
#include <QFileInfo>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>
#include <QElapsedTimer>
#include <QQuickItem>
#include <cstdio>
#include <ctime>

int main(int argc, char *argv[])
{
    // Web tabs (BrowserView.qml, Qt WebEngine) need shared OpenGL contexts, set before the application.
    // This is all QtWebEngineQuick::initialize() amounts to here; calling it would link Qt WebEngine
    // into fm, and loading that library costs ~35 ms before main() on every start. Qt WebEngine now
    // loads with the QtWebEngine QML module, on the first web tab.
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("irixfm"));
    app.setOrganizationName(QStringLiteral("irixfm"));
    app.setApplicationDisplayName(QStringLiteral("fm"));
    app.setDesktopFileName(QStringLiteral("irixfm"));
    // The dock and window switcher show the icon theme's normal file manager icon, not the IRIX one.
    app.setWindowIcon(QIcon::fromTheme(QStringLiteral("system-file-manager")));
    QQuickStyle::setStyle(QStringLiteral("org.kde.desktop"));
    // Quit cleanly on SIGTERM/SIGINT (pkill, logout) so the settings (sidebar, sizes, …) get saved.
    KSignalHandler::self()->watchSignal(SIGTERM);
    KSignalHandler::self()->watchSignal(SIGINT);
    QObject::connect(KSignalHandler::self(), &KSignalHandler::signalReceived, &app, [] { QCoreApplication::quit(); });

    // QML and icons live next to the binary in development, in ~/.local/share/irixfm when installed.
    QString assets = qEnvironmentVariable("IRIXFM_ASSETS");
    if (assets.isEmpty()) {
        const QString local = QCoreApplication::applicationDirPath();
        assets = QFileInfo::exists(local + QStringLiteral("/qml/Main.qml")) ? local
                                                                             : QDir::homePath() + QStringLiteral("/.local/share/irixfm");
    }

    QString start = app.arguments().value(1, QDir::homePath());
    if (start.startsWith(QLatin1String("file://")))
        start = QUrl(start).toLocalFile();
    const QFileInfo startInfo(start);
    start = startInfo.isDir() ? startInfo.absoluteFilePath() : startInfo.exists() ? startInfo.absolutePath() : QDir::homePath();

    // Without a folder argument (dock, menu) the last session's tabs come back.
    Fm fm(start, assets, nullptr, app.arguments().size() < 2);
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("Fm"), &fm);
    QElapsedTimer clock;
    clock.start();
    engine.load(QUrl::fromLocalFile(assets + QStringLiteral("/qml/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return 1;
    // IRIXFM_BENCH=1: print how long start-up takes (QML loaded, first frame, the folder's items in
    // the view), then scroll the view to the end and back frame by frame, and quit. For measuring
    // optimisations: run it a few times, off-screen (QT_QPA_PLATFORM=offscreen) for steady numbers.
    // IRIXFM_BENCH=split opens the split view on the same folder and scrolls its side instead.
    if (qEnvironmentVariableIsSet("IRIXFM_BENCH")) {
        auto *w = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
        const bool split = qEnvironmentVariable("IRIXFM_BENCH") == QLatin1String("split");
        if (split)
            QMetaObject::invokeMethod(engine.rootObjects().constFirst(), "toggleSplit");
        const qint64 loaded = clock.elapsed();
        auto *firstFrame = new qint64(-1);
        QObject::connect(w, &QQuickWindow::frameSwapped, w, [&clock, firstFrame] { if (*firstFrame < 0) *firstFrame = clock.elapsed(); });
        auto *poll = new QTimer(&app);
        auto *lastCount = new int(-1);
        auto *stableSince = new qint64(0);
        poll->setInterval(5);
        QObject::connect(poll, &QTimer::timeout, &app, [=, &engine, &clock, &app] {
            QObject *root = engine.rootObjects().constFirst();
            const bool list = root->property(split ? "splitViewMode" : "viewMode").toInt() == 1;
            auto *view = root->findChild<QQuickItem *>(split ? (list ? QStringLiteral("splitList") : QStringLiteral("splitGrid"))
                                                             : (list ? QStringLiteral("list") : QStringLiteral("grid")));
            const int count = view ? view->property("count").toInt() : 0;
            if (count != *lastCount) {
                *lastCount = count;
                *stableSince = clock.elapsed();
            }
            if (*firstFrame < 0 || clock.elapsed() - *stableSince < 400)
                return;
            poll->stop();
            const qint64 ready = *stableSince;
            // scroll: to the end and back in 60 steps each, rendering every step
            // the GUI thread's CPU time too: steadier than wall time on a busy machine
            const auto cpuMs = [] { timespec t; clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t); return qint64(t.tv_sec) * 1000 + t.tv_nsec / 1000000; };
            const qint64 cpu0 = cpuMs();
            QElapsedTimer s;
            s.start();
            const double end = std::max(0.0, view->property("contentHeight").toDouble() - view->height());
            for (int i = 0; i <= 120; ++i) {
                const double f = i <= 60 ? i / 60.0 : (120 - i) / 60.0;
                view->setProperty("contentY", view->property("originY").toDouble() + end * f);
                w->grabWindow();   // renders the frame
            }
            std::fprintf(stderr, "bench: items=%d qml-loaded=%lldms first-frame=%lldms items-ready=%lldms scroll-121-frames=%lldms cpu=%lldms\n",
                         count, loaded, *firstFrame, ready, s.elapsed(), cpuMs() - cpu0);
            app.quit();
        });
        poll->start();
    }
    // IRIXFM_SNAPSHOT=<file.png>: save the rendered window and quit (for checking the look).
    const QString snapshot = qEnvironmentVariable("IRIXFM_SNAPSHOT");
    if (!snapshot.isEmpty()) {
        QTimer::singleShot(2500, &app, [&] {
            if (auto *w = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst()))
                w->grabWindow().save(snapshot);
            app.quit();
        });
    }
    return app.exec();
}
