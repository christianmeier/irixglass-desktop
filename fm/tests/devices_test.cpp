// Devices in Locations, with a fake device backend (Fm::setFakeDevices): nothing here touches real
// drives. Checks the filter (system partitions out, USB/SD/disc/LUKS/phones in), the sidebar rows,
// click to mount/open, the device menu per kind, Safely Remove (every partition unmounted, then
// power off), fm's views and terminal leaving the volume first, busy volumes, and disc eject.
// Run with `make test`.
#include "fm.h"

#include <QApplication>
#include <QDir>
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
#include <functional>

namespace
{
int failures = 0;

void check(bool ok, const QString &what, const QString &got = {})
{
    std::printf("%s %-58s %s\n", ok ? "PASS" : "FAIL", qPrintable(what), ok ? "" : qPrintable("got: " + got));
    failures += ok ? 0 : 1;
}

bool waitFor(const std::function<bool()> &cond, int ms = 5000)
{
    for (int t = 0; t < ms && !cond(); t += 50)
        QTest::qWait(50);
    return cond();
}

QVariantMap dev(std::initializer_list<std::pair<QString, QVariant>> kv)
{
    QVariantMap m{{QStringLiteral("usage"), QStringLiteral("filesystem")}, {QStringLiteral("fsType"), QStringLiteral("ext4")}};
    for (const auto &[k, v] : kv)
        m[k] = v;
    return m;
}

QStringList labels(const QVariantList &rows)
{
    QStringList out;
    for (const QVariant &r : rows)
        out << r.toMap().value(QStringLiteral("label")).toString();
    return out;
}

QVariantMap row(const QVariantList &rows, const QString &label)
{
    for (const QVariant &r : rows)
        if (r.toMap().value(QStringLiteral("label")).toString() == label)
            return r.toMap();
    return {};
}
} // namespace

int main(int argc, char *argv[])
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    qputenv("IRIXFM_SHELL", "/bin/bash");
    qputenv("PS1", "fm$ ");
    qputenv("HISTFILE", "/dev/null");
    qputenv("IRIXFM_NO_DIALOGS", "1");
    QApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("irixfm-test"));
    QQuickStyle::setStyle(QStringLiteral("org.kde.desktop"));

    QTemporaryDir tmp;
    const QString base = QDir(tmp.path()).canonicalPath();
    auto at = [&](const QString &n) { return base + QLatin1Char('/') + n; };
    for (const char *d : {"Stick/sub", "Data", "Disc", "Busy", "Secret", "Windows"})
        QDir(base).mkpath(QString::fromLatin1(d));

    const QString efi = QStringLiteral("c12a7328-f81f-11d2-ba4b-00a0c93ec93b");
    const QString usb = QStringLiteral("/fake/drives/usb"), sd = QStringLiteral("/fake/drives/sd"), dvd = QStringLiteral("/fake/drives/dvd");
    const QString busyDrive = QStringLiteral("/fake/drives/busy"), nvme = QStringLiteral("/fake/drives/nvme");
    const QString phoneUdi = QStringLiteral("/org/kde/solid/udev/sys/devices/pci0000:00/usb1/1-2");
    const QVariantList raw{
        // a USB stick with two partitions (one mounted) and an EFI partition
        dev({{"udi", "usb1"}, {"drive", usb}, {"label", "STICK"}, {"removable", true}, {"mounted", true}, {"path", at("Stick")}, {"size", 32000000000LL}, {"fsType", "exfat"}, {"device", "/dev/sdb1"}}),
        dev({{"udi", "usb2"}, {"drive", usb}, {"label", "DATA"}, {"removable", true}, {"mountAt", at("Data")}, {"size", 1500000000LL}}),
        dev({{"udi", "usb3"}, {"drive", usb}, {"label", "EFI"}, {"removable", true}, {"partType", efi}, {"fsType", "vfat"}}),
        // an SD card, a DVD, an encrypted stick, a stick something keeps busy
        dev({{"udi", "sd1"}, {"drive", sd}, {"label", "CARD"}, {"removable", true}, {"driveType", "flash"}, {"mountAt", at("Card")}}),
        dev({{"udi", "dvd1"}, {"drive", dvd}, {"label", "DISC"}, {"optical", true}, {"mounted", true}, {"path", at("Disc")}, {"fsType", "iso9660"}}),
        dev({{"udi", "crypt1"}, {"drive", QStringLiteral("/fake/drives/crypt")}, {"label", "Secret"}, {"removable", true}, {"encrypted", true},
             {"usage", "encrypted"}, {"fsType", "crypto_LUKS"}, {"mountAt", at("Secret")}}),
        dev({{"udi", "crypt1-clear"}, {"drive", QStringLiteral("/fake/drives/crypt")}, {"label", "Secret"}, {"removable", true}, {"cleartext", true}}),
        dev({{"udi", "busy1"}, {"drive", busyDrive}, {"label", "BUSY"}, {"removable", true}, {"mounted", true}, {"path", at("Busy")}, {"busy", true}}),
        // the internal disk: only "Windows" is a place to open
        dev({{"udi", "root"}, {"drive", nvme}, {"label", "root"}, {"mounted", true}, {"path", "/"}, {"fsType", "btrfs"}}),
        dev({{"udi", "home"}, {"drive", nvme}, {"label", "home"}, {"mounted", true}, {"path", "/home"}}),
        dev({{"udi", "boot"}, {"drive", nvme}, {"label", "LIMINE"}, {"mounted", true}, {"path", "/boot"}, {"fsType", "vfat"}}),
        dev({{"udi", "esp"}, {"drive", nvme}, {"label", "SYSTEM"}, {"partType", efi}, {"fsType", "vfat"}}),
        dev({{"udi", "winre"}, {"drive", nvme}, {"label", "WinRE_DRV"}, {"partType", "de94bba4-06d1-4d40-a16a-bfd50179d6ac"}, {"fsType", "ntfs"}}),
        dev({{"udi", "swap"}, {"drive", nvme}, {"label", "swap"}, {"fsType", "swap"}, {"usage", "other"}}),
        dev({{"udi", "zram"}, {"device", "/dev/zram0"}, {"label", "zram0"}, {"fsType", "swap"}}),
        dev({{"udi", "snap"}, {"device", "/dev/loop3"}, {"label", "core22"}, {"loop", true}, {"mounted", true}, {"path", "/snap/core22/1"}, {"fsType", "squashfs"}}),
        dev({{"udi", "luks-sys"}, {"drive", nvme}, {"label", ""}, {"encrypted", true}, {"unlocked", true}, {"usage", "encrypted"}}),
        dev({{"udi", "hidden"}, {"drive", nvme}, {"label", "Hidden"}, {"ignored", true}}),
        dev({{"udi", "win"}, {"drive", nvme}, {"label", "Windows"}, {"fsType", "ntfs"}, {"size", 133822497280LL}, {"mountAt", at("Windows")}}),
        // phones and a camera
        QVariantMap{{"udi", phoneUdi}, {"player", true}, {"protocols", QStringList{"mtp"}}, {"label", "Pixel 8"}, {"icon", "phone"}},
        QVariantMap{{"udi", "/org/kde/solid/imobiledevice/00008030-ABC"}, {"player", true}, {"protocols", QStringList{"afc"}}, {"label", "iPhone"}},
        QVariantMap{{"udi", phoneUdi}, {"camera", true}, {"protocols", QStringList{"ptp"}}, {"label", "Pixel 8"}},   // same device: once
        QVariantMap{{"udi", "/fake/camera"}, {"camera", true}, {"protocols", QStringList{"ptp"}}, {"label", "EOS"}},
    };

    // --- the filter, without a window
    const QVariantList rows = Fm::deviceRows(raw);
    check(labels(rows) == QStringList{"STICK", "DATA", "CARD", "DISC", "Secret", "BUSY", "Pixel 8", "iPhone", "EOS", "Windows"},
          QStringLiteral("devices: plugged-in ones, then internal; no system ones"), labels(rows).join(", "));
    for (const char *p : {"/", "/home", "/boot", "/boot/efi", "/efi", "/var/lib/x", "/usr", "/run/user/1000/doc", "[SWAP]"})
        check(Fm::isSystemMount(QString::fromLatin1(p)), QStringLiteral("system mount: ") + QString::fromLatin1(p));
    for (const char *p : {"/run/media/me/STICK", "/media/STICK", "/mnt/data", "/home/me/mnt", ""})
        check(!Fm::isSystemMount(QString::fromLatin1(p)), QStringLiteral("not a system mount: ") + QString::fromLatin1(p));
    check(row(rows, "STICK").value("icon") == "drive-removable-media-symbolic" && row(rows, "CARD").value("icon") == "media-flash-symbolic"
              && row(rows, "DISC").value("icon") == "media-optical-symbolic" && row(rows, "Windows").value("icon") == "drive-harddisk-symbolic"
              && row(rows, "Pixel 8").value("icon") == "phone-symbolic" && row(rows, "EOS").value("icon") == "camera-photo-symbolic",
          QStringLiteral("each kind gets its symbolic icon"));
    check(row(rows, "Pixel 8").value("url") == QStringLiteral("mtp:udi=") + phoneUdi, QStringLiteral("an Android phone opens as mtp:udi=<udi>"),
          row(rows, "Pixel 8").value("url").toString());
    check(row(rows, "iPhone").value("url") == "afc://00008030-ABC/", QStringLiteral("an iPhone opens as afc://<id>/"), row(rows, "iPhone").value("url").toString());
    check(row(rows, "STICK").value("tip").toString().contains("32 GB") && row(rows, "STICK").value("tip").toString().contains("exfat"),
          QStringLiteral("the tooltip has the capacity and format"), row(rows, "STICK").value("tip").toString());
    check(row(rows, "Secret").value("encrypted").toBool() && !row(rows, "Secret").value("mounted").toBool(), QStringLiteral("a locked LUKS stick shows, not mounted"));
    check(row(rows, "Windows").value("internal").toBool() && !row(rows, "STICK").value("internal").toBool(), QStringLiteral("internal vs. plugged-in"));
    check(Fm::deviceRows({dev({{"udi", "x"}, {"size", 64000000000LL}, {"removable", true}})}).value(0).toMap().value("label") == "64 GB Volume",
          QStringLiteral("a volume without a label is named by its size"));

    // --- in the window
    const QString assets = QStringLiteral(IRIXFM_SOURCE_DIR);
    Fm fm(base, assets);
    fm.setFakeDevices(raw);
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("Fm"), &fm);
    engine.load(QUrl::fromLocalFile(assets + QStringLiteral("/qml/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return 2;
    QObject *win = engine.rootObjects().constFirst();
    auto *window = qobject_cast<QQuickWindow *>(win);
    auto dir = [&] { return win->property("dir").toString(); };
    auto go = [&](const QString &p) { QMetaObject::invokeMethod(win, "go", Q_ARG(QVariant, p), Q_ARG(QVariant, false)); QTest::qWait(200); };
    auto vol = [&](const QString &label) { return row(fm.volumes(), label); };
    auto activate = [&](const QString &label) { QMetaObject::invokeMethod(win, "activatePlace", Q_ARG(QVariant, QVariant(vol(label)))); };
    auto call = [&](const char *fn, const QString &label) { QMetaObject::invokeMethod(win, fn, Q_ARG(QVariant, QVariant(vol(label)))); };
    auto notice = [&] { return win->property("deviceNotice").toString(); };
    auto log = [&] { return fm.deviceLog(); };

    QTimer::singleShot(1200, &app, [&] {
        window->requestActivate();
        (void)QTest::qWaitForWindowActive(window);

        QStringList sidebar;
        bool afterLocations = false;
        for (const QVariant &r : win->property("placeRows").toList()) {
            const QVariantMap m = r.toMap();
            if (m.value("kind") == "head")
                afterLocations = m.value("title") == "Locations";
            if (m.value("kind") == "volume" && afterLocations)
                sidebar << m.value("item").toMap().value("label").toString();
        }
        check(sidebar == labels(fm.volumes()), QStringLiteral("every device is a row in Locations"), sidebar.join(", "));

        // click: a mounted one opens, an unmounted one mounts and opens
        activate("STICK");
        check(waitFor([&] { return dir() == at("Stick"); }), QStringLiteral("clicking a mounted stick opens it"), dir());
        activate("DATA");
        check(waitFor([&] { return dir() == at("Data"); }), QStringLiteral("clicking an unmounted partition mounts and opens it"), dir());
        check(log().contains("mount usb2") && vol("DATA").value("mounted").toBool(), QStringLiteral("…it's mounted now"), log().join(" | "));
        activate("Pixel 8");
        check(waitFor([&] { return dir() == QStringLiteral("mtp:udi=") + phoneUdi; }), QStringLiteral("clicking a phone opens its mtp: URL"), dir());
        check(win->property("tabModel").toList().value(win->property("tabIndex").toInt()).toMap().value("title") == "Pixel 8",
              QStringLiteral("…its tab is named after the phone"));
        activate("EOS");
        check(waitFor([&] { return notice().contains("EOS"); }), QStringLiteral("a camera without kio support: a notice, nothing breaks"), notice());

        // the device menu, per kind
        QObject *menu = win->findChild<QObject *>(QStringLiteral("volumeMenu"));
        auto item = [&](const char *name) { return menu ? menu->findChild<QObject *>(QString::fromLatin1(name)) : nullptr; };
        auto shown = [&](const char *name) { QObject *o = item(name); return o && o->property("visible").toBool(); };
        check(menu && item("volOpen") && item("volNewTab") && item("volMount") && item("volEject") && item("volInfo"), QStringLiteral("the device menu has its items"));
        if (menu) {
            menu->setProperty("vol", QVariant(vol("STICK")));
            check(shown("volMount") && item("volMount")->property("text").toString() == QStringLiteral("Unmount “STICK”")
                      && shown("volEject") && item("volEject")->property("text").toString() == QStringLiteral("Safely Remove “STICK”"),
                  QStringLiteral("a stick: Unmount and Safely Remove"), item("volMount") ? item("volMount")->property("text").toString() : QString());
            menu->setProperty("vol", QVariant(vol("Windows")));
            check(shown("volMount") && item("volMount")->property("text").toString().startsWith("Mount") && !shown("volEject"),
                  QStringLiteral("an internal partition: Mount, no Safely Remove"));
            menu->setProperty("vol", QVariant(vol("DISC")));
            check(!shown("volMount") && item("volEject")->property("text").toString() == QStringLiteral("Eject “DISC”"), QStringLiteral("a disc: Eject"));
            menu->setProperty("vol", QVariant(vol("Pixel 8")));
            check(!shown("volMount") && !shown("volEject") && shown("volOpen") && shown("volInfo"), QStringLiteral("a phone: Open, Get Info"));
        }

        // Safely Remove: fm leaves the volume (tab, split side, terminal) first, then every partition
        // of the drive is unmounted and the drive powered off.
        go(at("Stick/sub"));
        QMetaObject::invokeMethod(win, "openTerminal", Q_ARG(QVariant, QStringLiteral("shell")));
        QObject *term = nullptr;
        check(waitFor([&] {
                  const int id = win->property("activeTermId").toInt();
                  term = id ? win->property("terminals").toMap().value(QString::number(id)).value<QObject *>() : nullptr;
                  return term && term->property("running").toBool();
              }),
              QStringLiteral("(a terminal tab inside the stick)"));
        QTest::qWait(600);
        QMetaObject::invokeMethod(win, "newTab", Q_ARG(QVariant, at("Data")), Q_ARG(QVariant, true));
        QTest::qWait(200);
        QMetaObject::invokeMethod(win, "toggleSplit");
        QTest::qWait(200);
        QObject *split = win->findChild<QObject *>(QStringLiteral("splitPane"));
        if (split)
            QMetaObject::invokeMethod(split, "go", Q_ARG(QVariant, at("Stick")));
        QTest::qWait(200);
        const int before = log().size();
        call("ejectDevice", "STICK");
        check(waitFor([&] { return notice() == QStringLiteral("Safe to remove “STICK”"); }), QStringLiteral("Safely Remove ends with “Safe to remove”"), notice());
        check(log().mid(before) == QStringList{"unmount usb1", "unmount usb2", "poweroff " + usb},
              QStringLiteral("…after unmounting both partitions, then power off"), log().mid(before).join(" | "));
        check(dir() == fm.home(), QStringLiteral("…the tab inside it went home first"), dir());
        bool tabsOff = true;
        for (const QVariant &t : win->property("tabs").toList())
            tabsOff &= !t.toMap().value("dir").toString().startsWith(at("Stick")) && !t.toMap().value("dir").toString().startsWith(at("Data"));
        check(tabsOff, QStringLiteral("…and so did the other tabs"));
        check(split && split->property("dir").toString() == fm.home(), QStringLiteral("…and the split side"), split ? split->property("dir").toString() : QString());
        check(term && waitFor([&] { return term->property("currentDirectory").toString() == fm.home(); }), QStringLiteral("…and the terminal's shell"),
              term ? term->property("currentDirectory").toString() : QString());
        check(waitFor([&] { return vol("STICK").isEmpty() && vol("DATA").isEmpty(); }), QStringLiteral("…and the powered-off stick leaves Locations"));
        QMetaObject::invokeMethod(win, "toggleSplit");

        // busy: never forced; a clear message, no power off
        {
            QProcess holder;   // a program with its working directory on the volume
            holder.setWorkingDirectory(at("Busy"));
            holder.start(QStringLiteral("sleep"), {QStringLiteral("20")});
            holder.waitForStarted();
            QTest::qWait(100);
            check(Fm::blockers(at("Busy")).contains("sleep"), QStringLiteral("blockers() finds the program using it"), Fm::blockers(at("Busy")).join(", "));
            const int b = log().size();
            call("ejectDevice", "BUSY");
            check(waitFor([&] { return notice().contains("in use"); }), QStringLiteral("a busy volume: “in use” message"), notice());
            check(notice().contains("sleep"), QStringLiteral("…naming what blocks it"), notice());
            check(log().mid(b) == QStringList{"busy busy1", "busy busy1"} && vol("BUSY").value("mounted").toBool(),
                  QStringLiteral("…tried twice, still mounted, not powered off"), log().mid(b).join(" | "));
            holder.kill();
            holder.waitForFinished();
        }

        // a disc is ejected; an internal partition is only unmounted
        int b = log().size();
        call("ejectDevice", "DISC");
        check(waitFor([&] { return notice() == QStringLiteral("“DISC” ejected"); }), QStringLiteral("Eject on a disc"), notice());
        check(log().mid(b) == QStringList{"unmount dvd1", "eject " + dvd}, QStringLiteral("…unmounts, then ejects the tray"), log().mid(b).join(" | "));
        win->setProperty("mountThen", QStringLiteral("stay"));
        fm.mountVolume("win");
        check(waitFor([&] { return vol("Windows").value("mounted").toBool(); }), QStringLiteral("Mount (from the menu) mounts an internal partition"));
        check(dir() == fm.home(), QStringLiteral("…without opening it"), dir());
        b = log().size();
        call("ejectDevice", "Windows");
        check(waitFor([&] { return notice() == QStringLiteral("“Windows” unmounted"); }), QStringLiteral("⏏ on an internal partition unmounts it"), notice());
        check(log().mid(b) == QStringList{"unmount win"}, QStringLiteral("…and nothing is powered off"), log().mid(b).join(" | "));

        // Get Info
        const QVariantMap info = fm.volumeInfo("sd1");
        check(info.value("kind") == "Memory card" && info.value("mountPoint") == "Not mounted", QStringLiteral("Get Info describes a device"),
              info.value("kind").toString() + " " + info.value("mountPoint").toString());

        app.exit(failures ? 1 : 0);
    });
    return app.exec();
}
