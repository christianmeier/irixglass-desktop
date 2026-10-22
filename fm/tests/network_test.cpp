// Network browsing test: KioModel (folders behind KIO URLs) and the window on KIO URLs, without a
// network. The model is forced onto file:// and onto tar:/ (KIO's archive worker, a real non-file
// scheme that works offline); errors come from smb://nonexistent.invalid. URL helpers (crumbs for
// smb, sftp, remote:/, mtp:, afc:) are checked on sample strings. Run with `make test`.
#include "fm.h"
#include "kiomodel.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QProcess>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <cstdio>

namespace
{
int failures = 0;

void check(bool ok, const QString &what, const QString &got = {})
{
    std::printf("%s %-60s %s\n", ok ? "PASS" : "FAIL", qPrintable(what), ok ? "" : qPrintable("got: " + got));
    failures += ok ? 0 : 1;
}

void writeFile(const QString &path, int bytes)
{
    QFile f(path);
    if (f.open(QIODevice::WriteOnly))
        f.write(QByteArray(bytes, 'x'));
}

// Waits (processing events) until `cond` holds or `ms` pass.
template<typename F>
bool waitFor(F cond, int ms)
{
    QElapsedTimer t;
    t.start();
    while (!cond() && t.elapsed() < ms)
        QTest::qWait(20);
    return cond();
}

QStringList names(const KioModel &m)
{
    QStringList out;
    for (int i = 0; i < m.count(); ++i)
        out << m.get(i, QStringLiteral("fileName")).toString();
    return out;
}

QString labels(const QVariantList &crumbs)
{
    QStringList out;
    for (const QVariant &c : crumbs)
        out << c.toMap().value(QStringLiteral("label")).toString() + QLatin1Char('=') + c.toMap().value(QStringLiteral("path")).toString();
    return out.join(QLatin1String(" | "));
}
} // namespace

int main(int argc, char *argv[])
{
    setvbuf(stdout, nullptr, _IONBF, 0);
    qputenv("IRIXFM_NO_DIALOGS", "1");
    QApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("irixfm-test"));
    QQuickStyle::setStyle(QStringLiteral("org.kde.desktop"));

    // --- URL helpers on sample strings
    check(KioModel::isUrl(QStringLiteral("smb://host/share")) && KioModel::isUrl(QStringLiteral("remote:/"))
              && KioModel::isUrl(QStringLiteral("mtp:udi=/org/kde/solid/x")) && !KioModel::isUrl(QStringLiteral("/home/x"))
              && !KioModel::isUrl(QStringLiteral("file:///tmp")),
          QStringLiteral("isUrl: KIO URLs yes, paths and file:// no"));
    check(KioModel::normalize(QStringLiteral("smb://host/share/folder/")) == QStringLiteral("smb://host/share/folder")
              && KioModel::normalize(QStringLiteral("smb://")) == QStringLiteral("smb:/")
              && KioModel::normalize(QStringLiteral("remote:")) == QStringLiteral("remote:/")
              && KioModel::normalize(QStringLiteral("sftp://u@h/home/x/../y/")) == QStringLiteral("sftp://u@h/home/y"),
          QStringLiteral("normalize: one spelling per folder"), KioModel::normalize(QStringLiteral("smb://")));
    check(KioModel::parentUrl(QStringLiteral("smb://host/share/folder")) == QStringLiteral("smb://host/share")
              && KioModel::parentUrl(QStringLiteral("smb://host/share")) == QStringLiteral("smb://host/")
              && KioModel::parentUrl(QStringLiteral("smb://host/")).isEmpty() && KioModel::parentUrl(QStringLiteral("remote:/")).isEmpty()
              && KioModel::parentUrl(QStringLiteral("remote:/smb-network")) == QStringLiteral("remote:/"),
          QStringLiteral("parentUrl: up to the server, none above"), KioModel::parentUrl(QStringLiteral("smb://host/share")));
    check(KioModel::childUrl(QStringLiteral("smb://host/a b"), QStringLiteral("New Folder")) == QStringLiteral("smb://host/a b/New Folder"),
          QStringLiteral("childUrl"), KioModel::childUrl(QStringLiteral("smb://host/a b"), QStringLiteral("New Folder")));
    QString c = labels(KioModel::crumbs(QStringLiteral("smb://host/share/folder")));
    check(c == QStringLiteral("host=smb://host/ | share=smb://host/share | folder=smb://host/share/folder"), QStringLiteral("crumbs: smb://host/share/folder"), c);
    c = labels(KioModel::crumbs(QStringLiteral("sftp://chris@server/home/chris")));
    check(c.startsWith(QStringLiteral("chris@server=sftp://chris@server/ | home=")) && c.endsWith(QStringLiteral("chris=sftp://chris@server/home/chris")),
          QStringLiteral("crumbs: sftp://user@host/… starts with user@host"), c);
    c = labels(KioModel::crumbs(QStringLiteral("remote:/")));
    check(c == QStringLiteral("Network=remote:/"), QStringLiteral("crumbs: remote:/ is the Network"), c);
    c = labels(KioModel::crumbs(QStringLiteral("mtp:/Pixel 7/Internal shared storage/DCIM")));
    check(c == QStringLiteral("Devices=mtp:/ | Pixel 7=mtp:/Pixel 7 | Internal shared storage=mtp:/Pixel 7/Internal shared storage | DCIM=mtp:/Pixel 7/Internal shared storage/DCIM"),
          QStringLiteral("crumbs: mtp:/<phone>/<storage>/DCIM"), c);
    c = labels(KioModel::crumbs(QStringLiteral("mtp:udi=/org/kde/solid/udev/sys/devices/usb1/1-2/")));
    check(KioModel::crumbs(QStringLiteral("mtp:udi=/org/kde/solid/udev/sys/devices/usb1/1-2/")).size() == 1 && c.startsWith(QStringLiteral("Device=")),
          QStringLiteral("crumbs: mtp:udi=… is one crumb, not the udi's pieces"), c);
    KioModel::setDisplayName(QStringLiteral("afc://00008110-001A2B3C/"), QStringLiteral("Chris's iPhone"));
    c = labels(KioModel::crumbs(QStringLiteral("afc://00008110-001A2B3C/DCIM")));
    check(c.startsWith(QStringLiteral("Chris's iPhone=afc://00008110-001a2b3c/ | DCIM=")), QStringLiteral("crumbs: a device's display name instead of its id"), c);
    check(KioModel::title(QStringLiteral("smb://host/share")) == QStringLiteral("share") && KioModel::title(QStringLiteral("remote:/")) == QStringLiteral("Network"),
          QStringLiteral("title: last folder, or Network"));

    // --- a folder to list
    QTemporaryDir tmp;
    const QString base = QDir(tmp.path()).canonicalPath();
    QDir(base).mkdir(QStringLiteral("src"));
    const QString src = base + QStringLiteral("/src");
    QDir(src).mkdir(QStringLiteral("Cdir"));
    writeFile(src + QStringLiteral("/b.txt"), 10);
    writeFile(src + QStringLiteral("/a.png"), 3000);
    writeFile(src + QStringLiteral("/d.md"), 500);
    writeFile(src + QStringLiteral("/.hidden"), 1);
    writeFile(src + QStringLiteral("/Cdir/inner.txt"), 5);

    {
        KioModel m;
        m.setUrl(QUrl::fromLocalFile(src).toString());
        check(m.status() == KioModel::Loading || m.status() == KioModel::Ready, QStringLiteral("file:// through KIO: loading starts at once"));
        waitFor([&] { return m.status() == KioModel::Ready && m.count() == 4; }, 10000);
        check(names(m) == QStringList{QStringLiteral("Cdir"), QStringLiteral("a.png"), QStringLiteral("b.txt"), QStringLiteral("d.md")},
              QStringLiteral("listing: folders first, then by name, hidden files left out"), names(m).join(QLatin1Char(',')));
        check(m.get(0, QStringLiteral("fileIsDir")).toBool() && !m.get(1, QStringLiteral("fileIsDir")).toBool()
                  && m.get(1, QStringLiteral("fileSize")).toLongLong() == 3000
                  && m.get(1, QStringLiteral("filePath")).toString() == QUrl::fromLocalFile(src + QStringLiteral("/a.png")).toString()
                  && m.get(1, QStringLiteral("fileUrl")).toUrl() == QUrl::fromLocalFile(src + QStringLiteral("/a.png"))
                  && m.get(1, QStringLiteral("fileModified")).toDateTime().isValid() && !m.get(1, QStringLiteral("fileIsRemote")).toBool()
                  && m.get(1, QStringLiteral("fileSuffix")).toString() == QStringLiteral("png"),
              QStringLiteral("roles: fileIsDir, fileSize, filePath, fileUrl, fileModified, …"));
        check(m.indexOf(QUrl::fromLocalFile(src + QStringLiteral("/d.md")).toString()) == 3, QStringLiteral("indexOf(filePath)"));
        m.setSortField(3);   // Size: largest first
        check(names(m) == QStringList{QStringLiteral("Cdir"), QStringLiteral("a.png"), QStringLiteral("d.md"), QStringLiteral("b.txt")},
              QStringLiteral("sort by size: folders first, largest first"), names(m).join(QLatin1Char(',')));
        m.setSortReversed(true);
        check(names(m) == QStringList{QStringLiteral("Cdir"), QStringLiteral("b.txt"), QStringLiteral("d.md"), QStringLiteral("a.png")},
              QStringLiteral("reversed: folders stay first"), names(m).join(QLatin1Char(',')));
        m.setSortField(4);
        m.setSortReversed(false);
        check(names(m) == QStringList{QStringLiteral("Cdir"), QStringLiteral("d.md"), QStringLiteral("a.png"), QStringLiteral("b.txt")},
              QStringLiteral("sort by kind (extension)"), names(m).join(QLatin1Char(',')));
        m.setSortField(1);
        m.setShowHidden(true);
        check(m.count() == 5 && names(m).contains(QStringLiteral(".hidden")), QStringLiteral("showHidden shows dotfiles"));
        m.setShowHidden(false);
        writeFile(src + QStringLiteral("/e.txt"), 1);
        check(waitFor([&] { return m.count() == 5; }, 10000), QStringLiteral("a new file shows up (auto update)"), QString::number(m.count()));
        QFile::remove(src + QStringLiteral("/e.txt"));
        check(waitFor([&] { return m.count() == 4; }, 10000), QStringLiteral("a deleted file goes away"), QString::number(m.count()));
    }

    // --- tar:/ — a KIO scheme that isn't file://, offline
    const QString archive = base + QStringLiteral("/a.tar");
    QProcess::execute(QStringLiteral("tar"), {QStringLiteral("cf"), archive, QStringLiteral("-C"), base, QStringLiteral("src")});
    const QString tarDir = KioModel::normalize(QStringLiteral("tar:") + archive + QStringLiteral("/src"));
    Fm fm(base, QStringLiteral(IRIXFM_SOURCE_DIR));
    {
        KioModel m;
        m.setUrl(tarDir);
        waitFor([&] { return m.status() == KioModel::Ready && m.count() == 4; }, 15000);
        check(m.count() == 4 && m.get(0, QStringLiteral("fileName")).toString() == QStringLiteral("Cdir") && m.get(1, QStringLiteral("fileIsRemote")).toBool(),
              QStringLiteral("tar:/ listing through KIO"), names(m).join(QLatin1Char(',')));
        const QString png = m.get(1, QStringLiteral("filePath")).toString();
        check(png == tarDir + QStringLiteral("/a.png"), QStringLiteral("a KIO item's filePath is its URL"), png);
        check(fm.kind(png) == QStringLiteral("image") && fm.kind(tarDir + QStringLiteral("/Cdir")) == QStringLiteral("folder"),
              QStringLiteral("Fm.kind of KIO items, from the listing"), fm.kind(png));
        const QVariantMap info = fm.info(png);
        check(info.value(QStringLiteral("remote")).toBool() && info.value(QStringLiteral("name")).toString() == QStringLiteral("a.png")
                  && info.value(QStringLiteral("size")).toLongLong() == 3000 && info.value(QStringLiteral("mime")).toString() == QStringLiteral("image/png"),
              QStringLiteral("Get Info of a KIO item"));
        check(fm.tags(png).isEmpty() && fm.freeSpace(tarDir).isEmpty() && fm.textPreview(png).isEmpty() && fm.showOriginal(png).isEmpty(),
              QStringLiteral("local-only things answer empty for URLs"));
        // copy out of the archive with KIO, as a drop or F5 would
        QDir(base).mkdir(QStringLiteral("out"));
        fm.copyOrMove({png}, base + QStringLiteral("/out"), false);
        check(waitFor([&] { return QFileInfo(base + QStringLiteral("/out/a.png")).size() == 3000; }, 15000), QStringLiteral("copy from a KIO folder to a local one"));
        bool sized = false;
        QObject::connect(&fm, &Fm::folderSizeReady, &app, [&](const QString &p, qint64 bytes) { if (p == tarDir && bytes < 0) sized = true; });
        fm.folderSize(tarDir);
        check(waitFor([&] { return sized; }, 2000), QStringLiteral("no folder size over KIO (answers --)"));
    }

    // --- errors
    {
        KioModel m;
        m.setUrl(QStringLiteral("tar:") + base + QStringLiteral("/missing.tar/x"));
        waitFor([&] { return m.status() == KioModel::Error; }, 15000);
        check(m.status() == KioModel::Error && !m.errorString().isEmpty() && m.count() == 0, QStringLiteral("a missing archive: error state with a message"), m.errorString());
        QElapsedTimer t;
        t.start();
        m.setUrl(QStringLiteral("sftp://nonexistent.invalid/x"));
        check(t.elapsed() < 500, QStringLiteral("opening an unreachable host doesn't block"), QString::number(t.elapsed()));
        waitFor([&] { return m.status() == KioModel::Error; }, 30000);
        check(m.status() == KioModel::Error && !m.errorString().isEmpty(), QStringLiteral("sftp://nonexistent.invalid: error state, no hang"),
              QString::number(m.status()) + QLatin1Char(' ') + m.errorString());
        // smb: libsmbclient's name lookup takes 25-60 s here, so the error is only checked when it comes in time
        t.restart();
        m.setUrl(QStringLiteral("smb://nonexistent.invalid/x"));
        check(t.elapsed() < 500 && m.status() == KioModel::Loading, QStringLiteral("smb://nonexistent.invalid: loading, the UI not blocked"));
        if (waitFor([&] { return m.status() == KioModel::Error; }, 8000))
            check(!m.errorString().isEmpty(), QStringLiteral("smb://nonexistent.invalid: error state with a message"), m.errorString());
        else
            std::printf("note: smb://nonexistent.invalid still looking up the name after 8 s (not a failure)\n");
    }

    // --- resolving typed URLs
    auto resolved = [&](const QString &t, const QString &b) { return fm.resolvePath(t, b); };
    check(resolved(QStringLiteral("smb://host/share/"), base).value(QStringLiteral("dir")).toString() == QStringLiteral("smb://host/share"),
          QStringLiteral("typed smb://host/share/ resolves"));
    check(resolved(QStringLiteral("sftp://chris@server/home/chris"), base).value(QStringLiteral("ok")).toBool(), QStringLiteral("typed sftp://user@host/… resolves"));
    check(!resolved(QStringLiteral("bogus-proto://x"), base).value(QStringLiteral("ok")).toBool(), QStringLiteral("an unknown protocol is refused"));
    check(resolved(QStringLiteral("trash:/"), base).value(QStringLiteral("dir")).toString() == fm.trashDir(), QStringLiteral("trash:/ is fm's Trash folder"));
    check(resolved(QStringLiteral(".."), QStringLiteral("smb://host/share/folder")).value(QStringLiteral("dir")).toString() == QStringLiteral("smb://host/share"),
          QStringLiteral(".. inside a KIO folder"), resolved(QStringLiteral(".."), QStringLiteral("smb://host/share/folder")).value(QStringLiteral("dir")).toString());
    check(fm.completePath(QStringLiteral("smb://ho"), base) == QStringLiteral("smb://ho"), QStringLiteral("no completion over KIO"));

    // --- the window on KIO folders
    Fm wfm(tarDir, QStringLiteral(IRIXFM_SOURCE_DIR));   // before the engine, so it outlives the window
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("Fm"), &wfm);
    engine.load(QUrl::fromLocalFile(QStringLiteral(IRIXFM_SOURCE_DIR "/qml/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return 2;
    QObject *win = engine.rootObjects().constFirst();
    auto *window = qobject_cast<QQuickWindow *>(win);
    auto dir = [&] { return win->property("dir").toString(); };
    auto call = [&](const char *fn) { QMetaObject::invokeMethod(win, fn); QTest::qWait(200); };
    auto go = [&](const QString &p) { QMetaObject::invokeMethod(win, "go", Q_ARG(QVariant, p), Q_ARG(QVariant, false)); QTest::qWait(200); };
    auto itemCount = [&] { return win->property("itemCount").toInt(); };

    QTimer::singleShot(800, &app, [&] {
        window->requestActivate();
        QTest::qWaitForWindowActive(window);
        auto *grid = win->findChild<QQuickItem *>(QStringLiteral("grid"));
        auto *kio = win->findChild<QObject *>(QStringLiteral("kioFolder"));
        check(win->property("remote").toBool() && dir() == tarDir, QStringLiteral("window: started on a KIO URL"), dir());
        waitFor([&] { return itemCount() == 4 && grid->property("count").toInt() == 4; }, 15000);
        check(itemCount() == 4 && grid->property("count").toInt() == 4 && grid->property("model").value<QObject *>() == kio,
              QStringLiteral("window: the icon view shows the KIO model"), QString::number(itemCount()));
        // the delegates show the names (the pool was drained when switching models)
        QStringList shown;
        for (QQuickItem *d : grid->property("contentItem").value<QQuickItem *>()->childItems())
            if (d->isVisible() && d->property("fileName").isValid())
                shown << d->property("fileName").toString();
        check(shown.contains(QStringLiteral("a.png")) && shown.contains(QStringLiteral("Cdir")), QStringLiteral("window: delegates show KIO items"), shown.join(QLatin1Char(',')));
        // double-click a folder = activate(path, true): navigates inside KIO
        const QString cdir = tarDir + QStringLiteral("/Cdir");
        QMetaObject::invokeMethod(win, "activate", Q_ARG(QVariant, cdir), Q_ARG(QVariant, true));
        waitFor([&] { return itemCount() == 1; }, 15000);
        check(dir() == cdir && itemCount() == 1, QStringLiteral("window: opening a KIO folder navigates into it"), dir());
        call("goBack");
        check(dir() == tarDir, QStringLiteral("window: back"), dir());
        call("goForward");
        check(dir() == cdir, QStringLiteral("window: forward"), dir());
        call("goUp");
        check(dir() == tarDir, QStringLiteral("window: up goes to the parent URL"), dir());
        // typing a URL in the path bar
        QTest::keyClick(window, Qt::Key_L, Qt::ControlModifier);
        auto *input = win->findChild<QQuickItem *>(QStringLiteral("pathInput"));
        check(input && input->property("text").toString() == tarDir + QStringLiteral("/"), QStringLiteral("path bar: editing starts with the URL"),
              input ? input->property("text").toString() : QString());
        if (input)
            input->setProperty("text", cdir);
        QTest::keyClick(window, Qt::Key_Return);
        QTest::qWait(200);
        check(dir() == cdir, QStringLiteral("path bar: typing a KIO URL goes there"), dir());
        QMetaObject::invokeMethod(win, "submitPath", Q_ARG(QVariant, QStringLiteral("..")));
        check(dir() == tarDir, QStringLiteral("path bar: .. in a KIO folder"), dir());
        // tabs
        QMetaObject::invokeMethod(win, "newTab", Q_ARG(QVariant, cdir), Q_ARG(QVariant, true));
        QTest::qWait(300);
        QVariant title;
        QMetaObject::invokeMethod(win, "tabTitle", Q_RETURN_ARG(QVariant, title), Q_ARG(QVariant, win->property("tabIndex")));
        check(dir() == cdir && title.toString() == QStringLiteral("Cdir"), QStringLiteral("tabs: a tab on a KIO folder, titled by its name"), title.toString());
        QMetaObject::invokeMethod(win, "switchTab", Q_ARG(QVariant, 0));
        QTest::qWait(300);
        check(dir() == tarDir, QStringLiteral("tabs: switching back restores the URL"), dir());
        // selection works on URLs
        waitFor([&] { return itemCount() == 4; }, 15000);
        QVariant first;
        QMetaObject::invokeMethod(win, "itemAt", Q_RETURN_ARG(QVariant, first), Q_ARG(QVariant, 1), Q_ARG(QVariant, QStringLiteral("filePath")));
        QMetaObject::invokeMethod(win, "selectWhenReady", Q_ARG(QVariant, QStringList{first.toString()}));
        check(win->property("selection").toMap().contains(tarDir + QStringLiteral("/a.png")), QStringLiteral("selection: KIO items by URL"), first.toString());
        // Delete on a KIO item asks first (no Trash there), nothing is trashed
        QMetaObject::invokeMethod(win, "trashSelected");
        QTest::qWait(200);
        check(win->property("pendingDelete").toStringList() == QStringList{tarDir + QStringLiteral("/a.png")},
              QStringLiteral("delete: KIO items wait for the confirmation"), win->property("pendingDelete").toStringList().join(QLatin1Char(',')));
        win->setProperty("pendingDelete", QStringList());
        // back to a local folder: FolderListModel again
        go(src);
        auto *folder = win->findChild<QObject *>(QStringLiteral("kioFolder"));
        waitFor([&] { return itemCount() == 4 && grid->property("count").toInt() == 4; }, 10000);
        check(!win->property("remote").toBool() && grid->property("model").value<QObject *>() != folder && itemCount() == 4,
              QStringLiteral("window: a local folder uses FolderListModel again"), QString::number(itemCount()));
        // an unreachable host: a readable message in the view, not an empty folder
        go(QStringLiteral("smb://nonexistent.invalid/x"));
        auto *msg = win->findChild<QQuickItem *>(QStringLiteral("remoteMessage"));
        check(msg && msg->isVisible() && msg->property("text").toString().startsWith(QStringLiteral("Connecting")),
              QStringLiteral("view: Connecting… while KIO works"), msg ? msg->property("text").toString() : QString());
        go(QStringLiteral("tar:") + base + QStringLiteral("/missing.tar/x"));   // fails at once (smb takes ~25 s)
        waitFor([&] { return msg && msg->property("text").toString().startsWith(QStringLiteral("Can't open")); }, 15000);
        check(msg && msg->isVisible() && msg->property("text").toString().startsWith(QStringLiteral("Can't open")),
              QStringLiteral("view: an error message instead of an empty folder"), msg ? msg->property("text").toString() : QString());
        // Network in the sidebar opens remote:/ in fm
        const QVariantMap network{{QStringLiteral("label"), QStringLiteral("Network")}, {QStringLiteral("action"), QStringLiteral("network")}};
        QMetaObject::invokeMethod(win, "activatePlace", Q_ARG(QVariant, network));
        QTest::qWait(300);
        check(dir() == QStringLiteral("remote:/"), QStringLiteral("sidebar: Network opens remote:/ in fm"), dir());
        waitFor([&] { return itemCount() > 0; }, 15000);
        check(itemCount() > 0, QStringLiteral("remote:/ lists the network places"), QString::number(itemCount()));
        // split side on a KIO folder
        call("toggleSplit");
        auto *split = win->findChild<QObject *>(QStringLiteral("splitPane"));
        QMetaObject::invokeMethod(split, "go", Q_ARG(QVariant, tarDir));
        waitFor([&] { return split->property("count").toInt() == 4; }, 15000);
        check(split->property("count").toInt() == 4 && split->property("remote").toBool(), QStringLiteral("split side: lists a KIO folder"),
              split->property("count").toString());
        QMetaObject::invokeMethod(split, "goUp");
        QTest::qWait(200);
        check(split->property("dir").toString() == KioModel::parentUrl(tarDir), QStringLiteral("split side: up in a KIO folder"), split->property("dir").toString());
        app.exit(failures ? 1 : 0);
    });
    return app.exec();
}
