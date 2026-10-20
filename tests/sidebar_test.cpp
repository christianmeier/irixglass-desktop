// Sidebar test: in a short window the places list scrolls (touchpad + wheel) so Indigo Magic and
// Locations stay reachable, and entries can be added, renamed, moved, removed and reset; the
// result is saved in the settings. Off-screen, run with `make test`.
#include "fm.h"

#include <QApplication>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointingDevice>
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
    std::printf("%s %-56s %s\n", ok ? "PASS" : "FAIL", qPrintable(what), ok ? "" : qPrintable("got: " + got));
    failures += ok ? 0 : 1;
}

QJsonArray places(QObject *win)
{
    // win.places is a JS array; its JSON form is in placesJson (or the defaults when empty).
    const QString json = win->property("placesJson").toString();
    if (!json.isEmpty())
        return QJsonDocument::fromJson(json.toUtf8()).array();
    QVariant v;
    QMetaObject::invokeMethod(win, "defaultPlaces", Q_RETURN_ARG(QVariant, v));
    return QJsonArray::fromVariantList(v.toList());
}

QStringList labels(QObject *win, int si)
{
    QStringList out;
    for (const auto &it : places(win).at(si).toObject().value(QStringLiteral("items")).toArray()) {
        const QJsonObject o = it.toObject();
        out << (o.value(QStringLiteral("label")).toString().isEmpty() ? o.value(QStringLiteral("path")).toString()
                                                                       : o.value(QStringLiteral("label")).toString());
    }
    return out;
}

void call(QObject *win, const char *fn, QVariantList args)
{
    switch (args.size()) {
    case 1: QMetaObject::invokeMethod(win, fn, Q_ARG(QVariant, args[0])); break;
    case 2: QMetaObject::invokeMethod(win, fn, Q_ARG(QVariant, args[0]), Q_ARG(QVariant, args[1])); break;
    case 3: QMetaObject::invokeMethod(win, fn, Q_ARG(QVariant, args[0]), Q_ARG(QVariant, args[1]), Q_ARG(QVariant, args[2])); break;
    default: QMetaObject::invokeMethod(win, fn); break;
    }
    QCoreApplication::processEvents();
}
} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("irixfm-test"));
    QQuickStyle::setStyle(QStringLiteral("org.kde.desktop"));
    QTemporaryDir tmp;
    const QString base = QDir(tmp.path()).canonicalPath();
    for (const char *d : {"Work", "Music"})
        QDir(base).mkdir(QString::fromLatin1(d));

    const QString assets = QStringLiteral(IRIXFM_SOURCE_DIR);
    Fm fm(base, assets);
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("Fm"), &fm);
    engine.load(QUrl::fromLocalFile(assets + QStringLiteral("/qml/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return 2;
    QObject *win = engine.rootObjects().constFirst();
    auto *window = qobject_cast<QQuickWindow *>(win);
    auto *list = win->findChild<QQuickItem *>(QStringLiteral("places"));

    const QPointingDevice touchpad(QStringLiteral("test touchpad"), 9001, QInputDevice::DeviceType::TouchPad,
                                   QPointingDevice::PointerType::Finger,
                                   QInputDevice::Capability::Position | QInputDevice::Capability::Scroll
                                       | QInputDevice::Capability::PixelScroll,
                                   2, 0);

    QTimer::singleShot(1200, &app, [&] {
        // --- scrolling in a short window
        window->setHeight(window->minimumHeight());
        QTest::qWait(200);
        const double listH = list->height(), contentH = list->property("contentHeight").toDouble();
        check(contentH > listH, QStringLiteral("short window: the list is taller than the sidebar"),
              QString::number(contentH) + " vs " + QString::number(listH));
        const QPointF pos = list->mapToScene(QPointF(list->width() / 2, list->height() / 2));
        for (int i = 0; i < 20; ++i)
            for (auto phase : {Qt::ScrollBegin, Qt::ScrollUpdate, Qt::ScrollEnd}) {
                QWheelEvent ev(pos, window->mapToGlobal(pos), phase == Qt::ScrollUpdate ? QPoint(0, -40) : QPoint(),
                               phase == Qt::ScrollUpdate ? QPoint(0, -48) : QPoint(), Qt::NoButton, Qt::NoModifier, phase, false,
                               Qt::MouseEventNotSynthesized, &touchpad);
                QCoreApplication::sendEvent(window, &ev);
            }
        QCoreApplication::processEvents();
        const double y = list->property("contentY").toDouble();
        const double end = list->property("originY").toDouble() + contentH - listH;
        check(y > 0 && qAbs(y - end) < 1, QStringLiteral("touchpad scrolls the sidebar to the end (Locations)"),
              QString::number(y) + " / " + QString::number(end));
        window->setHeight(830 * .85);
        QTest::qWait(200);

        // --- editing
        const QStringList fav0 = labels(win, 0);
        call(win, "placeAdd", {0, base + QStringLiteral("/Work"), -1});
        check(labels(win, 0).size() == fav0.size() + 1 && labels(win, 0).last().endsWith(QStringLiteral("/Work")),
              QStringLiteral("add a folder to Favorites"), labels(win, 0).join(", "));
        call(win, "placeAdd", {0, base + QStringLiteral("/Work"), -1});
        check(labels(win, 0).size() == fav0.size() + 1, QStringLiteral("the same folder isn't added twice"));
        call(win, "placeAdd", {1, base + QStringLiteral("/Music"), 0});
        check(labels(win, 1).first().endsWith(QStringLiteral("/Music")), QStringLiteral("add to Indigo Magic at the top"), labels(win, 1).join(", "));
        call(win, "placeRename", {1, 0, QStringLiteral("Tunes")});
        check(labels(win, 1).first() == QStringLiteral("Tunes"), QStringLiteral("rename an entry"), labels(win, 1).join(", "));
        call(win, "placeMove", {1, 0, 1});
        check(labels(win, 1).at(1) == QStringLiteral("Tunes"), QStringLiteral("move an entry down"), labels(win, 1).join(", "));
        call(win, "placeMoveTo", {1, 1, 2});
        check(labels(win, 2).last() == QStringLiteral("Tunes") && !labels(win, 1).contains(QStringLiteral("Tunes")),
              QStringLiteral("move an entry to Locations"), labels(win, 2).join(", "));
        const int locs = labels(win, 2).size();
        call(win, "placeRemove", {2, locs - 1});
        check(labels(win, 2).size() == locs - 1, QStringLiteral("remove an entry"), labels(win, 2).join(", "));
        call(win, "placeRemove", {2, 0});
        call(win, "placeRemove", {2, 0});
        QQuickItem *empty = nullptr;
        QTest::qWait(100);
        // Delegates are visual children of the list's content item.
        auto *content = list->property("contentItem").value<QQuickItem *>();
        for (auto *it : content->childItems())
            if (it->property("modelData").toMap().value(QStringLiteral("kind")) == QStringLiteral("empty"))
                empty = it;
        check(labels(win, 2).isEmpty() && empty, QStringLiteral("an emptied section shows a drop hint"));
        call(win, "dropOnPlaces", {QVariantList{QUrl::fromLocalFile(base + QStringLiteral("/Work"))}, 2, 0});
        check(labels(win, 2).size() == 1, QStringLiteral("dropping a folder on a section adds it"), labels(win, 2).join(", "));

        // --- clicking an entry opens it
        call(win, "activatePlace", {places(win).at(2).toObject().value(QStringLiteral("items")).toArray().at(0).toObject().toVariantMap()});
        check(win->property("dir").toString() == base + QStringLiteral("/Work"), QStringLiteral("clicking an entry opens the folder"),
              win->property("dir").toString());

        // --- saved, and reset
        check(!win->property("placesJson").toString().isEmpty(), QStringLiteral("changes are kept in the settings"));
        call(win, "resetPlaces", {});
        check(labels(win, 2).size() == 2 && labels(win, 1).size() == 3, QStringLiteral("Reset Sidebar restores the defaults"));
        app.exit(failures ? 1 : 0);
    });
    return app.exec();
}
