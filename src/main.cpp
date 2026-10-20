#include "fm.h"

#include <QApplication>
#include <QIcon>
#include <QDir>
#include <QFileInfo>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("irixfm"));
    app.setOrganizationName(QStringLiteral("irixfm"));
    app.setApplicationDisplayName(QStringLiteral("fm"));
    app.setDesktopFileName(QStringLiteral("irixfm"));
    // The dock and window switcher show the icon theme's normal file manager icon, not the IRIX one.
    app.setWindowIcon(QIcon::fromTheme(QStringLiteral("system-file-manager")));
    QQuickStyle::setStyle(QStringLiteral("org.kde.desktop"));

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

    Fm fm(start, assets);
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("Fm"), &fm);
    engine.load(QUrl::fromLocalFile(assets + QStringLiteral("/qml/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return 1;
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
