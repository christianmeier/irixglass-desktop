#include "fm.h"

#include <QClipboard>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QLocale>
#include <QMimeData>
#include <QMimeDatabase>
#include <QPainter>
#include <QPainterPath>
#include <QProcess>
#include <QQuickWindow>
#include <QRegion>
#include <QStorageInfo>

#include <KDesktopFile>
#include <KIO/CopyJob>
#include <KIO/JobUiDelegateFactory>
#include <KIO/MkdirJob>
#include <KIO/OpenUrlJob>
#include <KJobTrackerInterface>
#include <KJobUiDelegate>
#include <KWindowEffects>
#include <KWindowShadow>
#include <KIO/JobTracker>

namespace
{
QList<QUrl> toUrls(const QStringList &paths)
{
    QList<QUrl> urls;
    for (const QString &p : paths)
        urls << QUrl::fromLocalFile(p);
    return urls;
}

void track(KJob *job)
{
    job->setUiDelegate(KIO::createDefaultJobUiDelegate(KJobUiDelegate::AutoHandlingEnabled, nullptr));
    KIO::getJobTracker()->registerJob(job);
}

// Rounded rectangle as a region, built row by row so corners follow the curve.
QRegion roundedRegion(int w, int h, int r)
{
    QRegion region;
    for (int y = 0; y < h; ++y) {
        int inset = 0;
        const double dy = y < r ? r - y - 0.5 : (y >= h - r ? y - (h - r) + 0.5 : -1);
        if (dy >= 0)
            inset = int(std::ceil(r - std::sqrt(std::max(0.0, double(r) * r - dy * dy))));
        region += QRect(inset, y, w - 2 * inset, 1);
    }
    return region;
}

// Separable box blur on the alpha channel, three passes (close to a Gaussian).
void blurAlpha(QImage &img, int radius)
{
    const int w = img.width(), h = img.height();
    std::vector<int> a(w * h), t(w * h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            a[y * w + x] = qAlpha(img.pixel(x, y));
    for (int pass = 0; pass < 3; ++pass) {
        for (int y = 0; y < h; ++y) {
            int sum = 0;
            for (int x = -radius; x <= radius; ++x)
                sum += a[y * w + std::clamp(x, 0, w - 1)];
            for (int x = 0; x < w; ++x) {
                t[y * w + x] = sum / (2 * radius + 1);
                sum += a[y * w + std::min(x + radius + 1, w - 1)] - a[y * w + std::max(x - radius, 0)];
            }
        }
        for (int x = 0; x < w; ++x) {
            int sum = 0;
            for (int y = -radius; y <= radius; ++y)
                sum += t[std::clamp(y, 0, h - 1) * w + x];
            for (int y = 0; y < h; ++y) {
                a[y * w + x] = sum / (2 * radius + 1);
                sum += t[std::min(y + radius + 1, h - 1) * w + x] - t[std::max(y - radius, 0) * w + x];
            }
        }
    }
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            img.setPixel(x, y, qRgba(0, 0, 0, a[y * w + x]));
}

KWindowShadowTile::Ptr tile(const QImage &img, const QRect &r)
{
    auto t = KWindowShadowTile::Ptr::create();
    t->setImage(img.copy(r));
    t->create();
    return t;
}
} // namespace

Fm::Fm(const QString &startDir, const QString &assetDir, QObject *parent)
    : QObject(parent)
    , m_startDir(startDir)
    , m_assetDir(assetDir)
{
}

QString Fm::home() const
{
    return QDir::homePath();
}

QString Fm::userName() const
{
    return QDir::home().dirName();
}

void Fm::updateWindow(QQuickWindow *window, int radius, bool shadow)
{
    if (!window)
        return;
    const QRegion shape = roundedRegion(window->width(), window->height(), radius);
    KWindowEffects::enableBlurBehind(window, true, shape);
    window->setMask(shape);

    if (!shadow) {
        if (m_shadow)
            m_shadow->destroy();
        return;
    }
    if (m_shadow && m_shadow->isCreated())
        return;
    if (!m_shadow) {
        // Shadow image: a blurred rounded rect, shifted down; tiles are cut around the window frame.
        const int pad = 64, inner = 2 * radius + 4, offset = 18, blur = 18;
        QImage img(2 * pad + inner, 2 * pad + inner, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::transparent);
        {
            QPainter p(&img);
            p.setRenderHint(QPainter::Antialiasing);
            QPainterPath path;
            path.addRoundedRect(QRectF(pad, pad + offset, inner, inner), radius, radius);
            p.fillPath(path, QColor(0, 0, 0, 150));
        }
        blurAlpha(img, blur / 2);
        const int mid = pad + inner / 2, end = pad + inner;
        m_shadow = new KWindowShadow(this);
        m_shadow->setTopLeftTile(tile(img, QRect(0, 0, pad, pad)));
        m_shadow->setTopTile(tile(img, QRect(mid, 0, 1, pad)));
        m_shadow->setTopRightTile(tile(img, QRect(end, 0, pad, pad)));
        m_shadow->setRightTile(tile(img, QRect(end, mid, pad, 1)));
        m_shadow->setBottomRightTile(tile(img, QRect(end, end, pad, pad)));
        m_shadow->setBottomTile(tile(img, QRect(mid, end, 1, pad)));
        m_shadow->setBottomLeftTile(tile(img, QRect(0, end, pad, pad)));
        m_shadow->setLeftTile(tile(img, QRect(0, mid, pad, 1)));
        m_shadow->setPadding(QMargins(pad, pad, pad, pad));
        m_shadow->setWindow(window);
    }
    m_shadow->create();
}

QString Fm::kind(const QString &path) const
{
    const QFileInfo fi(path);
    if (fi.isDir())
        return QStringLiteral("folder");
    if (fi.suffix() == QLatin1String("desktop"))
        return QStringLiteral("app");
    const QMimeType mime = QMimeDatabase().mimeTypeForFile(fi);
    if (mime.name().startsWith(QLatin1String("image/")))
        return QStringLiteral("image");
    if (fi.isExecutable() || mime.inherits(QStringLiteral("application/x-executable"))
        || mime.inherits(QStringLiteral("application/x-shellscript")))
        return QStringLiteral("exe");
    return QStringLiteral("doc");
}

QVariantMap Fm::desktopInfo(const QString &path) const
{
    const KDesktopFile df(path);
    return {{QStringLiteral("name"), df.readName()}, {QStringLiteral("icon"), df.readIcon()}};
}

QString Fm::freeSpace(const QString &path) const
{
    const QStorageInfo info(path);
    return humanSize(info.bytesAvailable()) + QStringLiteral(" available");
}

QString Fm::humanSize(qint64 bytes) const
{
    return QLocale().formattedDataSize(bytes, 0, QLocale::DataSizeSIFormat);
}

QString Fm::uriList(const QStringList &paths) const
{
    QStringList lines;
    for (const QUrl &u : toUrls(paths))
        lines << QString::fromUtf8(u.toEncoded());
    return lines.join(QStringLiteral("\r\n"));
}

void Fm::open(const QString &path)
{
    auto *job = new KIO::OpenUrlJob(QUrl::fromLocalFile(path));
    job->setRunExecutables(true);
    job->setUiDelegate(KIO::createDefaultJobUiDelegate(KJobUiDelegate::AutoHandlingEnabled, nullptr));
    job->start();
}

void Fm::openInDolphin(const QString &path)
{
    QProcess::startDetached(QStringLiteral("dolphin"), {path});
}

void Fm::openTerminal(const QString &dir)
{
    QProcess::startDetached(QStringLiteral("alacritty"), {QStringLiteral("--working-directory"), dir});
}

void Fm::newWindow(const QString &dir)
{
    QProcess::startDetached(QCoreApplication::applicationFilePath(), {dir});
}

void Fm::overview()
{
    QProcess::startDetached(QStringLiteral("qdbus6"),
                            {QStringLiteral("org.kde.kglobalaccel"), QStringLiteral("/component/kwin"),
                             QStringLiteral("org.kde.kglobalaccel.Component.invokeShortcut"), QStringLiteral("Overview")});
}

void Fm::trash(const QStringList &paths)
{
    if (!paths.isEmpty())
        track(KIO::trash(toUrls(paths)));
}

void Fm::copyToClipboard(const QStringList &paths)
{
    auto *mime = new QMimeData;
    mime->setUrls(toUrls(paths));
    QGuiApplication::clipboard()->setMimeData(mime);
}

void Fm::copyPathText(const QString &text)
{
    QGuiApplication::clipboard()->setText(text);
}

bool Fm::canPaste() const
{
    const QMimeData *mime = QGuiApplication::clipboard()->mimeData();
    return mime && mime->hasUrls();
}

void Fm::paste(const QString &dir)
{
    const QMimeData *mime = QGuiApplication::clipboard()->mimeData();
    if (mime && mime->hasUrls())
        track(KIO::copy(mime->urls(), QUrl::fromLocalFile(dir)));
}

void Fm::drop(const QList<QUrl> &urls, const QString &dir, bool move)
{
    QList<QUrl> todo;
    for (const QUrl &u : urls) {
        // Dropping an item onto its own folder (or onto itself) is a no-op.
        const QString local = u.toLocalFile();
        if (QFileInfo(local).absolutePath() == QDir(dir).absolutePath() || local == dir)
            continue;
        todo << u;
    }
    if (todo.isEmpty())
        return;
    const QUrl dest = QUrl::fromLocalFile(dir);
    track(move ? static_cast<KJob *>(KIO::move(todo, dest)) : static_cast<KJob *>(KIO::copy(todo, dest)));
}

QString Fm::makeFolder(const QString &dir)
{
    QString name = QStringLiteral("New Folder");
    for (int i = 2; QFileInfo::exists(dir + QLatin1Char('/') + name); ++i)
        name = QStringLiteral("New Folder %1").arg(i);
    const QString path = dir + QLatin1Char('/') + name;
    track(KIO::mkdir(QUrl::fromLocalFile(path)));
    return path;
}

void Fm::rename(const QString &path, const QString &newName)
{
    const QFileInfo fi(path);
    if (newName.isEmpty() || newName.contains(QLatin1Char('/')) || newName == fi.fileName())
        return;
    track(KIO::moveAs(QUrl::fromLocalFile(path), QUrl::fromLocalFile(fi.absolutePath() + QLatin1Char('/') + newName),
                      KIO::HideProgressInfo));
}
