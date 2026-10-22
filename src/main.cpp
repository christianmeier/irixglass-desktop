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
#include <QCryptographicHash>
#include <QLocalServer>
#include <QLocalSocket>
#include <QQmlComponent>
#include <QPointer>
#include <chrono>
#include <cstdlib>
#include <functional>
#include <thread>
#include <unistd.h>
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

    auto folderOf = [](QString start) {
        if (start.startsWith(QLatin1String("file://")))
            start = QUrl(start).toLocalFile();
        const QFileInfo info(start);
        return info.isDir() ? info.absoluteFilePath() : info.exists() ? info.absolutePath() : QDir::homePath();
    };
    const QString start = folderOf(app.arguments().value(1, QDir::homePath()));

    // One fm process: launching fm again (dock, xdg-open, a terminal) hands the folder to the running
    // one, which opens a window, instead of starting a second process (~200 MB more, and a second web
    // profile on the same storage, which the first one has locked). The socket name includes this
    // binary's path, so a development build and the installed one don't talk to each other.
    // Benchmarks, snapshots and off-screen runs always run on their own (IRIXFM_SINGLE=1: test it anyway).
    const bool alone = !qEnvironmentVariableIsSet("IRIXFM_SINGLE")
        && (qEnvironmentVariableIsSet("IRIXFM_BENCH") || qEnvironmentVariableIsSet("IRIXFM_SNAPSHOT")
            || QGuiApplication::platformName() == QLatin1String("offscreen"));
    const QString serverName = QStringLiteral("irixfm-%1-%2").arg(getuid())
        .arg(QString::fromLatin1(QCryptographicHash::hash(QCoreApplication::applicationFilePath().toUtf8(), QCryptographicHash::Md5).toHex().left(8)));
    if (!alone) {
        QLocalSocket other;
        other.connectToServer(serverName);
        if (other.waitForConnected(300)) {
            other.write((app.arguments().size() < 2 ? QString() : start).toUtf8() + '\n');
            other.waitForBytesWritten(300);
            other.disconnectFromServer();
            return 0;
        }
    }

    // Without a folder argument (dock, menu) the last session's tabs come back.
    Fm fm(start, assets, nullptr, app.arguments().size() < 2);
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("Fm"), &fm);

    // More windows: each has its own Fm (start folder, its window effects) in a child context.
    QQmlComponent windowComponent(&engine, QUrl::fromLocalFile(assets + QStringLiteral("/qml/Main.qml")));
    QList<QPointer<QObject>> extraWindows;
    std::function<void(const QString &)> openWindow = [&](const QString &dir) {
        auto *wfm = new Fm(dir.isEmpty() ? QDir::homePath() : dir, assets, &app, false);
        auto *ctx = new QQmlContext(engine.rootContext(), wfm);
        ctx->setContextProperty(QStringLiteral("Fm"), wfm);
        QObject *w = windowComponent.create(ctx);
        if (!w) {
            qWarning() << windowComponent.errorString();
            delete wfm;
            return;
        }
        extraWindows << w;
        QObject::connect(wfm, &Fm::newWindowRequested, &app, openWindow);
        if (qEnvironmentVariableIsSet("IRIXFM_SINGLE"))
            std::fprintf(stderr, "irixfm: window opened on %s\n", qPrintable(wfm->property("startDir").toString()));
        // a closed window goes away with its Fm (the first one lives as long as fm does)
        if (auto *qw = qobject_cast<QQuickWindow *>(w))
            QObject::connect(qw, &QWindow::visibleChanged, wfm, [qw, wfm](bool visible) {
                if (!visible) {
                    qw->deleteLater();
                    wfm->deleteLater();
                }
            });
    };
    QObject::connect(&fm, &Fm::newWindowRequested, &app, openWindow);
    QLocalServer server;
    if (!alone) {
        QLocalServer::removeServer(serverName);   // a stale socket from a crashed fm
        server.listen(serverName);
        QObject::connect(&server, &QLocalServer::newConnection, &app, [&] {
            while (QLocalSocket *c = server.nextPendingConnection()) {
                QObject::connect(c, &QLocalSocket::readyRead, c, [c, &openWindow, folderOf] {
                    if (!c->canReadLine())
                        return;
                    const QString dir = QString::fromUtf8(c->readLine()).trimmed();
                    openWindow(dir.isEmpty() ? QDir::homePath() : folderOf(dir));
                    c->disconnectFromServer();
                });
                QObject::connect(c, &QLocalSocket::disconnected, c, &QObject::deleteLater);
            }
        });
    }
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
    const int rc = app.exec();
    // Shutting down: windows (and their web tabs) go before the engine and QApplication, as Qt
    // WebEngine's own clean-up waits for every web view to be gone. And if anything still hangs on
    // the way out, end the process after 5 s (the settings are saved by then): fm processes from
    // older builds hung in WebEngine's shutdown and stayed in the background, holding gigabytes.
    for (const QPointer<QObject> &w : std::as_const(extraWindows))
        delete w.data();
    std::thread([] {
        std::this_thread::sleep_for(std::chrono::seconds(5));
        std::_Exit(0);
    }).detach();
    return rc;
}
