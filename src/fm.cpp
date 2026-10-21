#include "fm.h"

#include <QClipboard>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QImageReader>
#include <QLocale>
#include <QMimeData>
#include <QMimeDatabase>
#include <QPainter>
#include <QPainterPath>
#include <QProcess>
#include <QQuickWindow>
#include <QRegion>
#include <QDirIterator>
#include <QRegularExpression>
#include <QTextDocument>
#include <KSyntaxHighlighting/AbstractHighlighter>
#include <KSyntaxHighlighting/Definition>
#include <KSyntaxHighlighting/Format>
#include <KSyntaxHighlighting/Repository>
#include <KSyntaxHighlighting/State>
#include <KSyntaxHighlighting/Theme>
#include <QFileSystemWatcher>
#include <KConfig>
#include <KConfigGroup>
#include <QStandardPaths>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTimer>
#include <QJsonObject>
#include <QStorageInfo>
#include <sys/xattr.h>
#include <QtConcurrent/QtConcurrentRun>

#include <K7Zip>
#include <KTar>
#include <KZip>
#include <KIO/ApplicationLauncherJob>
#include <KIO/DeleteJob>
#include <KIO/EmptyTrashJob>
#include <KIO/RestoreJob>
#include <Solid/Device>
#include <Solid/DeviceNotifier>
#include <Solid/OpticalDrive>
#include <Solid/StorageAccess>
#include <Solid/StorageDrive>
#include <Solid/StorageVolume>

#include <KDesktopFile>
#include <KApplicationTrader>
#include <KFileItem>
#include <KIO/CopyJob>
#include <KIO/DirectorySizeJob>
#include <KIO/FileUndoManager>
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

// "~", "~/x", "file:///x", "rel/x" -> absolute, cleaned path (not checked for existence).
QString expandPath(QString text, const QString &base)
{
    text = text.trimmed();
    if (text.startsWith(QLatin1String("file:")))
        text = QUrl(text).toLocalFile();
    if (text == QLatin1String("~") || text.startsWith(QLatin1String("~/")))
        text = QDir::homePath() + text.mid(1);
    if (text.isEmpty())
        text = base;
    if (!QDir::isAbsolutePath(text))
        text = base + QLatin1Char('/') + text;
    return QDir::cleanPath(text);
}

// IRIXFM_NO_DIALOGS (tests): report job errors on stderr instead of opening dialogs, which would
// block an off-screen run.
const bool kNoDialogs = qEnvironmentVariableIsSet("IRIXFM_NO_DIALOGS");

void track(KJob *job)
{
    if (kNoDialogs) {
        QObject::connect(job, &KJob::result, job, [job] { if (job->error()) qWarning("irixfm job error: %s", qPrintable(job->errorString())); });
        return;
    }
    job->setUiDelegate(KIO::createDefaultJobUiDelegate(KJobUiDelegate::AutoHandlingEnabled, nullptr));
    KIO::getJobTracker()->registerJob(job);
}

// Copy/move/trash/rename jobs are recorded so Edit ▸ Undo can reverse them.
KIO::CopyJob *undoable(KIO::CopyJob *job)
{
    KIO::FileUndoManager::self()->recordCopyJob(job);
    track(job);
    return job;
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

Fm::Fm(const QString &startDir, const QString &assetDir, QObject *parent, bool restoreSession)
    : QObject(parent)
    , m_startDir(startDir)
    , m_restoreSession(restoreSession)
    , m_assetDir(assetDir)
{
    connect(KIO::FileUndoManager::self(), &KIO::FileUndoManager::undoAvailable, this, &Fm::undoChanged);
    connect(KIO::FileUndoManager::self(), &KIO::FileUndoManager::undoTextChanged, this, &Fm::undoChanged);
    connect(Solid::DeviceNotifier::instance(), &Solid::DeviceNotifier::deviceAdded, this, &Fm::refreshVolumes);
    connect(Solid::DeviceNotifier::instance(), &Solid::DeviceNotifier::deviceRemoved, this, &Fm::refreshVolumes);
    refreshVolumes();
    loadTagIndex();
    loadSizeCache();
    // The dock's size: from Plasma's panel settings, re-read whenever they change.
    m_panelWatcher = new QFileSystemWatcher(this);
    const QString cfg = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    m_panelWatcher->addPath(cfg);   // KConfig replaces files on save, so watch the folder
    connect(m_panelWatcher, &QFileSystemWatcher::directoryChanged, this, &Fm::readDockReserve);
    readDockReserve();
}

void Fm::readDockReserve()
{
    // Bottom panels (location 4) whose visibility lets windows go below them — "Windows Go Below"
    // (3) and "Auto Hide" / "Dodge Windows" (1, 2) — don't reserve space, so a maximized window
    // ends under them. "Always Visible" (0) panels already shrink the maximized area.
    const KConfig applets(QStringLiteral("plasma-org.kde.plasma.desktop-appletsrc"), KConfig::SimpleConfig);
    const KConfig shell(QStringLiteral("plasmashellrc"), KConfig::SimpleConfig);
    const KConfigGroup containments = applets.group(QStringLiteral("Containments"));
    int reserve = 0;
    for (const QString &id : containments.groupList()) {
        const KConfigGroup c = containments.group(id);
        if (c.readEntry("plugin") != QLatin1String("org.kde.panel") || c.readEntry("location", 0) != 4)
            continue;
        const KConfigGroup view = shell.group(QStringLiteral("PlasmaViews")).group(QStringLiteral("Panel ") + id);
        if (view.readEntry("panelVisibility", 0) == 0)
            continue;
        const int thickness = view.group(QStringLiteral("Defaults")).readEntry("thickness", 44);
        const bool floating = view.readEntry("floating", 1) != 0;
        reserve = std::max(reserve, thickness + (floating ? 16 : 4));   // a floating panel sits ~8 px up, plus a gap
    }
    if (reserve != m_dockReserve) {
        m_dockReserve = reserve;
        Q_EMIT dockReserveChanged();
    }
}

// ---- Trash ---------------------------------------------------------------------------

QString Fm::trashDir() const
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/Trash/files");
}

void Fm::restoreFromTrash(const QStringList &paths)
{
    // Items in the home trash are trash:/0-<name> to KIO; it reads the .trashinfo for the original place.
    QList<QUrl> urls;
    for (const QString &p : paths)
        urls << QUrl(QStringLiteral("trash:/0-") + QFileInfo(p).fileName());
    if (!urls.isEmpty())
        track(KIO::restoreFromTrash(urls));
}

void Fm::emptyTrash()
{
    track(KIO::emptyTrash());
}

void Fm::deletePermanently(const QStringList &paths)
{
    if (!paths.isEmpty())
        track(KIO::del(toUrls(paths)));
}

int Fm::trashCount() const
{
    return QDir(trashDir()).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System).size();
}

// ---- Open With ---------------------------------------------------------------------------

QVariantList Fm::appsFor(const QString &path) const
{
    const QString mime = QMimeDatabase().mimeTypeForFile(path).name();
    QVariantList out;
    for (const KService::Ptr &app : KApplicationTrader::queryByMimeType(mime)) {
        out << QVariantMap{{QStringLiteral("id"), app->storageId()}, {QStringLiteral("name"), app->name()}, {QStringLiteral("icon"), app->icon()}};
        if (out.size() >= 12)
            break;
    }
    return out;
}

void Fm::openWith(const QStringList &paths, const QString &appId)
{
    const KService::Ptr app = appId.isEmpty() ? KService::Ptr() : KService::serviceByStorageId(appId);
    // Without a service, ApplicationLauncherJob asks with KDE's "Open With" dialog.
    auto *job = app ? new KIO::ApplicationLauncherJob(app) : new KIO::ApplicationLauncherJob();
    job->setUrls(toUrls(paths));
    job->setUiDelegate(KIO::createDefaultJobUiDelegate(KJobUiDelegate::AutoHandlingEnabled, nullptr));
    job->start();
}

// ---- Removable drives ------------------------------------------------------------------

QVariantList Fm::volumes() const
{
    return m_volumes;
}

void Fm::refreshVolumes()
{
    QVariantList list;
    for (const Solid::Device &dev : Solid::Device::listFromType(Solid::DeviceInterface::StorageAccess)) {
        const auto *vol = dev.as<Solid::StorageVolume>();
        const auto *access = dev.as<Solid::StorageAccess>();
        if (!access || (vol && vol->isIgnored()))
            continue;
        // Only hot-pluggable or removable media (USB sticks, SD cards, discs), like Finder's Locations.
        Solid::Device drive = dev;
        while (drive.isValid() && !drive.is<Solid::StorageDrive>())
            drive = drive.parent();
        const auto *sd = drive.isValid() ? drive.as<Solid::StorageDrive>() : nullptr;
        if (!sd || !(sd->isHotpluggable() || sd->isRemovable()))
            continue;
        list << QVariantMap{{QStringLiteral("udi"), dev.udi()},
                            {QStringLiteral("label"), dev.description()},
                            {QStringLiteral("icon"), dev.icon()},
                            {QStringLiteral("mounted"), access->isAccessible()},
                            {QStringLiteral("path"), access->filePath()}};
        // Keep the list current when it gets mounted or unmounted elsewhere.
        connect(access, &Solid::StorageAccess::accessibilityChanged, this, &Fm::refreshVolumes, Qt::UniqueConnection);
    }
    if (list != m_volumes) {
        m_volumes = list;
        Q_EMIT volumesChanged();
    }
}

void Fm::mountVolume(const QString &udi)
{
    Solid::Device dev(udi);
    auto *access = dev.as<Solid::StorageAccess>();
    if (!access)
        return;
    if (access->isAccessible()) {
        Q_EMIT volumeMounted(udi, access->filePath());
        return;
    }
    connect(access, &Solid::StorageAccess::setupDone, this, [this, udi](Solid::ErrorType error, const QVariant &details) {
        Solid::Device d(udi);
        if (error == Solid::NoError)
            Q_EMIT volumeMounted(udi, d.as<Solid::StorageAccess>()->filePath());
        else
            Q_EMIT volumeError(details.toString());
        refreshVolumes();
    }, Qt::SingleShotConnection);
    access->setup();
}

void Fm::ejectVolume(const QString &udi)
{
    Solid::Device dev(udi);
    auto *access = dev.as<Solid::StorageAccess>();
    if (!access)
        return;
    connect(access, &Solid::StorageAccess::teardownDone, this, [this, udi](Solid::ErrorType error, const QVariant &details) {
        if (error != Solid::NoError) {
            Q_EMIT volumeError(details.toString());
            return;
        }
        // Discs come out of the drive; USB drives are just unmounted (safe to unplug).
        Solid::Device d(udi);
        for (Solid::Device p = d; p.isValid(); p = p.parent())
            if (auto *optical = p.as<Solid::OpticalDrive>()) {
                optical->eject();
                break;
            }
        refreshVolumes();
    }, Qt::SingleShotConnection);
    access->teardown();
}

// ---- Search through subfolders -------------------------------------------------------------

void Fm::startSearch(const QString &root, const QString &text, bool hidden, bool recursive)
{
    const int gen = ++m_searchGen;
    if (text.trimmed().isEmpty())
        return;
    QPointer<Fm> self(this);
    (void)QtConcurrent::run([self, gen, root, needle = text.trimmed(), hidden, recursive] {
        constexpr int kMax = 2000, kBatch = 50;
        QVariantList batch;
        int count = 0;
        bool truncated = false;
        auto flush = [&] {
            if (!batch.isEmpty() && self)
                QMetaObject::invokeMethod(self, [self, gen, b = batch] { if (self && self->m_searchGen == gen) Q_EMIT self->searchBatch(b); });
            batch.clear();
        };
        QDir::Filters filters = QDir::AllEntries | QDir::NoDotAndDotDot | QDir::NoSymLinks;
        if (hidden)
            filters |= QDir::Hidden;
        QDirIterator it(root, filters, recursive ? QDirIterator::Subdirectories : QDirIterator::NoIteratorFlags);
        while (it.hasNext()) {
            if (!self || self->m_searchGen != gen)
                return;
            it.next();
            const QFileInfo fi = it.fileInfo();
            if (!fi.fileName().contains(needle, Qt::CaseInsensitive))
                continue;
            batch << QVariantMap{{QStringLiteral("name"), fi.fileName()}, {QStringLiteral("path"), fi.absoluteFilePath()},
                                 {QStringLiteral("where"), fi.absolutePath()}, {QStringLiteral("isDir"), fi.isDir()},
                                 {QStringLiteral("modified"), fi.lastModified()}, {QStringLiteral("size"), fi.isDir() ? -1 : fi.size()}};
            if (++count >= kMax) {
                truncated = true;
                break;
            }
            if (batch.size() >= kBatch)
                flush();
        }
        flush();
        if (self)
            QMetaObject::invokeMethod(self, [self, gen, count, truncated] { if (self && self->m_searchGen == gen) Q_EMIT self->searchDone(count, truncated); });
    });
}

void Fm::stopSearch()
{
    ++m_searchGen;
}

// ---- Compress / expand ----------------------------------------------------------------------

namespace
{
std::unique_ptr<KArchive> archiveFor(const QString &path)
{
    const QMimeType mime = QMimeDatabase().mimeTypeForFile(path);
    if (mime.inherits(QStringLiteral("application/zip")))
        return std::make_unique<KZip>(path);
    if (mime.inherits(QStringLiteral("application/x-7z-compressed")))
        return std::make_unique<K7Zip>(path);
    if (mime.inherits(QStringLiteral("application/x-tar")) || mime.inherits(QStringLiteral("application/x-compressed-tar"))
        || mime.inherits(QStringLiteral("application/x-bzip-compressed-tar")) || mime.inherits(QStringLiteral("application/x-xz-compressed-tar"))
        || mime.inherits(QStringLiteral("application/x-zstd-compressed-tar")))
        return std::make_unique<KTar>(path);
    return nullptr;
}
} // namespace

bool Fm::isArchive(const QString &path) const
{
    return QFileInfo(path).isFile() && archiveFor(path) != nullptr;
}

QString Fm::compress(const QStringList &paths)
{
    if (paths.isEmpty())
        return {};
    // Finder: "name.zip" for one item, "Archive.zip" (Archive 2.zip, …) for several.
    const QFileInfo first(paths.first());
    const QString dir = first.absolutePath();
    const QString dest = freeName(dir, paths.size() == 1 ? first.fileName() : QStringLiteral("Archive"), QStringLiteral(".zip"));
    QPointer<Fm> self(this);
    (void)QtConcurrent::run([self, paths, dest] {
        KZip zip(dest);
        bool ok = zip.open(QIODevice::WriteOnly);
        for (const QString &p : paths) {
            const QFileInfo fi(p);
            ok = ok && (fi.isDir() ? zip.addLocalDirectory(p, fi.fileName()) : zip.addLocalFile(p, fi.fileName()));
        }
        ok = zip.close() && ok;
        if (self)
            QMetaObject::invokeMethod(self, [self, dest, ok] { if (self) Q_EMIT self->archiveDone(dest, ok); });
    });
    return dest;
}

QString Fm::expand(const QString &path)
{
    // Into a folder named after the archive, next to it (Finder's Archive Utility does the same).
    const QFileInfo fi(path);
    QString base = fi.fileName();
    for (const char *ext : {".tar.gz", ".tar.bz2", ".tar.xz", ".tar.zst", ".tgz", ".zip", ".7z", ".tar"})
        if (base.endsWith(QLatin1String(ext), Qt::CaseInsensitive)) {
            base.chop(int(qstrlen(ext)));
            break;
        }
    const QString dest = freeName(fi.absolutePath(), base, QString());
    QPointer<Fm> self(this);
    (void)QtConcurrent::run([self, path, dest] {
        std::unique_ptr<KArchive> archive = archiveFor(path);
        bool ok = archive && archive->open(QIODevice::ReadOnly) && QDir().mkpath(dest) && archive->directory()->copyTo(dest, true);
        if (self)
            QMetaObject::invokeMethod(self, [self, dest, ok] { if (self) Q_EMIT self->archiveDone(dest, ok); });
    });
    return dest;
}

bool Fm::canUndo() const
{
    return KIO::FileUndoManager::self()->isUndoAvailable();
}

QString Fm::undoText() const
{
    return KIO::FileUndoManager::self()->undoText();
}

void Fm::undo()
{
    KIO::FileUndoManager::self()->undo();
}

// First free "<base><suffix>", "<base> 2<suffix>", … in dir.
QString Fm::freeName(const QString &dir, const QString &base, const QString &suffix)
{
    QString name = base + suffix;
    for (int i = 2; QFileInfo::exists(dir + QLatin1Char('/') + name) || QFileInfo(dir + QLatin1Char('/') + name).isSymLink(); ++i)
        name = QStringLiteral("%1 %2%3").arg(base).arg(i).arg(suffix);
    return dir + QLatin1Char('/') + name;
}

QStringList Fm::duplicate(const QStringList &paths)
{
    QStringList out;
    for (const QString &p : paths) {
        const QFileInfo fi(p);
        // "photo.jpg" -> "photo copy.jpg"; folders and dotfiles keep their whole name as the base.
        const bool split = !fi.isDir() && !fi.completeSuffix().isEmpty() && !fi.baseName().isEmpty();
        const QString base = (split ? fi.completeBaseName() : fi.fileName()) + QStringLiteral(" copy");
        const QString dest = freeName(fi.absolutePath(), base, split ? QLatin1Char('.') + fi.suffix() : QString());
        undoable(KIO::copyAs(QUrl::fromLocalFile(p), QUrl::fromLocalFile(dest), KIO::HideProgressInfo));
        out << dest;
    }
    return out;
}

QStringList Fm::makeAlias(const QStringList &paths)
{
    QStringList out;
    for (const QString &p : paths) {
        const QFileInfo fi(p);
        const QString dest = freeName(fi.absolutePath(), fi.fileName() + QStringLiteral(" alias"), QString());
        // linkAs is a CopyJob, so the undo manager knows exactly which link to remove.
        undoable(KIO::linkAs(QUrl::fromLocalFile(p), QUrl::fromLocalFile(dest), KIO::HideProgressInfo));
        out << dest;
    }
    return out;
}

QString Fm::showOriginal(const QString &path) const
{
    const QFileInfo fi(path);
    return fi.isSymLink() ? fi.symLinkTarget() : QString();
}

void Fm::pasteMove(const QString &dir)
{
    const QMimeData *mime = QGuiApplication::clipboard()->mimeData();
    if (mime && mime->hasUrls())
        undoable(KIO::move(mime->urls(), QUrl::fromLocalFile(dir)));
}

QVariantMap Fm::info(const QString &path) const
{
    const QFileInfo fi(path);
    const QMimeType mime = QMimeDatabase().mimeTypeForFile(fi);
    QVariantMap m;
    m[QStringLiteral("name")] = fi.fileName().isEmpty() ? QStringLiteral("/") : fi.fileName();
    m[QStringLiteral("path")] = fi.absoluteFilePath();
    m[QStringLiteral("where")] = fi.absolutePath();
    m[QStringLiteral("exists")] = fi.exists() || fi.isSymLink();
    m[QStringLiteral("isDir")] = fi.isDir();
    m[QStringLiteral("isLink")] = fi.isSymLink();
    m[QStringLiteral("target")] = fi.isSymLink() ? fi.symLinkTarget() : QString();
    m[QStringLiteral("mime")] = fi.isDir() ? QStringLiteral("inode/directory") : mime.name();
    m[QStringLiteral("kindText")] = fi.isDir() ? QStringLiteral("Folder") : mime.comment();
    m[QStringLiteral("size")] = fi.isDir() ? -1 : fi.size();
    m[QStringLiteral("sizeText")] = fi.isDir() ? QString() : humanSize(fi.size()) + QStringLiteral(" (%L1 bytes)").arg(fi.size());
    const QLocale loc;
    // Finder style: "Tuesday, 20 October 2026 at 21:52".
    auto date = [&](const QDateTime &d) {
        return d.isValid() ? loc.toString(d, QStringLiteral("dddd, d MMMM yyyy 'at' ") + loc.timeFormat(QLocale::ShortFormat)) : QStringLiteral("--");
    };
    m[QStringLiteral("created")] = date(fi.birthTime());
    m[QStringLiteral("modified")] = date(fi.lastModified());
    m[QStringLiteral("accessed")] = date(fi.lastRead());
    m[QStringLiteral("owner")] = fi.owner();
    m[QStringLiteral("group")] = fi.group();
    auto access = [&](QFile::Permission r, QFile::Permission w) {
        const bool cr = fi.permissions() & r, cw = fi.permissions() & w;
        return cr && cw ? QStringLiteral("Read & Write") : cr ? QStringLiteral("Read only") : cw ? QStringLiteral("Write only")
                                                                                              : QStringLiteral("No access");
    };
    m[QStringLiteral("permOwner")] = access(QFile::ReadOwner, QFile::WriteOwner);
    m[QStringLiteral("permGroup")] = access(QFile::ReadGroup, QFile::WriteGroup);
    m[QStringLiteral("permOther")] = access(QFile::ReadOther, QFile::WriteOther);
    m[QStringLiteral("executable")] = !fi.isDir() && fi.isExecutable();
    const KService::Ptr app = fi.isDir() ? KService::Ptr() : KApplicationTrader::preferredService(mime.name());
    m[QStringLiteral("tags")] = tags(path);
    m[QStringLiteral("openWith")] = app ? app->name() : QString();
    m[QStringLiteral("openWithIcon")] = app ? app->icon() : QString();
    return m;
}

namespace
{
QString sizeCacheFile()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + QStringLiteral("/irixfm/folder-sizes.json");
}
constexpr qint64 kSizeMaxAge = 60 * 60 * 1000;   // recount after an hour even if the folder looks unchanged
} // namespace

void Fm::folderSize(const QString &path)
{
    const QFileInfo fi(path);
    const auto it = m_sizeCache.constFind(path);
    const bool fresh = it != m_sizeCache.constEnd() && it->dirMtime == fi.lastModified().toMSecsSinceEpoch()
        && QDateTime::currentMSecsSinceEpoch() - it->counted < kSizeMaxAge;
    if (fresh) {
        // Answer from the cache, after the caller has finished (a delegate being created).
        const SizeEntry e = *it;
        QMetaObject::invokeMethod(this, [this, path, e] { Q_EMIT folderSizeReady(path, e.bytes, e.files, e.folders); }, Qt::QueuedConnection);
        return;
    }
    // Queued: a list view of a big folder asks for many sizes at once. The view keeps showing the
    // cached (possibly stale) size meanwhile.
    if (!m_sizeQueue.contains(path))
        m_sizeQueue << path;
    startNextSize();
}

QString Fm::cachedFolderSize(const QString &path) const
{
    const auto it = m_sizeCache.constFind(path);
    return it == m_sizeCache.constEnd() ? QString() : humanSize(it->bytes);
}

void Fm::loadSizeCache()
{
    QFile f(sizeCacheFile());
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    for (auto it = o.begin(); it != o.end(); ++it) {
        const QJsonArray a = it.value().toArray();
        if (a.size() == 5)
            m_sizeCache.insert(it.key(), {qint64(a[0].toDouble()), qint64(a[1].toDouble()), qint64(a[2].toDouble()),
                                          qint64(a[3].toDouble()), qint64(a[4].toDouble())});
    }
}

void Fm::saveSizeCacheSoon()
{
    if (m_sizeSavePending)
        return;
    m_sizeSavePending = true;
    QTimer::singleShot(3000, this, [this] {
        m_sizeSavePending = false;
        // Keep it small: forget folders that are gone, and the oldest entries beyond 5000.
        QList<QPair<qint64, QString>> order;
        for (auto it = m_sizeCache.begin(); it != m_sizeCache.end(); ++it)
            order << qMakePair(it->counted, it.key());
        std::sort(order.begin(), order.end(), std::greater<>());
        QJsonObject o;
        for (const auto &[counted, path] : std::as_const(order)) {
            if (o.size() >= 5000)
                break;
            if (!QFileInfo::exists(path))
                continue;
            const SizeEntry &e = m_sizeCache[path];
            o.insert(path, QJsonArray{double(e.bytes), double(e.files), double(e.folders), double(e.dirMtime), double(e.counted)});
        }
        QDir().mkpath(QFileInfo(sizeCacheFile()).absolutePath());
        QFile f(sizeCacheFile());
        if (f.open(QIODevice::WriteOnly))
            f.write(QJsonDocument(o).toJson(QJsonDocument::Compact));
    });
}

void Fm::startNextSize()
{
    constexpr int kMaxSizeJobs = 3;
    while (m_sizeRunning < kMaxSizeJobs && !m_sizeQueue.isEmpty()) {
        const QString path = m_sizeQueue.takeFirst();
        ++m_sizeRunning;
        auto *job = KIO::directorySize(QUrl::fromLocalFile(path));
        const qint64 mtime = QFileInfo(path).lastModified().toMSecsSinceEpoch();   // before counting
        connect(job, &KJob::result, this, [this, job, path, mtime] {
            --m_sizeRunning;
            if (!job->error()) {
                m_sizeCache.insert(path, {qint64(job->totalSize()), qint64(job->totalFiles()), qint64(job->totalSubdirs()), mtime,
                                          QDateTime::currentMSecsSinceEpoch()});
                saveSizeCacheSoon();
            }
            Q_EMIT folderSizeReady(path, job->error() ? -1 : qint64(job->totalSize()), qint64(job->totalFiles()), qint64(job->totalSubdirs()));
            startNextSize();
        });
    }
}

QString Fm::textPreview(const QString &path, int maxBytes) const
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    const QByteArray data = f.read(maxBytes);
    if (data.contains('\0'))
        return {};
    return QString::fromUtf8(data);
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
    // By name first (no disk reads); look inside only when the name says nothing.
    static const QMimeDatabase db;
    QMimeType mime = db.mimeTypeForFile(fi, QMimeDatabase::MatchExtension);
    if (mime.isDefault())
        mime = db.mimeTypeForFile(fi);
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

QVariantMap Fm::resolvePath(const QString &text, const QString &base) const
{
    const QFileInfo fi(expandPath(text, base));
    if (fi.isDir())
        return {{QStringLiteral("ok"), true}, {QStringLiteral("dir"), fi.absoluteFilePath()}};
    if (fi.exists())
        return {{QStringLiteral("ok"), true}, {QStringLiteral("dir"), fi.absolutePath()}, {QStringLiteral("file"), fi.absoluteFilePath()}};
    return {{QStringLiteral("ok"), false}};
}

QString Fm::completePath(const QString &text, const QString &base) const
{
    // Split what was typed into the folder part (kept as typed, e.g. "~/Do") and the name prefix.
    const int slash = text.lastIndexOf(QLatin1Char('/'));
    const QString head = text.left(slash + 1);
    const QString prefix = text.mid(slash + 1);
    const QString dir = head.isEmpty() ? base : expandPath(head, base);

    QDir::Filters filters = QDir::Dirs | QDir::NoDotAndDotDot;
    if (prefix.startsWith(QLatin1Char('.')))
        filters |= QDir::Hidden;
    QStringList matches;
    for (const QString &name : QDir(dir).entryList(filters, QDir::Name | QDir::IgnoreCase))
        if (name.startsWith(prefix, Qt::CaseInsensitive))
            matches << name;
    if (matches.isEmpty())
        return text;
    if (matches.size() == 1)
        return head + matches.first() + QLatin1Char('/');

    // Several matches: extend to their longest common prefix.
    QString common = matches.first();
    for (const QString &m : std::as_const(matches)) {
        int i = 0;
        while (i < common.size() && i < m.size() && common[i].toLower() == m[i].toLower())
            ++i;
        common.truncate(i);
    }
    return common.size() > prefix.size() ? head + common : text;
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
        undoable(KIO::trash(toUrls(paths)));
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
        undoable(KIO::copy(mime->urls(), QUrl::fromLocalFile(dir)));
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
    undoable(move ? KIO::move(todo, dest) : KIO::copy(todo, dest));
}

void Fm::copyOrMove(const QStringList &paths, const QString &dir, bool move)
{
    drop(toUrls(paths), dir, move);
}

bool Fm::copyModifierHeld() const
{
    return QGuiApplication::queryKeyboardModifiers() & Qt::ControlModifier;
}

void Fm::dropInternal(const QList<QUrl> &urls, const QString &dir)
{
    if (urls.isEmpty())
        return;
    const QString destRoot = QStorageInfo(dir).rootPath();
    bool sameDisk = true;
    for (const QUrl &u : urls)
        sameDisk &= QStorageInfo(QFileInfo(u.toLocalFile()).absolutePath()).rootPath() == destRoot;
    drop(urls, dir, sameDisk && !copyModifierHeld());
}

QString Fm::makeFolder(const QString &dir)
{
    QString name = QStringLiteral("New Folder");
    for (int i = 2; QFileInfo::exists(dir + QLatin1Char('/') + name); ++i)
        name = QStringLiteral("New Folder %1").arg(i);
    const QString path = dir + QLatin1Char('/') + name;
    KIO::SimpleJob *job = KIO::mkdir(QUrl::fromLocalFile(path));
    KIO::FileUndoManager::self()->recordJob(KIO::FileUndoManager::Mkdir, {}, QUrl::fromLocalFile(path), job);
    track(job);
    return path;
}

void Fm::rename(const QString &path, const QString &newName)
{
    const QFileInfo fi(path);
    if (newName.isEmpty() || newName.contains(QLatin1Char('/')) || newName == fi.fileName())
        return;
    undoable(KIO::moveAs(QUrl::fromLocalFile(path), QUrl::fromLocalFile(fi.absolutePath() + QLatin1Char('/') + newName),
                         KIO::HideProgressInfo));
}

// ---- Tags -------------------------------------------------------------------------------------

namespace
{
const char kTagAttr[] = "user.xdg.tags";

QStringList readTags(const QString &path)
{
    const QByteArray p = QFile::encodeName(path);
    char buf[4096];
    const ssize_t n = getxattr(p.constData(), kTagAttr, buf, sizeof buf);
    if (n <= 0)
        return {};
    QStringList out;
    for (const QString &t : QString::fromUtf8(buf, int(n)).split(QLatin1Char(','), Qt::SkipEmptyParts))
        out << t.trimmed();
    out.removeAll(QString());
    return out;
}

bool writeTags(const QString &path, const QStringList &tags)
{
    const QByteArray p = QFile::encodeName(path);
    if (tags.isEmpty())
        return removexattr(p.constData(), kTagAttr) == 0 || errno == ENODATA;
    const QByteArray v = tags.join(QLatin1Char(',')).toUtf8();
    return setxattr(p.constData(), kTagAttr, v.constData(), size_t(v.size()), 0) == 0;
}

bool hasTag(const QStringList &tags, const QString &tag)
{
    return tags.contains(tag, Qt::CaseInsensitive);
}

QString tagIndexFile()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/irixfm/tags.json");
}
} // namespace

QStringList Fm::tags(const QString &path, int) const
{
    return readTags(path);
}

void Fm::setTags(const QString &path, const QStringList &list)
{
    QStringList clean;
    for (const QString &t : list)
        if (!t.trimmed().isEmpty() && !t.contains(QLatin1Char(',')) && !hasTag(clean, t.trimmed()))
            clean << t.trimmed();
    if (writeTags(path, clean))
        indexTags(path, clean);
    else
        qWarning("irixfm: can't write tags on %s: %s", qPrintable(path), strerror(errno));
}

void Fm::addTag(const QStringList &paths, const QString &tag)
{
    for (const QString &p : paths) {
        QStringList t = readTags(p);
        if (!hasTag(t, tag))
            setTags(p, t << tag);
    }
    saveTagIndex();
    Q_EMIT tagsChanged();
}

void Fm::removeTag(const QStringList &paths, const QString &tag)
{
    for (const QString &p : paths) {
        QStringList t = readTags(p);
        t.removeIf([&](const QString &x) { return x.compare(tag, Qt::CaseInsensitive) == 0; });
        setTags(p, t);
    }
    saveTagIndex();
    Q_EMIT tagsChanged();
}

void Fm::clearTags(const QStringList &paths)
{
    for (const QString &p : paths)
        setTags(p, {});
    saveTagIndex();
    Q_EMIT tagsChanged();
}

void Fm::indexTags(const QString &path, const QStringList &tags)
{
    if (tags.isEmpty())
        m_tagIndex.remove(path);
    else
        m_tagIndex.insert(path, tags);
}

void Fm::loadTagIndex()
{
    QFile f(tagIndexFile());
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    // Files that are gone (deleted, or temporary) drop out of the index.
    for (auto it = o.begin(); it != o.end(); ++it)
        if (QFileInfo::exists(it.key()))
            m_tagIndex.insert(it.key(), it.value().toVariant().toStringList());
    if (m_tagIndex.size() != o.size())
        saveTagIndex();
}

void Fm::saveTagIndex() const
{
    QJsonObject o;
    for (auto it = m_tagIndex.begin(); it != m_tagIndex.end(); ++it)
        o.insert(it.key(), QJsonValue::fromVariant(it.value()));
    QDir().mkpath(QFileInfo(tagIndexFile()).absolutePath());
    QFile f(tagIndexFile());
    if (f.open(QIODevice::WriteOnly))
        f.write(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

QStringList Fm::taggedCandidates(const QString &tag) const
{
    // fm's own index (finds files Baloo doesn't index: hidden folders, other disks, Baloo off)…
    QStringList out;
    for (auto it = m_tagIndex.begin(); it != m_tagIndex.end(); ++it)
        if (hasTag(it.value(), tag))
            out << it.key();
    // …plus Baloo, which indexes user.xdg.tags, so tags set by Dolphin or before fm are found too.
    QProcess baloo;
    baloo.start(QStringLiteral("baloosearch6"), {QStringLiteral("tag:") + tag});
    if (baloo.waitForFinished(10000))
        for (const QString &line : QString::fromLocal8Bit(baloo.readAllStandardOutput()).split(QLatin1Char('\n')))
            if (line.startsWith(QLatin1Char('/')) && !out.contains(line.trimmed()))
                out << line.trimmed();
    return out;
}

void Fm::startTagSearch(const QString &tag, const QString &here)
{
    const int gen = ++m_searchGen;
    QPointer<Fm> self(this);
    QStringList candidates = taggedCandidates(tag);
    // The folder on screen too: finds tags set by other tools where Baloo doesn't look.
    if (!here.isEmpty())
        for (const QFileInfo &fi : QDir(here).entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden))
            if (!candidates.contains(fi.absoluteFilePath()))
                candidates << fi.absoluteFilePath();
    (void)QtConcurrent::run([self, gen, tag, candidates] {
        QVariantList hits;
        for (const QString &p : candidates) {
            const QFileInfo fi(p);
            if (!(fi.exists() && hasTag(readTags(p), tag)))   // moved, deleted or untagged since
                continue;
            hits << QVariantMap{{QStringLiteral("name"), fi.fileName()}, {QStringLiteral("path"), fi.absoluteFilePath()},
                                {QStringLiteral("where"), fi.absolutePath()}, {QStringLiteral("isDir"), fi.isDir()},
                                {QStringLiteral("modified"), fi.lastModified()}, {QStringLiteral("size"), fi.isDir() ? -1 : fi.size()}};
        }
        if (self)
            QMetaObject::invokeMethod(self, [self, gen, hits] {
                if (!self || self->m_searchGen != gen)
                    return;
                if (!hits.isEmpty())
                    Q_EMIT self->searchBatch(hits);
                Q_EMIT self->searchDone(int(hits.size()), false);
            });
    });
}

void Fm::renameTagEverywhere(const QString &from, const QString &to)
{
    for (const QString &p : taggedCandidates(from)) {
        QStringList t = readTags(p);
        bool changed = false;
        for (QString &x : t)
            if (x.compare(from, Qt::CaseInsensitive) == 0) {
                x = to;
                changed = true;
            }
        if (changed)
            setTags(p, t);
    }
    saveTagIndex();
    Q_EMIT tagsChanged();
}

void Fm::deleteTagEverywhere(const QString &tag)
{
    removeTag(taggedCandidates(tag), tag);
}

// ---- Rich previews: Markdown and syntax highlighting (Nord) ---------------------------------------

namespace
{
// Nord palette for the text around code.
const char kNordText[] = "#D8DEE9";
const char kNordDim[] = "#4C566A";
const char kNordFrost[] = "#88C0D0";
const char kNordCodeBg[] = "#2E3440";

KSyntaxHighlighting::Repository &repository()
{
    static KSyntaxHighlighting::Repository repo;
    return repo;
}

KSyntaxHighlighting::Theme nordTheme()
{
    auto &repo = repository();
    for (const auto &t : repo.themes())
        if (t.name().compare(QLatin1String("Nord"), Qt::CaseInsensitive) == 0)
            return t;
    return repo.defaultTheme(KSyntaxHighlighting::Repository::DarkTheme);
}

// Turns code into HTML spans coloured by the theme, line by line.
class HtmlHighlighter : public KSyntaxHighlighting::AbstractHighlighter
{
public:
    QString html;
    QString line;
    int done = 0;
    void highlight(const QString &code)
    {
        KSyntaxHighlighting::State state;
        const QStringList lines = code.split(QLatin1Char('\n'));
        for (int i = 0; i < lines.size(); ++i) {
            line = lines[i];
            done = 0;
            state = highlightLine(line, state);
            if (done < line.size())
                html += line.mid(done).toHtmlEscaped();
            if (i + 1 < lines.size())
                html += QLatin1Char('\n');
        }
    }

protected:
    void applyFormat(int offset, int length, const KSyntaxHighlighting::Format &format) override
    {
        if (offset > done)
            html += line.mid(done, offset - done).toHtmlEscaped();
        const QString text = line.mid(offset, length).toHtmlEscaped();
        const QColor c = format.textColor(theme());
        if (format.isDefaultTextStyle(theme()) || !c.isValid()) {
            html += text;
        } else {
            QString style = QStringLiteral("color:%1;").arg(c.name());
            if (format.isBold(theme()))
                style += QStringLiteral("font-weight:600;");
            if (format.isItalic(theme()))
                style += QStringLiteral("font-style:italic;");
            html += QStringLiteral("<span style=\"%1\">%2</span>").arg(style, text);
        }
        done = offset + length;
    }
};

QString codeBlock(const QString &code, const KSyntaxHighlighting::Definition &def)
{
    QString body;
    if (def.isValid()) {
        HtmlHighlighter h;
        h.setDefinition(def);
        h.setTheme(nordTheme());
        h.highlight(code);
        body = h.html;
    } else {
        body = code.toHtmlEscaped();
    }
    return QStringLiteral("<table width=\"100%\" cellpadding=\"8\" style=\"background-color:%1;\"><tr><td>"
                          "<pre style=\"color:%2; font-family:'SF Mono','Noto Sans Mono',monospace;\">%3</pre></td></tr></table>")
        .arg(QLatin1String(kNordCodeBg), QLatin1String(kNordText), body);
}

// Markdown → HTML body (Qt's own renderer), without the document wrapper.
QString markdownFragment(const QString &md)
{
    QTextDocument doc;
    doc.setMarkdown(md, QTextDocument::MarkdownDialectGitHub);
    QString html = doc.toHtml();
    const int b = html.indexOf(QLatin1String("<body"));
    const int s = b >= 0 ? html.indexOf(QLatin1Char('>'), b) + 1 : 0;
    const int e = html.lastIndexOf(QLatin1String("</body>"));
    return html.mid(s, e > s ? e - s : -1);
}
} // namespace

QStringList Fm::highlightThemes() const
{
    QStringList names;
    for (const auto &t : repository().themes())
        names << t.name();
    return names;
}

QVariantMap Fm::richPreview(const QString &path, int maxBytes) const
{
    const QString text = textPreview(path, maxBytes);
    if (text.isEmpty())
        return {{QStringLiteral("kind"), QStringLiteral("plain")}, {QStringLiteral("html"), QString()}};
    const QMimeType mime = QMimeDatabase().mimeTypeForFile(path);
    const QString wrapStart = QStringLiteral("<div style=\"color:%1;\">").arg(QLatin1String(kNordText));

    if (mime.inherits(QStringLiteral("text/markdown")) || path.endsWith(QLatin1String(".md"), Qt::CaseInsensitive)) {
        // Fenced code blocks are highlighted by their language; the rest goes through Qt's Markdown.
        static const QRegularExpression fence(QStringLiteral("^(```|~~~)[ \\t]*([\\w+#.-]*)[^\\n]*\\n(.*?)^\\1[ \\t]*$"),
                                              QRegularExpression::MultilineOption | QRegularExpression::DotMatchesEverythingOption);
        QString html = wrapStart;
        int pos = 0;
        auto it = fence.globalMatch(text);
        while (it.hasNext()) {
            const auto m = it.next();
            html += markdownFragment(text.mid(pos, m.capturedStart() - pos));
            const QString lang = m.captured(2);
            auto def = repository().definitionForName(lang);
            if (!def.isValid() && !lang.isEmpty())
                def = repository().definitionForFileName(QStringLiteral("x.") + lang);
            QString code = m.captured(3);
            if (code.endsWith(QLatin1Char('\n')))
                code.chop(1);
            html += codeBlock(code, def);
            pos = m.capturedEnd();
        }
        html += markdownFragment(text.mid(pos)) + QStringLiteral("</div>");
        // Pictures: relative paths are relative to the Markdown file; wide ones are scaled down.
        static const QRegularExpression img(QStringLiteral("<img src=\"([^\"]+)\"([^>]*)>"));
        const QString base = QFileInfo(path).absolutePath();
        QString out;
        int last = 0;
        auto im = img.globalMatch(html);
        while (im.hasNext()) {
            const auto m = im.next();
            QString src = m.captured(1);
            if (!src.contains(QLatin1String("://")) && !src.startsWith(QLatin1String("data:")))
                src = QUrl::fromLocalFile(QDir(base).absoluteFilePath(src)).toString();
            const QSize size = QImageReader(QUrl(src).toLocalFile()).size();
            const QString width = size.width() > 640 ? QStringLiteral(" width=\"640\" height=\"%1\"").arg(size.height() * 640 / size.width()) : QString();
            out += html.mid(last, m.capturedStart() - last) + QStringLiteral("<img src=\"%1\"%2>").arg(src, width);
            last = m.capturedEnd();
        }
        html = out + html.mid(last);
        // Nord accents for links and rules.
        html.replace(QLatin1String("<a href"), QStringLiteral("<a style=\"color:%1;\" href").arg(QLatin1String(kNordFrost)));
        return {{QStringLiteral("kind"), QStringLiteral("markdown")}, {QStringLiteral("html"), html}};
    }

    const auto def = repository().definitionForFileName(path);
    const auto byMime = def.isValid() ? def : repository().definitionForMimeType(mime.name());
    if (byMime.isValid() && byMime.name() != QLatin1String("None"))
        return {{QStringLiteral("kind"), QStringLiteral("code")},
                {QStringLiteral("language"), byMime.name()},
                {QStringLiteral("html"), codeBlock(text, byMime)}};
    return {{QStringLiteral("kind"), QStringLiteral("plain")},
            {QStringLiteral("html"), QStringLiteral("<pre style=\"color:%1;\">%2</pre>").arg(QLatin1String(kNordText), text.toHtmlEscaped())}};
}
