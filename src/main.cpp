#include "fm.h"
#include "filemanager1.h"

#include <KStartupInfo>
#include <KWindowSystem>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <memory>

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
    // --dbus-activated: started by the D-Bus daemon for org.freedesktop.FileManager1 (the service
    // file, see the Makefile). No window of its own: the call that started it opens one.
    QStringList args = app.arguments().mid(1);
    const bool dbusActivated = args.removeAll(QStringLiteral("--dbus-activated")) > 0;
    const bool folderGiven = !args.isEmpty();
    const QString start = folderOf(args.value(0, QDir::homePath()));
    const bool debugLog = qEnvironmentVariableIsSet("IRIXFM_SINGLE") || qEnvironmentVariableIsSet("IRIXFM_DBUS");

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
    bool otherFm = false;   // D-Bus activated while an fm (an older build) runs without the D-Bus name
    if (!alone) {
        QLocalSocket other;
        other.connectToServer(serverName);
        if (other.waitForConnected(300)) {
            if (dbusActivated) {
                otherFm = true;
                std::fprintf(stderr, "irixfm: started for org.freedesktop.FileManager1 while another fm runs without it\n");
            } else {
                other.write((folderGiven ? start : QString()).toUtf8() + '\n');
                other.waitForBytesWritten(300);
                other.disconnectFromServer();
                return 0;
            }
        }
    }

    // The first window's Fm is made with it (loadMain), right away or, D-Bus activated, on the first call.
    std::unique_ptr<Fm> mainFm;
    QQmlApplicationEngine engine;

    // More windows: each has its own Fm (start folder, its window effects) in a child context.
    // The component is made with the second window: Main.qml needs the IrixFm types, which the
    // first Fm registers.
    std::unique_ptr<QQmlComponent> windowComponent;
    QList<QPointer<QObject>> extraWindows;
    std::function<QObject *(const QString &)> openWindow = [&](const QString &dir) -> QObject * {
        auto *wfm = new Fm(dir.isEmpty() ? QDir::homePath() : dir, assets, &app, false);
        if (!windowComponent)
            windowComponent = std::make_unique<QQmlComponent>(&engine, QUrl::fromLocalFile(assets + QStringLiteral("/qml/Main.qml")));
        auto *ctx = new QQmlContext(engine.rootContext(), wfm);
        ctx->setContextProperty(QStringLiteral("Fm"), wfm);
        QObject *w = windowComponent->create(ctx);
        if (!w) {
            qWarning() << windowComponent->errorString();
            delete wfm;
            return nullptr;
        }
        extraWindows << w;
        QObject::connect(wfm, &Fm::newWindowRequested, &app, openWindow);
        if (debugLog)
            std::fprintf(stderr, "irixfm: window opened on %s\n", qPrintable(wfm->property("startDir").toString()));
        // a closed window goes away with its Fm (the first one lives as long as fm does)
        if (auto *qw = qobject_cast<QQuickWindow *>(w))
            QObject::connect(qw, &QWindow::visibleChanged, wfm, [qw, wfm](bool visible) {
                if (!visible) {
                    qw->deleteLater();
                    wfm->deleteLater();
                }
            });
        return w;
    };
    // Without a folder argument (dock, menu) the last session's tabs come back.
    auto loadMain = [&](const QString &dir, bool restore) -> QObject * {
        mainFm = std::make_unique<Fm>(dir, assets, nullptr, restore);
        engine.rootContext()->setContextProperty(QStringLiteral("Fm"), mainFm.get());
        QObject::connect(mainFm.get(), &Fm::newWindowRequested, &app, openWindow);
        engine.load(QUrl::fromLocalFile(assets + QStringLiteral("/qml/Main.qml")));
        if (engine.rootObjects().isEmpty())
            return nullptr;
        if (debugLog && dbusActivated)
            std::fprintf(stderr, "irixfm: window opened on %s\n", qPrintable(dir));
        return engine.rootObjects().constFirst();
    };
    auto newWindow = [&](const QString &dir) { return mainFm ? openWindow(dir) : loadMain(dir, false); };

    QLocalServer server;
    if (!alone && !otherFm) {
        QLocalServer::removeServer(serverName);   // a stale socket from a crashed fm
        server.listen(serverName);
        QObject::connect(&server, &QLocalServer::newConnection, &app, [&] {
            while (QLocalSocket *c = server.nextPendingConnection()) {
                QObject::connect(c, &QLocalSocket::readyRead, c, [c, &newWindow, folderOf] {
                    if (!c->canReadLine())
                        return;
                    const QString dir = QString::fromUtf8(c->readLine()).trimmed();
                    newWindow(dir.isEmpty() ? QDir::homePath() : folderOf(dir));
                    c->disconnectFromServer();
                });
                QObject::connect(c, &QLocalSocket::disconnected, c, &QObject::deleteLater);
            }
        });
    }

    // org.freedesktop.FileManager1: "Show in folder" in other apps opens fm (see filemanager1.h).
    // Each call opens a new window, like Finder and Dolphin: the call comes from another app, and a
    // window of its own is what can be raised for it without taking over a tab the user is working
    // in. More folders in one call become tabs of that window. Off-screen, bench and snapshot runs
    // stay off the bus (IRIXFM_DBUS=1: a test opts in, on its own private bus).
    FileManager1 fileManager1;
    auto raise = [](QWindow *w, const QString &startupId) {
        if (!w)
            return;
        // The caller's activation token (Wayland) or startup id (X11) lets KWin focus the window.
        auto activate = [w = QPointer<QWindow>(w), startupId] {
            if (!w)
                return;
            if (!startupId.isEmpty() && KWindowSystem::isPlatformWayland())
                KWindowSystem::setCurrentXdgActivationToken(startupId);
            else if (!startupId.isEmpty() && KWindowSystem::isPlatformX11())
                KStartupInfo::setNewStartupId(w, startupId.toUtf8());
            w->raise();
            KWindowSystem::activateWindow(w);
        };
        // A new window has no surface to activate until it has been shown and drawn once.
        auto once = [activate, done = std::make_shared<bool>(false)] { if (!*done) { *done = true; activate(); } };
        if (auto *qw = qobject_cast<QQuickWindow *>(w))
            QObject::connect(qw, &QQuickWindow::frameSwapped, qw, once, Qt::SingleShotConnection);
        QTimer::singleShot(500, w, once);   // off-screen, or no frame yet
    };
    // URIs (or plain paths, which some apps send) -> existing local paths, in order, once each.
    auto localPaths = [](const QStringList &uris) {
        QStringList out;
        for (const QString &u : uris) {
            const QUrl url = u.startsWith(QLatin1Char('/')) ? QUrl::fromLocalFile(u) : QUrl(u);
            const QString p = url.isLocalFile() ? QDir::cleanPath(url.toLocalFile()) : QString();
            if (p.isEmpty() || !QFileInfo::exists(p))
                std::fprintf(stderr, "irixfm: FileManager1: can't show %s (not a local file that exists)\n", qPrintable(u));
            else if (!out.contains(p))
                out << p;
        }
        return out;
    };
    // groups: [{dir, items}], one per folder; the first folder gets the window, the others tabs.
    auto reveal = [&](const QVariantList &groups, const QString &startupId) -> QObject * {
        QObject *w = newWindow(groups.constFirst().toMap().value(QStringLiteral("dir")).toString());
        if (!w)
            return nullptr;
        w->setProperty("logReveal", debugLog);
        QMetaObject::invokeMethod(w, "revealItems", Q_ARG(QVariant, QVariant(groups)));
        raise(qobject_cast<QWindow *>(w), startupId);
        return w;
    };
    fileManager1.showFolders = [&](const QStringList &uris, const QString &startupId) {
        QVariantList groups;
        QStringList dirs;
        for (const QString &p : localPaths(uris)) {
            const QString dir = folderOf(p);
            if (!dirs.contains(dir)) {
                dirs << dir;
                groups << QVariantMap{{QStringLiteral("dir"), dir}, {QStringLiteral("items"), QStringList()}};
            }
        }
        return !groups.isEmpty() && reveal(groups, startupId);
    };
    fileManager1.showItems = [&](const QStringList &uris, const QString &startupId) {
        QStringList dirs;
        QList<QStringList> items;
        for (const QString &p : localPaths(uris)) {
            const QString dir = p == QLatin1String("/") ? p : QFileInfo(p).absolutePath();
            if (!dirs.contains(dir)) {
                dirs << dir;
                items << QStringList();
            }
            if (p != dir)
                items[dirs.indexOf(dir)] << p;
        }
        QVariantList groups;
        for (int i = 0; i < dirs.size(); ++i)
            groups << QVariantMap{{QStringLiteral("dir"), dirs[i]}, {QStringLiteral("items"), items[i]}};
        if (debugLog)
            for (const QStringList &s : std::as_const(items))
                if (!s.isEmpty())
                    std::fprintf(stderr, "irixfm: FileManager1: showing %s\n", qPrintable(s.join(QLatin1Char(' '))));
        return !groups.isEmpty() && reveal(groups, startupId);
    };
    // Get Info windows, made by any open fm window. Started for it alone (D-Bus activated), fm
    // also opens the first item's folder with the items selected: Get Info windows need a window.
    fileManager1.showItemProperties = [&](const QStringList &uris, const QString &startupId) {
        const QStringList paths = localPaths(uris);
        if (paths.isEmpty())
            return false;
        QObject *host = nullptr;
        if (mainFm)
            host = engine.rootObjects().value(0);
        for (const QPointer<QObject> &w : std::as_const(extraWindows))
            if (!host && w)
                host = w;
        if (!host) {
            const QString first = paths.constFirst();
            const QString dir = first == QLatin1String("/") ? first : QFileInfo(first).absolutePath();
            QStringList items;   // the items in that folder
            for (const QString &p : paths)
                if (p != dir && QFileInfo(p).absolutePath() == dir)
                    items << p;
            host = reveal({QVariantMap{{QStringLiteral("dir"), dir}, {QStringLiteral("items"), items}}}, QString());
        }
        if (!host)
            return false;
        QVariant made;
        QMetaObject::invokeMethod(host, "showInfoFor", Q_RETURN_ARG(QVariant, made), Q_ARG(QVariant, QVariant(paths)));
        QWindow *last = nullptr;
        for (const QVariant &v : made.toList())
            if (auto *iw = qobject_cast<QWindow *>(v.value<QObject *>())) {
                last = iw;
                if (debugLog)
                    std::fprintf(stderr, "irixfm: info window for %s\n", qPrintable(iw->property("path").toString()));
            }
        raise(last, startupId);
        return last != nullptr;
    };
    QElapsedTimer clock;
    clock.start();
    if (!dbusActivated && !loadMain(start, !folderGiven))
        return 1;

    // The name is taken once the first window is up (D-Bus activated: before any window, the call
    // that started fm comes in once it has the name).
    // Not IRIXFM_SINGLE: the single-instance test runs on the user's own session bus.
    const bool onTheBus = qEnvironmentVariableIsSet("IRIXFM_DBUS")
        || !(qEnvironmentVariableIsSet("IRIXFM_BENCH") || qEnvironmentVariableIsSet("IRIXFM_SNAPSHOT")
             || QGuiApplication::platformName() == QLatin1String("offscreen"));
    if (onTheBus) {
        QDBusConnection bus = QDBusConnection::sessionBus();
        const QString name = QStringLiteral("org.freedesktop.FileManager1");
        if (!bus.isConnected()) {
            std::fprintf(stderr, "irixfm: no D-Bus session bus, so no %s\n", qPrintable(name));
        } else {
            bus.registerObject(QStringLiteral("/org/freedesktop/FileManager1"), &fileManager1, QDBusConnection::ExportScriptableSlots);
            // Don't fight over the name: if Dolphin (or another fm) has it, wait in line for it
            // (it comes to fm when that one quits) instead of taking it away.
            const auto reply = bus.interface()->registerService(name, QDBusConnectionInterface::QueueService, QDBusConnectionInterface::DontAllowReplacement);
            if (reply.value() == QDBusConnectionInterface::ServiceRegistered) {
                if (debugLog)
                    std::fprintf(stderr, "irixfm: owns %s\n", qPrintable(name));
            } else {
                const QString owner = bus.interface()->serviceOwner(name).value();
                std::fprintf(stderr, "irixfm: %s belongs to %s (pid %u); fm waits in line for it\n", qPrintable(name),
                             qPrintable(owner), bus.interface()->servicePid(owner).value());
                if (dbusActivated)
                    return 0;   // started only for the name, which someone else serves
            }
        }
    }
    if (dbusActivated) {
        // The call that activated fm is waiting for the name; if it never comes, don't stay around.
        QTimer::singleShot(30000, &app, [&] { if (!mainFm) QCoreApplication::quit(); });
    }

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
