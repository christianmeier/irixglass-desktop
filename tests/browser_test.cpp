// Web tabs (Qt WebEngine): F7 opens one, a page loads and names the tab, the thumbwheel zooms it,
// the address field goes places, and it works on the split side. Off-screen, run with `make test`.
#include "fm.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QtWebEngineQuick/qtwebenginequickglobal.h>
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
bool waitFor(const std::function<bool()> &cond, int ms = 15000)
{
    for (int t = 0; t < ms && !cond(); t += 50)
        QTest::qWait(50);
    return cond();
}
void write(const QString &path, const QByteArray &data)
{
    QFile f(path);
    if (f.open(QIODevice::WriteOnly))
        f.write(data);
}
} // namespace

int main(int argc, char *argv[])
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    qputenv("QTWEBENGINE_CHROMIUM_FLAGS", "--disable-gpu --no-sandbox");
    QtWebEngineQuick::initialize();
    QApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("irixfm-test"));
    QQuickStyle::setStyle(QStringLiteral("org.kde.desktop"));
    QTemporaryDir tmp;
    const QString base = QDir(tmp.path()).canonicalPath();
    write(base + QStringLiteral("/page.html"), "<html><head><title>Hello Page</title></head><body><a id=l href='two.html'>next</a></body></html>");
    write(base + QStringLiteral("/two.html"), "<html><head><title>Second Page</title></head><body>two</body></html>");

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
    auto active = [&]() -> QQuickItem * { return win->property("activeTerminal").value<QQuickItem *>(); };

    QTimer::singleShot(1000, &app, [&] {
        window->requestActivate();
        (void)QTest::qWaitForWindowActive(window);

        // F7 with an .html file selected: a web tab on that page
        win->setProperty("selection", QVariantMap{{base + QStringLiteral("/page.html"), true}});
        QTest::keyClick(window, Qt::Key_F7);
        QTest::qWait(300);
        QQuickItem *b = active();
        check(tabs().size() == 2 && b && b->objectName() == QStringLiteral("browserView") && win->property("browserActive").toBool(),
              QStringLiteral("F7 opens a web tab"));
        if (b) {
            check(waitFor([&] { return b->property("title").toString() == QStringLiteral("Hello Page"); }),
                  QStringLiteral("…showing the selected page"), b->property("title").toString());
            QVariant title;
            QMetaObject::invokeMethod(win, "tabTitle", Q_RETURN_ARG(QVariant, title), Q_ARG(QVariant, 1));
            check(title.toString() == QStringLiteral("Hello Page"), QStringLiteral("the tab is named after the page"), title.toString());

            // the address field: a path goes to that file
            QMetaObject::invokeMethod(b, "go", Q_ARG(QVariant, base + QStringLiteral("/two.html")));
            check(waitFor([&] { return b->property("title").toString() == QStringLiteral("Second Page"); }),
                  QStringLiteral("typing a path in the address field opens it"), b->property("title").toString());
            QMetaObject::invokeMethod(win, "goBack");
            check(waitFor([&] { return b->property("title").toString() == QStringLiteral("Hello Page"); }),
                  QStringLiteral("Back goes back in the page, not the folder"), b->property("title").toString());

            // the thumbwheel zooms the page (12 pt = 100 %)
            auto *wheel = win->findChild<QQuickItem *>(QStringLiteral("thumbwheel"));
            const double icons = win->property("wheelValue").toDouble();
            wheel->setProperty("value", .5);   // 18 pt
            QTest::qWait(100);
            auto *view = b->findChild<QObject *>(QStringLiteral("webView"));
            check(view && qAbs(view->property("zoomFactor").toDouble() - 1.5) < .01 && qAbs(win->property("wheelValue").toDouble() - icons) < 1e-6,
                  QStringLiteral("the thumbwheel zooms the page, not the icons"), view ? view->property("zoomFactor").toString() : QString());

            QTest::keyClick(window, Qt::Key_W, Qt::ControlModifier | Qt::ShiftModifier);
            check(waitFor([&] { return tabs().size() == 1 && !win->property("browserActive").toBool(); }, 3000),
                  QStringLiteral("Ctrl+Shift+W closes the web tab"));
        }

        // the split side
        QMetaObject::invokeMethod(win, "toggleSplit");
        QTest::qWait(300);
        win->setProperty("activePane", 1);
        QTest::keyClick(window, Qt::Key_F7);
        QTest::qWait(300);
        auto *pane = win->findChild<QQuickItem *>(QStringLiteral("splitPane"));
        check(pane->property("tabs").toList().size() == 2 && pane->property("activeTermId").toInt() != 0 && win->property("browserActive").toBool(),
              QStringLiteral("F7 on the split side opens the web tab there"));
        app.exit(failures ? 1 : 0);
    });
    return app.exec();
}
