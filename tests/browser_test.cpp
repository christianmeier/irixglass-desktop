// Web tabs (Qt WebEngine): F7 opens one, a page loads and names the tab, the thumbwheel zooms it,
// the address field goes places, and it works on the split side. Off-screen, run with `make test`.
#include "fm.h"

#include <QApplication>
#include <QDir>
#include <QClipboard>
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
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);   // as main.cpp does
    QApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("irixfm-test"));
    QQuickStyle::setStyle(QStringLiteral("org.kde.desktop"));
    QTemporaryDir tmp;
    const QString base = QDir(tmp.path()).canonicalPath();
    write(base + QStringLiteral("/page.html"), "<html><head><title>Hello Page</title><style>body{margin:0} a{display:block;width:200px;height:40px}</style></head><body><a id=l href='two.html'>next</a></body></html>");
    write(base + QStringLiteral("/two.html"), "<html><head><title>Second Page</title></head><body>two</body></html>");
    // a form: the title shows what's in the field; a target=_blank link; a download link
    write(base + QStringLiteral("/form.html"),
          "<html><head><title>v:</title><style>body{margin:0} input{position:absolute;left:0;top:0;width:300px;height:40px}"
          " a{position:absolute;left:0;top:60px;width:200px;height:30px;display:block}"
          " #dl{top:110px}</style></head><body>"
          "<input id=f oninput=\"document.title='v:'+this.value\">"
          "<a id=nw href='two.html' target=_blank>new window</a>"
          "<a id=dl href='data.bin' download>download</a></body></html>");
    write(base + QStringLiteral("/data.bin"), QByteArray(4096, 'x'));
    write(base + QStringLiteral("/victim.txt"), "do not touch");
    // downloads go to XDG_DOWNLOAD_DIR: point it into the test folder, not the real ~/Downloads
    QDir().mkpath(base + QStringLiteral("/dl"));
    {
        const QString cfg = qEnvironmentVariable("XDG_CONFIG_HOME");
        if (!cfg.isEmpty()) {
            QDir().mkpath(cfg);
            write(cfg + QStringLiteral("/user-dirs.dirs"), ("XDG_DOWNLOAD_DIR=\"" + base + "/dl\"\n").toUtf8());
        }
    }

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

            // real input: click the address field, type a path, Enter
            {
                auto *addr = b->findChild<QQuickItem *>(QStringLiteral("browserAddress"));
                const QPoint ap = addr->mapToScene(QPointF(addr->width() / 2, addr->height() / 2)).toPoint();
                QTest::mouseClick(window, Qt::LeftButton, {}, ap);
                QTest::qWait(200);
                check(addr->hasActiveFocus(), QStringLiteral("clicking the address field gives it the keyboard"));
                QTest::keyClick(window, Qt::Key_A, Qt::ControlModifier);
                for (const QChar c : base + QStringLiteral("/two.html"))
                    QTest::keyClick(window, c.toLatin1());
                QTest::keyClick(window, Qt::Key_Return);
                check(waitFor([&] { return b->property("title").toString() == QStringLiteral("Second Page"); }),
                      QStringLiteral("typing an address and Enter goes there"), b->property("title").toString() + " | field: " + addr->property("text").toString());
                QMetaObject::invokeMethod(win, "goBack");
                waitFor([&] { return b->property("title").toString() == QStringLiteral("Hello Page"); });
                // click the link in the page
                auto *view = b->findChild<QQuickItem *>(QStringLiteral("webView"));
                QTest::qWait(500);
                const QPoint lp = view->mapToScene(QPointF(20, 15)).toPoint();
                QTest::mouseClick(window, Qt::LeftButton, {}, lp);
                check(waitFor([&] { return b->property("title").toString() == QStringLiteral("Second Page"); }, 5000),
                      QStringLiteral("clicking a link in the page follows it"), b->property("title").toString());
                QMetaObject::invokeMethod(win, "goBack");
                waitFor([&] { return b->property("title").toString() == QStringLiteral("Hello Page"); });
            }
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

        // inside a page: typing in a form, the editing keys stay in the page (they must not paste,
        // undo or delete files: fm's window shortcuts for those are off while a web tab shows),
        // target=_blank links, downloads
        {
            win->setProperty("selection", QVariantMap{{base + QStringLiteral("/form.html"), true}});
            QTest::keyClick(window, Qt::Key_F7);
            QTest::qWait(300);
            QQuickItem *w = active();
            waitFor([&] { return w && w->property("title").toString() == QStringLiteral("v:"); });
            auto *view = w->findChild<QQuickItem *>(QStringLiteral("webView"));
            QTest::qWait(400);
            QTest::mouseClick(window, Qt::LeftButton, {}, view->mapToScene(QPointF(100, 20)).toPoint());
            QTest::qWait(200);
            for (const char c : QByteArray("hello"))
                QTest::keyClick(window, c);
            check(waitFor([&] { return w->property("title").toString() == QStringLiteral("v:hello"); }, 3000),
                  QStringLiteral("typing into a form field in the page works"), w->property("title").toString());
            // the clipboard holds a file AND text: Ctrl+V must paste the text into the field, not the file into the folder
            auto *mime = new QMimeData;
            mime->setUrls({QUrl::fromLocalFile(base + QStringLiteral("/victim.txt"))});
            mime->setText(QStringLiteral("-clip"));
            QGuiApplication::clipboard()->setMimeData(mime);
            const int filesBefore = QDir(base).entryList(QDir::Files).size();
            QTest::keyClick(window, Qt::Key_End);
            QTest::keyClick(window, Qt::Key_V, Qt::ControlModifier);
            QTest::qWait(500);
            check(waitFor([&] { return w->property("title").toString().startsWith(QStringLiteral("v:hello")) && w->property("title").toString() != QStringLiteral("v:hello"); }, 3000)
                      && QDir(base).entryList(QDir::Files).size() == filesBefore,
                  QStringLiteral("Ctrl+V pastes into the page, not files into the folder"),
                  w->property("title").toString() + " files " + QString::number(QDir(base).entryList(QDir::Files).size()));
            // Ctrl+A then Delete clears the field; the selected file stays put
            win->setProperty("selection", QVariantMap{{base + QStringLiteral("/victim.txt"), true}});
            QTest::keyClick(window, Qt::Key_A, Qt::ControlModifier);
            QTest::keyClick(window, Qt::Key_Delete);
            check(waitFor([&] { return w->property("title").toString() == QStringLiteral("v:"); }, 3000)
                      && QFile::exists(base + QStringLiteral("/victim.txt")),
                  QStringLiteral("Ctrl+A and Delete edit the field, no file is trashed"), w->property("title").toString());
            // Ctrl+Z undoes in the field, not fm's last file operation
            const int beforeUndo = QDir(base).entryList(QDir::Files).size();
            QTest::keyClick(window, Qt::Key_Z, Qt::ControlModifier);
            check(waitFor([&] { return w->property("title").toString() != QStringLiteral("v:"); }, 3000)
                      && QDir(base).entryList(QDir::Files).size() == beforeUndo,
                  QStringLiteral("Ctrl+Z undoes in the field, not a file operation"), w->property("title").toString());
            // target=_blank: opens in this tab
            QTest::mouseClick(window, Qt::LeftButton, {}, view->mapToScene(QPointF(50, 75)).toPoint());
            check(waitFor([&] { return w->property("title").toString() == QStringLiteral("Second Page"); }, 5000),
                  QStringLiteral("a link that opens a new window opens in the tab"), w->property("title").toString());
            QMetaObject::invokeMethod(win, "goBack");
            waitFor([&] { return w->property("title").toString().startsWith(QStringLiteral("v:")); });
            QTest::qWait(400);
            // a download link: saved to the Downloads folder
            QTest::mouseClick(window, Qt::LeftButton, {}, view->mapToScene(QPointF(50, 125)).toPoint());
            check(waitFor([&] { return QFileInfo(base + QStringLiteral("/dl/data.bin")).size() == 4096; }, 8000),
                  QStringLiteral("download links save into Downloads"), QDir(base + QStringLiteral("/dl")).entryList(QDir::Files).join(", "));
            auto *notice = w->findChild<QQuickItem *>(QStringLiteral("downloadNotice"));
            check(waitFor([&] { return notice && notice->isVisible() && w->property("downloadText").toString().contains(QStringLiteral("✓")); }, 3000),
                  QStringLiteral("…the bar says so"), w->property("downloadText").toString());
            const int tabsBefore = tabs().size();
            QTest::mouseClick(window, Qt::LeftButton, {}, notice->mapToScene(QPointF(notice->width() / 2, notice->height() / 2)).toPoint());
            QTest::qWait(400);
            check(tabs().size() == tabsBefore + 1 && win->property("dir").toString() == base + QStringLiteral("/dl")
                      && win->property("selection").toMap().contains(base + QStringLiteral("/dl/data.bin")),
                  QStringLiteral("…and clicking it shows the file in a folder tab"), win->property("dir").toString());
            QTest::keyClick(window, Qt::Key_W, Qt::ControlModifier);   // back to the web tab
            QTest::qWait(200);
            // the profile keeps cookies and storage on disk
            auto *profile = view->property("profile").value<QObject *>();
            check(profile && !profile->property("offTheRecord").toBool()
                      && profile->property("persistentStoragePath").toString().contains(QStringLiteral("irixfm")),
                  QStringLiteral("the web profile is saved on disk (cookies, logins)"),
                  profile ? profile->property("persistentStoragePath").toString() : QString());
            QTest::keyClick(window, Qt::Key_W, Qt::ControlModifier | Qt::ShiftModifier);
            QTest::qWait(200);
        }
        // several web tabs at once all load (they share one profile)
        {
            QStringList titles;
            for (int i = 0; i < 3; ++i) {
                win->setProperty("selection", QVariantMap{{base + (i % 2 ? QStringLiteral("/two.html") : QStringLiteral("/page.html")), true}});
                QTest::keyClick(window, Qt::Key_F7);
                QTest::qWait(200);
                QQuickItem *w = active();
                waitFor([&] { return w && !w->property("title").toString().startsWith(QStringLiteral("file:")) && !w->property("loading").toBool(); });
                titles << (w ? w->property("title").toString() : QString());
            }
            check(titles == QStringList{QStringLiteral("Hello Page"), QStringLiteral("Second Page"), QStringLiteral("Hello Page")} && tabs().size() == 4,
                  QStringLiteral("three web tabs open at once all load"), titles.join(", "));
            // …also real sites, which use the profile's storage (cookies, quota database): with a
            // profile per tab the second one found the storage locked. Skipped when offline.
            if (qEnvironmentVariableIsSet("IRIXFM_TEST_NETWORK")) {
                QStringList web;
                for (int i = 1; i <= 3; ++i) {
                    QMetaObject::invokeMethod(active(), "go", Q_ARG(QVariant, QStringLiteral("https://example.com/?tab=%1").arg(i)));
                    QQuickItem *w = active();
                    waitFor([&] { return w->property("title").toString() == QStringLiteral("Example Domain"); }, 20000);
                    web << w->property("title").toString();
                    if (i < 3) {
                        QTest::keyClick(window, Qt::Key_F7);
                        QTest::qWait(300);
                    }
                }
                check(web.count(QStringLiteral("Example Domain")) == 3, QStringLiteral("three web tabs load real sites"), web.join(", "));
                for (int i = 0; i < 2; ++i) {
                    QTest::keyClick(window, Qt::Key_W, Qt::ControlModifier | Qt::ShiftModifier);
                    QTest::qWait(150);
                }
            }
            for (int i = 0; i < 3; ++i) {
                QTest::keyClick(window, Qt::Key_W, Qt::ControlModifier | Qt::ShiftModifier);
                QTest::qWait(150);
            }
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
