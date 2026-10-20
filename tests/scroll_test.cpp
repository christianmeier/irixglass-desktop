// Scroll test: loads fm off-screen, sends simulated touchpad and mouse wheel events, and checks
// that the icon view, the list view and the "Icon size" thumbwheel react. Run with `make test`.
#include "fm.h"

#include <QApplication>
#include <QPointingDevice>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>
#include <cstdio>

namespace
{
int failures = 0;

void check(bool ok, const char *what, double before, double after)
{
    std::printf("%s %-46s %8.3f -> %8.3f\n", ok ? "PASS" : "FAIL", what, before, after);
    failures += ok ? 0 : 1;
}

// One gesture: touchpads send begin/update/end phases with pixel deltas, mice a single notch.
void wheel(QQuickWindow *w, const QPointingDevice *dev, QPointF pos, QPoint pixel, QPoint angle)
{
    const bool touchpad = dev->type() == QInputDevice::DeviceType::TouchPad;
    for (auto phase : {Qt::ScrollBegin, Qt::ScrollUpdate, Qt::ScrollEnd}) {
        const bool update = phase == Qt::ScrollUpdate || !touchpad;
        QWheelEvent ev(pos, w->mapToGlobal(pos), update ? pixel : QPoint(), update ? angle : QPoint(), Qt::NoButton, Qt::NoModifier,
                       touchpad ? phase : Qt::NoScrollPhase, false, Qt::MouseEventNotSynthesized, dev);
        QCoreApplication::sendEvent(w, &ev);
        if (!touchpad)
            break;
    }
    QCoreApplication::processEvents();
}
} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("irixfm-test"));
    QQuickStyle::setStyle(QStringLiteral("org.kde.desktop"));
    const QString assets = QStringLiteral(IRIXFM_SOURCE_DIR);
    Fm fm(argc > 1 ? QString::fromLocal8Bit(argv[1]) : QStringLiteral("/usr/share/applications"), assets);
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("Fm"), &fm);
    engine.load(QUrl::fromLocalFile(assets + QStringLiteral("/qml/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return 2;
    QObject *root = engine.rootObjects().constFirst();
    auto *w = qobject_cast<QQuickWindow *>(root);

    const QPointingDevice touchpad(QStringLiteral("test touchpad"), 9001, QInputDevice::DeviceType::TouchPad,
                                   QPointingDevice::PointerType::Finger,
                                   QInputDevice::Capability::Position | QInputDevice::Capability::Scroll
                                       | QInputDevice::Capability::PixelScroll,
                                   2, 0);
    const QPointingDevice *mouse = QPointingDevice::primaryPointingDevice();

    QTimer::singleShot(1500, &app, [&] {
        const QPointF content(w->width() * .6, w->height() * .5);
        for (int mode : {0, 1}) {
            root->setProperty("viewMode", mode);
            QCoreApplication::processEvents();
            auto *view = root->findChild<QQuickItem *>(mode ? QStringLiteral("list") : QStringLiteral("grid"));
            const char *name = mode ? "list" : "icons";
            const double top = view->property("contentY").toDouble();
            wheel(w, &touchpad, content, QPoint(0, -40), QPoint(0, -48));  // two fingers up = scroll down
            const double afterTouchpad = view->property("contentY").toDouble();
            check(afterTouchpad > top, qPrintable(QStringLiteral("%1 view scrolls with the touchpad").arg(name)), top, afterTouchpad);
            wheel(w, mouse, content, QPoint(), QPoint(0, -120));
            const double afterMouse = view->property("contentY").toDouble();
            check(afterMouse > afterTouchpad, qPrintable(QStringLiteral("%1 view scrolls with a mouse wheel").arg(name)), afterTouchpad,
                  afterMouse);
            for (int i = 0; i < 300; ++i)
                wheel(w, &touchpad, content, QPoint(0, 80), QPoint(0, 96));
            const double back = view->property("contentY").toDouble();
            check(qAbs(back - top) < 1, qPrintable(QStringLiteral("%1 view stops at the top").arg(name)), afterMouse, back);
        }

        // The thumbwheel sits at the bottom right (see Main.qml): x = width - 216u, y = height - 42u, 190u x 22u.
        const double u = root->property("u").toDouble();
        const QPointF thumb(w->width() - 216 * u + 95 * u, w->height() - 42 * u + 11 * u);
        for (auto [what, px] : {std::pair{"thumbwheel rolls with a vertical swipe", QPoint(0, 30)},
                                std::pair{"thumbwheel rolls with a sideways swipe", QPoint(30, 0)}}) {
            const double before = root->property("wheelValue").toDouble();
            wheel(w, &touchpad, thumb, px, px * 6 / 5);
            const double after = root->property("wheelValue").toDouble();
            check(after - before > .05, what, before, after);
        }
        app.exit(failures ? 1 : 0);
    });
    return app.exec();
}
