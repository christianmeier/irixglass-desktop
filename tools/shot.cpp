// Screenshot harness: load fm off-screen, evaluate a QML expression on the window, save a PNG.
//   make build/shot
//   XDG_CONFIG_HOME=$(mktemp -d) XDG_CACHE_HOME=$(mktemp -d) QT_QPA_PLATFORM=offscreen
//       build/shot <start-dir> <repo> <out.png> '<qml expression>'
// The expression runs 1.2 s after start in the window's scope (e.g. 'viewMode = 1',
// 'toggleSplit()', 'openTerminal("shell")'); the PNG is taken at 6 s. The glass is transparent in
// the PNG (KWin draws the blur): composite it over a blurred wallpaper to judge it (DEVELOPMENT.md).
// Like the tests it links fm's sources, not Qt WebEngine (main() only sets AA_ShareOpenGLContexts).
#include "fm.h"
#include <QApplication>
#include <QCoreApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlExpression>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>
#include <cstdio>

int main(int argc, char *argv[])
{
    if (argc < 5) {
        std::fprintf(stderr, "usage: %s <start-dir> <repo> <out.png> '<qml expression>'\n", argv[0]);
        return 2;
    }
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    QApplication app(argc, argv);
    app.setOrganizationName("irixfm-shot");
    QQuickStyle::setStyle("org.kde.desktop");
    Fm fm(argv[1], argv[2]);
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("Fm", &fm);
    engine.load(QUrl::fromLocalFile(QString(argv[2]) + "/qml/Main.qml"));
    if (engine.rootObjects().isEmpty())
        return 1;
    QObject *win = engine.rootObjects().first();
    QTimer::singleShot(1200, &app, [&] {
        QQmlExpression e(qmlContext(win), win, argv[4]);
        e.evaluate();
        if (e.hasError())
            qWarning() << e.error().toString();
    });
    QTimer::singleShot(6000, &app, [&] {
        const bool ok = qobject_cast<QQuickWindow *>(win)->grabWindow().save(argv[3]);
        std::fprintf(stderr, ok ? "shot: saved %s\n" : "shot: could not save %s\n", argv[3]);
        app.exit(ok ? 0 : 1);
    });
    return app.exec();
}
