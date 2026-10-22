#include "fm.h"
#include "kiomodel.h"
#include "terminal.h"

#include <QQmlEngine>

#include <QClipboard>
#include <QDir>
#include <QSysInfo>
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
#include <KProtocolInfo>
#include <Solid/Block>
#include <Solid/Camera>
#include <Solid/Device>
#include <Solid/OpticalDisc>
#include <Solid/PortableMediaPlayer>
#include <Solid/DeviceNotifier>
#include <Solid/OpticalDrive>
#include <Solid/StorageAccess>
#include <Solid/StorageDrive>
#include <Solid/StorageVolume>

#include <KDesktopFile>
#include <KProtocolInfo>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <memory>
#include <KApplicationTrader>
#include <QSet>
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
// fm's items are local paths, or URL strings for folders listed through KIO (KioModel).
QUrl toUrl(const QString &p)
{
    return KioModel::isUrl(p) ? QUrl(p) : QUrl::fromLocalFile(p);
}

QList<QUrl> toUrls(const QStringList &paths)
{
    QList<QUrl> urls;
    for (const QString &p : paths)
        urls << toUrl(p);
    return urls;
}

QString kindOfMime(const QMimeType &mime);

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
    connect(Solid::DeviceNotifier::instance(), &Solid::DeviceNotifier::deviceAdded, this, &Fm::scheduleRefresh);
    connect(Solid::DeviceNotifier::instance(), &Solid::DeviceNotifier::deviceRemoved, this, [this](const QString &udi) {
        m_udisksCache.remove(udi);
        scheduleRefresh();
    });
    refreshVolumes();
    loadTagIndex();
    loadSizeCache();
    // The terminal used by terminal tabs (import IrixFm).
    static const bool registered = (qmlRegisterType<TerminalView>("IrixFm", 1, 0, "TerminalView"),
                                    // folders behind KIO URLs (smb://, sftp://, remote:/, mtp:, …) and the URL helpers
                                    qmlRegisterType<KioModel>("IrixFm", 1, 0, "KioModel"),
                                    qmlRegisterSingletonInstance("IrixFm", 1, 0, "Kio", KioUrl::instance()), true);
    Q_UNUSED(registered)
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
    const KFileItem remote = KioModel::isUrl(path) ? KioModel::findItem(path) : KFileItem();
    const QString mime = KioModel::isUrl(path) ? (remote.isNull() ? QMimeDatabase().mimeTypeForFile(path, QMimeDatabase::MatchExtension).name()
                                                                  : remote.mimetype())
                                               : QMimeDatabase().mimeTypeForFile(path).name();
    QVariantList out;
    for (const KService::Ptr &app : KApplicationTrader::queryByMimeType(mime)) {
        out << QVariantMap{{QStringLiteral("id"), app->storageId()}, {QStringLiteral("name"), app->name()}, {QStringLiteral("icon"), app->icon()}};
        if (out.size() >= 12)
            break;
    }
    return out;
}

QVariantMap Fm::fileMeta(const QString &path) const
{
    if (KioModel::isUrl(path)) {   // what KIO listed
        const KFileItem it = KioModel::findItem(path);
        if (it.isNull())
            return {};
        return {{QStringLiteral("created"), it.time(KFileItem::CreationTime)},
                {QStringLiteral("opened"), it.time(KFileItem::AccessTime)},
                {QStringLiteral("permissions"), it.permissionsString().mid(1)},
                {QStringLiteral("owner"), it.user()}};
    }
    const QFileInfo fi(path);
    const QFile::Permissions p = fi.permissions();
    QString perms;
    for (const auto &[bit, ch] : {std::pair{QFile::ReadOwner, 'r'}, {QFile::WriteOwner, 'w'}, {QFile::ExeOwner, 'x'},
                                  {QFile::ReadGroup, 'r'}, {QFile::WriteGroup, 'w'}, {QFile::ExeGroup, 'x'},
                                  {QFile::ReadOther, 'r'}, {QFile::WriteOther, 'w'}, {QFile::ExeOther, 'x'}})
        perms += QLatin1Char(p & bit ? ch : '-');
    return {{QStringLiteral("created"), fi.birthTime()},
            {QStringLiteral("opened"), fi.lastRead()},
            {QStringLiteral("changed"), fi.metadataChangeTime()},
            {QStringLiteral("permissions"), perms},
            {QStringLiteral("owner"), fi.owner()}};
}

QVariantList Fm::applications() const
{
    QList<KService::Ptr> apps = KApplicationTrader::query([](const KService::Ptr &s) { return !s->noDisplay() && !s->icon().isEmpty(); });
    std::sort(apps.begin(), apps.end(), [](const KService::Ptr &a, const KService::Ptr &b) { return a->name().localeAwareCompare(b->name()) < 0; });
    QVariantList out;
    QSet<QString> seen;
    for (const KService::Ptr &app : apps) {
        if (seen.contains(app->icon()))
            continue;
        seen.insert(app->icon());
        out << QVariantMap{{QStringLiteral("name"), app->name()}, {QStringLiteral("icon"), app->icon()}};
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

// ---- Devices (Locations) -------------------------------------------------------------------
// Solid lists them, UDisks2 (system D-Bus) fills in what Solid doesn't say (partition type,
// HintIgnore, LUKS state) and powers drives off. Everything goes through raw records (QVariantMap),
// so deviceRows() — the filter — and the actions can be tested with fakes (setFakeDevices).

namespace
{
const QString kUDisks = QStringLiteral("org.freedesktop.UDisks2");

// org.freedesktop.DBus.Properties.GetAll on a UDisks2 object ({} when it hasn't that interface).
QVariantMap udisksProps(const QString &path, const QString &iface)
{
    QDBusMessage m = QDBusMessage::createMethodCall(kUDisks, path, QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("GetAll"));
    m << QStringLiteral("org.freedesktop.UDisks2.") + iface;
    const QDBusMessage r = QDBusConnection::systemBus().call(m, QDBus::Block, 1000);
    if (r.type() != QDBusMessage::ReplyMessage || r.arguments().isEmpty())
        return {};
    return qdbus_cast<QVariantMap>(r.arguments().constFirst());
}

QString objectPath(const QVariant &v)
{
    return v.canConvert<QDBusObjectPath>() ? v.value<QDBusObjectPath>().path() : v.toString();
}

QString sizeText(qint64 bytes)
{
    // Decimal units like Finder ("32 GB" for a 32 GB stick).
    return bytes > 0 ? QLocale().formattedDataSize(bytes, bytes >= 1000LL * 1000 * 1000 * 10 ? 0 : 1, QLocale::DataSizeSIFormat) : QString();
}

bool truthy(const QVariantMap &d, const char *key)
{
    return d.value(QLatin1String(key)).toBool();
}
} // namespace

QVariantList Fm::volumes() const
{
    return m_volumes;
}

bool Fm::isSystemMount(const QString &path)
{
    if (path.isEmpty())
        return false;
    static const QStringList exact = {QStringLiteral("/"),    QStringLiteral("/home"), QStringLiteral("/boot"), QStringLiteral("/efi"),
                                      QStringLiteral("/usr"), QStringLiteral("/var"),  QStringLiteral("/opt"),  QStringLiteral("/srv"),
                                      QStringLiteral("/root"), QStringLiteral("/tmp"), QStringLiteral("/nix"),  QStringLiteral("/gnu"),
                                      QStringLiteral("/snap"), QStringLiteral("/recovery"), QStringLiteral("[SWAP]")};
    static const QStringList under = {QStringLiteral("/boot/"), QStringLiteral("/efi/"), QStringLiteral("/usr/"), QStringLiteral("/var/"),
                                      QStringLiteral("/snap/"), QStringLiteral("/proc/"), QStringLiteral("/sys/"), QStringLiteral("/dev/"),
                                      QStringLiteral("/nix/"), QStringLiteral("/opt/")};
    const QString p = path.size() > 1 && path.endsWith(QLatin1Char('/')) ? path.chopped(1) : path;
    if (exact.contains(p))
        return true;
    for (const QString &u : under)
        if (p.startsWith(u))
            return true;
    // /run is the system's, except udisks' /run/media/<user>/…
    return p.startsWith(QLatin1String("/run/")) && !p.startsWith(QLatin1String("/run/media/"));
}

QVariantList Fm::deviceRows(const QVariantList &raw)
{
    // Partition types that are never a place to open: EFI system, BIOS boot, Microsoft reserved,
    // Windows recovery, Linux swap / LVM / RAID members (GPT GUIDs, and the MBR codes).
    static const QSet<QString> hiddenTypes = {
        QStringLiteral("c12a7328-f81f-11d2-ba4b-00a0c93ec93b"), QStringLiteral("21686148-6449-6e6f-744e-656564454649"),
        QStringLiteral("e3c9e316-0b5c-4db8-817d-f92df00215ae"), QStringLiteral("de94bba4-06d1-4d40-a16a-bfd50179d6ac"),
        QStringLiteral("0657fd6d-a4ab-43c4-84e5-0933c84b4f4f"), QStringLiteral("e6d6d379-f507-44c2-a23c-238f2a3df928"),
        QStringLiteral("a19d880f-05fc-4d3b-a006-743f0f84911e"), QStringLiteral("0xef"), QStringLiteral("0x82"), QStringLiteral("0x27"),
        QStringLiteral("0x8e"), QStringLiteral("0xfd")};
    QVariantList outside, inside;
    QSet<QString> seen;
    for (const QVariant &v : raw) {
        const QVariantMap d = v.toMap();
        const QString udi = d.value(QStringLiteral("udi")).toString();
        if (udi.isEmpty() || seen.contains(udi))
            continue;
        const QString name = d.value(QStringLiteral("label")).toString();
        // Phones, media players and cameras: browsed through a KIO worker URL, no mounting.
        if (truthy(d, "player") || truthy(d, "camera")) {
            const QStringList protocols = d.value(QStringLiteral("protocols")).toStringList();
            QString url;
            if (truthy(d, "player") && !protocols.isEmpty()) {
                // As KFilePlacesItem (Dolphin) does: mtp:udi=<udi>, otherwise <protocol>://<udi's last part>/ (afc).
                const QString proto = protocols.constFirst();
                url = proto == QLatin1String("mtp") ? QStringLiteral("mtp:udi=") + udi
                                                    : proto + QStringLiteral("://") + udi.section(QLatin1Char('/'), -1) + QLatin1Char('/');
            } else if (truthy(d, "camera") && protocols.contains(QLatin1String("ptp"))) {
                url = QStringLiteral("camera:/");   // kio-extras' kamera (gphoto2)
            }
            if (url.isEmpty())
                continue;
            seen.insert(udi);
            const QString icon = truthy(d, "camera") ? QStringLiteral("camera-photo-symbolic")
                                 : (d.value(QStringLiteral("icon")).toString().contains(QLatin1String("phone")) || protocols.contains(QLatin1String("mtp"))
                                    || protocols.contains(QLatin1String("afc")))
                                     ? QStringLiteral("phone-symbolic")
                                     : QStringLiteral("multimedia-player-symbolic");
            const QString label = name.isEmpty() ? QStringLiteral("Phone") : name;
            const QString kindText = truthy(d, "camera") ? QStringLiteral("Camera") : icon == QLatin1String("phone-symbolic") ? QStringLiteral("Phone") : QStringLiteral("Media player");
            outside << QVariantMap{{QStringLiteral("udi"), udi},          {QStringLiteral("drive"), udi},
                                   {QStringLiteral("kind"), QStringLiteral("phone")}, {QStringLiteral("label"), label},
                                   {QStringLiteral("icon"), icon},        {QStringLiteral("mounted"), false},
                                   {QStringLiteral("path"), QString()},   {QStringLiteral("url"), url},
                                   {QStringLiteral("removable"), true},   {QStringLiteral("optical"), false},
                                   {QStringLiteral("encrypted"), false},  {QStringLiteral("internal"), false},
                                   {QStringLiteral("size"), 0},           {QStringLiteral("sizeText"), QString()},
                                   {QStringLiteral("fsType"), QString()}, {QStringLiteral("device"), QString()},
                                   {QStringLiteral("kindText"), kindText},
                                   {QStringLiteral("tip"), label + QStringLiteral(" — ") + kindText + QStringLiteral(" (") + url.section(QLatin1Char(':'), 0, 0) + QLatin1Char(')')}};
            continue;
        }
        // Storage: what the system uses, hides or can't open stays out.
        const QString device = d.value(QStringLiteral("device")).toString();
        const QString fsType = d.value(QStringLiteral("fsType")).toString();
        const QString usage = d.value(QStringLiteral("usage")).toString();
        const QString path = d.value(QStringLiteral("path")).toString();
        const bool mounted = truthy(d, "mounted");
        const bool optical = truthy(d, "optical");
        const bool encrypted = truthy(d, "encrypted");
        const bool removable = truthy(d, "removable") || optical;
        if (truthy(d, "ignored") || truthy(d, "cleartext") || truthy(d, "loop"))
            continue;
        if (device.startsWith(QLatin1String("/dev/loop")) || device.startsWith(QLatin1String("/dev/zram")) || device.startsWith(QLatin1String("/dev/ram")))
            continue;
        if (fsType == QLatin1String("swap") || (usage != QLatin1String("filesystem") && !(encrypted && usage == QLatin1String("encrypted"))))
            continue;
        if (hiddenTypes.contains(d.value(QStringLiteral("partType")).toString().toLower()))
            continue;
        if (mounted && isSystemMount(path))
            continue;
        // An internal LUKS container that's unlocked but has no mounted file system holds the system
        // (LVM, swap, root mounted elsewhere): not a place to open.
        if (encrypted && !removable && truthy(d, "unlocked") && !mounted)
            continue;
        seen.insert(udi);
        const qint64 size = d.value(QStringLiteral("size")).toLongLong();
        const QString sz = sizeText(size);
        const QString label = !name.isEmpty() ? name : sz.isEmpty() ? QStringLiteral("Untitled") : sz + QStringLiteral(" Volume");
        const QString driveType = d.value(QStringLiteral("driveType")).toString();
        const bool flash = driveType == QLatin1String("flash");
        const QString icon = optical ? QStringLiteral("media-optical-symbolic")
                             : flash ? QStringLiteral("media-flash-symbolic")
                             : removable ? QStringLiteral("drive-removable-media-symbolic")
                                         : QStringLiteral("drive-harddisk-symbolic");
        const QString kindText = optical ? QStringLiteral("Disc") : flash ? QStringLiteral("Memory card")
                                 : removable ? QStringLiteral("External drive") : QStringLiteral("Internal disk");
        QStringList facts;
        if (!sz.isEmpty())
            facts << sz;
        facts << kindText.toLower();
        if (!fsType.isEmpty() && !(encrypted && fsType.startsWith(QLatin1String("crypto"))))
            facts << fsType;
        if (encrypted)
            facts << QStringLiteral("encrypted");
        if (!mounted)
            facts << QStringLiteral("not mounted: click to open");
        const QVariantMap row{{QStringLiteral("udi"), udi},
                              {QStringLiteral("drive"), d.value(QStringLiteral("drive")).toString()},
                              {QStringLiteral("kind"), QStringLiteral("volume")},
                              {QStringLiteral("label"), label},
                              {QStringLiteral("icon"), icon},
                              {QStringLiteral("mounted"), mounted},
                              {QStringLiteral("path"), mounted ? path : QString()},
                              {QStringLiteral("url"), QString()},
                              {QStringLiteral("removable"), removable},
                              {QStringLiteral("optical"), optical},
                              {QStringLiteral("encrypted"), encrypted},
                              {QStringLiteral("internal"), !removable},
                              {QStringLiteral("size"), size},
                              {QStringLiteral("sizeText"), sz},
                              {QStringLiteral("fsType"), fsType},
                              {QStringLiteral("device"), device},
                              {QStringLiteral("kindText"), kindText},
                              {QStringLiteral("tip"), label + QStringLiteral(" — ") + facts.join(QStringLiteral(", "))}};
        (removable ? outside : inside) << row;
    }
    // Plugged-in things first (in the order they came), then the internal partitions.
    return outside + inside;
}

QVariantList Fm::collectDevices()
{
    if (m_fakeDevices)
        return m_rawDevices;
    QVariantList list;
    for (const Solid::Device &dev : Solid::Device::listFromType(Solid::DeviceInterface::StorageAccess)) {
        const QString udi = dev.udi();
        // fstab-only entries (/org/kde/fstab/…) are network shares and the like: not devices.
        if (!udi.startsWith(QLatin1String("/org/freedesktop/UDisks2/")))
            continue;
        auto *access = dev.as<Solid::StorageAccess>();
        if (!access)
            continue;
        const auto *vol = dev.as<Solid::StorageVolume>();
        const auto *block = dev.as<Solid::Block>();
        Solid::Device drive = dev;
        while (drive.isValid() && !drive.is<Solid::StorageDrive>())
            drive = drive.parent();
        const auto *sd = drive.isValid() ? drive.as<Solid::StorageDrive>() : nullptr;
        // UDisks facts that don't change while the device is there; the LUKS state does.
        QVariantMap facts = m_udisksCache.value(udi);
        if (facts.isEmpty()) {
            const QVariantMap b = udisksProps(udi, QStringLiteral("Block"));
            const QVariantMap p = udisksProps(udi, QStringLiteral("Partition"));
            facts = {{QStringLiteral("hintIgnore"), b.value(QStringLiteral("HintIgnore")).toBool()},
                     {QStringLiteral("cryptoBacking"), objectPath(b.value(QStringLiteral("CryptoBackingDevice")))},
                     {QStringLiteral("partType"), p.value(QStringLiteral("Type")).toString()},
                     {QStringLiteral("ok"), true}};
            m_udisksCache.insert(udi, facts);
        }
        const QString backing = facts.value(QStringLiteral("cryptoBacking")).toString();
        const bool encrypted = access->isEncrypted() || (vol && vol->usage() == Solid::StorageVolume::Encrypted);
        bool unlocked = false;
        if (encrypted) {
            const QString clear = objectPath(udisksProps(udi, QStringLiteral("Encrypted")).value(QStringLiteral("CleartextDevice")));
            unlocked = !clear.isEmpty() && clear != QLatin1String("/");
        }
        QString usage;
        switch (vol ? vol->usage() : Solid::StorageVolume::Other) {
        case Solid::StorageVolume::FileSystem: usage = QStringLiteral("filesystem"); break;
        case Solid::StorageVolume::Encrypted: usage = QStringLiteral("encrypted"); break;
        default: usage = QStringLiteral("other"); break;
        }
        QString driveType;
        if (sd) {
            switch (sd->driveType()) {
            case Solid::StorageDrive::CdromDrive: driveType = QStringLiteral("optical"); break;
            case Solid::StorageDrive::SdMmc:
            case Solid::StorageDrive::CompactFlash:
            case Solid::StorageDrive::MemoryStick:
            case Solid::StorageDrive::SmartMedia:
            case Solid::StorageDrive::Xd: driveType = QStringLiteral("flash"); break;
            default: driveType = QStringLiteral("disk"); break;
            }
        }
        const bool optical = dev.is<Solid::OpticalDisc>() || driveType == QLatin1String("optical");
        list << QVariantMap{{QStringLiteral("udi"), udi},
                            {QStringLiteral("drive"), drive.isValid() ? drive.udi() : QString()},
                            {QStringLiteral("label"), vol && !vol->label().isEmpty() ? vol->label() : QString()},
                            {QStringLiteral("product"), drive.isValid() ? (drive.vendor() + QLatin1Char(' ') + drive.product()).trimmed() : QString()},
                            {QStringLiteral("icon"), dev.icon()},
                            {QStringLiteral("device"), block ? block->device() : QString()},
                            {QStringLiteral("fsType"), vol ? vol->fsType() : QString()},
                            {QStringLiteral("usage"), usage},
                            {QStringLiteral("size"), vol ? qint64(vol->size()) : 0},
                            {QStringLiteral("mounted"), access->isAccessible()},
                            {QStringLiteral("path"), access->filePath()},
                            {QStringLiteral("encrypted"), encrypted},
                            {QStringLiteral("unlocked"), unlocked},
                            {QStringLiteral("cleartext"), !backing.isEmpty() && backing != QLatin1String("/")},
                            {QStringLiteral("ignored"), facts.value(QStringLiteral("hintIgnore")).toBool() || (vol && vol->isIgnored())},
                            {QStringLiteral("partType"), facts.value(QStringLiteral("partType"))},
                            {QStringLiteral("removable"), sd && (sd->isHotpluggable() || sd->isRemovable())},
                            {QStringLiteral("optical"), optical},
                            {QStringLiteral("driveType"), driveType}};
        // Keep the list current when it gets mounted or unmounted elsewhere.
        connect(access, &Solid::StorageAccess::accessibilityChanged, this, &Fm::scheduleRefresh, Qt::UniqueConnection);
    }
    for (const Solid::Device &dev : Solid::Device::listFromType(Solid::DeviceInterface::PortableMediaPlayer)) {
        const auto *player = dev.as<Solid::PortableMediaPlayer>();
        if (!player)
            continue;
        list << QVariantMap{{QStringLiteral("udi"), dev.udi()},
                            {QStringLiteral("player"), true},
                            {QStringLiteral("protocols"), player->supportedProtocols()},
                            {QStringLiteral("label"), !dev.product().isEmpty() ? dev.product() : dev.description()},
                            {QStringLiteral("icon"), dev.icon()}};
    }
    for (const Solid::Device &dev : Solid::Device::listFromType(Solid::DeviceInterface::Camera)) {
        const auto *camera = dev.as<Solid::Camera>();
        if (!camera)
            continue;
        list << QVariantMap{{QStringLiteral("udi"), dev.udi()},
                            {QStringLiteral("camera"), true},
                            {QStringLiteral("protocols"), camera->supportedProtocols()},
                            {QStringLiteral("label"), !dev.product().isEmpty() ? dev.product() : dev.description()},
                            {QStringLiteral("icon"), dev.icon()}};
    }
    return list;
}

void Fm::scheduleRefresh()
{
    // Plugging in a stick sends a burst of deviceAdded (drive, table, partitions): one refresh.
    if (!m_refreshTimer) {
        m_refreshTimer = new QTimer(this);
        m_refreshTimer->setSingleShot(true);
        m_refreshTimer->setInterval(150);
        connect(m_refreshTimer, &QTimer::timeout, this, &Fm::refreshVolumes);
    }
    m_refreshTimer->start();
}

void Fm::refreshVolumes()
{
    const QVariantList raw = collectDevices();
    if (!m_fakeDevices)
        m_rawDevices = raw;
    const QVariantList list = deviceRows(raw);
    if (list != m_volumes) {
        m_volumes = list;
        Q_EMIT volumesChanged();
    }
}

void Fm::setFakeDevices(const QVariantList &raw)
{
    m_fakeDevices = true;
    m_rawDevices = raw;
    m_deviceLog.clear();
    refreshVolumes();
}

QVariantMap Fm::rawDevice(const QString &udi) const
{
    for (const QVariant &v : m_rawDevices)
        if (v.toMap().value(QStringLiteral("udi")).toString() == udi)
            return v.toMap();
    return {};
}

static QString rowLabel(const QVariantList &rows, const QString &udi)
{
    for (const QVariant &v : rows)
        if (v.toMap().value(QStringLiteral("udi")).toString() == udi)
            return v.toMap().value(QStringLiteral("label")).toString();
    return QStringLiteral("the device");
}

bool Fm::canBrowseUrl(const QString &url) const
{
    const QString scheme = QUrl(url).scheme();
    return !scheme.isEmpty() && KProtocolInfo::isKnownProtocol(scheme);
}

void Fm::mountVolume(const QString &udi)
{
    const QString label = rowLabel(m_volumes, udi);
    if (m_fakeDevices) {
        QTimer::singleShot(0, this, [this, udi] {
            for (QVariant &v : m_rawDevices) {
                QVariantMap d = v.toMap();
                if (d.value(QStringLiteral("udi")).toString() != udi)
                    continue;
                if (d.contains(QStringLiteral("failMount"))) {
                    Q_EMIT volumeError(d.value(QStringLiteral("failMount")).toString());
                    return;
                }
                m_deviceLog << QStringLiteral("mount ") + udi;
                const QString path = d.value(QStringLiteral("mountAt"), QStringLiteral("/run/media/") + userName() + QLatin1Char('/') + d.value(QStringLiteral("label")).toString()).toString();
                d[QStringLiteral("mounted")] = true;
                d[QStringLiteral("path")] = path;
                if (truthy(d, "encrypted"))
                    d[QStringLiteral("unlocked")] = true;
                v = d;
                refreshVolumes();
                Q_EMIT volumeMounted(udi, path);
                return;
            }
        });
        return;
    }
    Solid::Device dev(udi);
    auto *access = dev.as<Solid::StorageAccess>();
    if (!access)
        return;
    if (access->isAccessible()) {
        Q_EMIT volumeMounted(udi, access->filePath());
        return;
    }
    // A LUKS volume: Solid's setup() asks for the passphrase (KDE's dialog via kded's
    // soliduiserver), unlocks it with UDisks and mounts the unlocked file system.
    connect(access, &Solid::StorageAccess::setupDone, this, [this, udi, label](Solid::ErrorType error, const QVariant &details) {
        Solid::Device d(udi);
        if (error == Solid::NoError && d.as<Solid::StorageAccess>())
            Q_EMIT volumeMounted(udi, d.as<Solid::StorageAccess>()->filePath());
        else if (error != Solid::UserCanceled)
            Q_EMIT volumeError(QStringLiteral("Couldn't open “%1”").arg(label) + (details.toString().isEmpty() ? QString() : QStringLiteral(": ") + details.toString()));
        scheduleRefresh();
    }, Qt::SingleShotConnection);
    access->setup();
}

QStringList Fm::blockers(const QString &mountPath)
{
    QStringList out;
    if (mountPath.isEmpty() || mountPath == QLatin1String("/"))
        return out;
    auto inside = [&](const QString &p) { return p == mountPath || p.startsWith(mountPath + QLatin1Char('/')); };
    const QStringList pids = QDir(QStringLiteral("/proc")).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &pid : pids) {
        if (pid.isEmpty() || !pid.at(0).isDigit())
            continue;
        const QString base = QStringLiteral("/proc/") + pid;
        QString where;
        const QString cwd = QFileInfo(base + QStringLiteral("/cwd")).symLinkTarget();
        if (inside(cwd))
            where = cwd;
        else {
            QDirIterator fds(base + QStringLiteral("/fd"), QDir::System | QDir::Files | QDir::NoDotAndDotDot);
            while (fds.hasNext()) {
                const QString t = QFileInfo(fds.next()).symLinkTarget();
                if (inside(t)) {
                    where = t;
                    break;
                }
            }
        }
        if (where.isEmpty())
            continue;
        QFile comm(base + QStringLiteral("/comm"));
        QString name = comm.open(QIODevice::ReadOnly) ? QString::fromLocal8Bit(comm.readAll()).trimmed() : pid;
        if (name == QLatin1String("irixfm"))
            name = QStringLiteral("fm");
        const QString rel = where.mid(mountPath.size());
        const QString entry = rel.isEmpty() ? name : name + QStringLiteral(" (") + rel.mid(1) + QLatin1Char(')');
        if (!out.contains(entry))
            out << entry;
    }
    return out;
}

QString Fm::busyMessage(const QString &label, const QString &path, const QString &details) const
{
    const QStringList who = blockers(path);
    QString msg = QStringLiteral("“%1” is in use").arg(label);
    if (!who.isEmpty())
        msg += QStringLiteral(" by ") + who.mid(0, 4).join(QStringLiteral(", ")) + (who.size() > 4 ? QStringLiteral(", …") : QString());
    msg += QStringLiteral(". Close what's open on it, then try again.");
    Q_UNUSED(details)
    return msg;
}

void Fm::teardownOne(const QString &udi, std::function<void(int, const QString &)> done)
{
    if (m_fakeDevices) {
        QTimer::singleShot(0, this, [this, udi, done] {
            for (QVariant &v : m_rawDevices) {
                QVariantMap d = v.toMap();
                if (d.value(QStringLiteral("udi")).toString() != udi)
                    continue;
                if (truthy(d, "busy")) {
                    m_deviceLog << QStringLiteral("busy ") + udi;
                    done(Solid::DeviceBusy, QStringLiteral("target is busy"));
                    return;
                }
                m_deviceLog << QStringLiteral("unmount ") + udi;
                d[QStringLiteral("mounted")] = false;
                d[QStringLiteral("path")] = QString();
                d[QStringLiteral("unlocked")] = false;
                v = d;
            }
            done(Solid::NoError, {});
        });
        return;
    }
    Solid::Device dev(udi);
    auto *access = dev.as<Solid::StorageAccess>();
    if (!access) {
        done(Solid::NoError, {});   // gone already
        return;
    }
    // For a LUKS volume Solid unmounts the unlocked file system, then locks it.
    auto conn = std::make_shared<QMetaObject::Connection>();
    *conn = connect(access, &Solid::StorageAccess::teardownDone, this, [conn, done](Solid::ErrorType error, const QVariant &details, const QString &) {
        QObject::disconnect(*conn);
        done(error, details.toString());
    });
    if (!access->teardown()) {
        QObject::disconnect(*conn);
        done(Solid::OperationFailed, QString());
    }
}

void Fm::teardownAll(QStringList udis, const QString &label, std::function<void(const QString &)> done, bool retried)
{
    if (udis.isEmpty()) {
        done({});
        return;
    }
    const QString udi = udis.takeFirst();
    const QString path = rawDevice(udi).value(QStringLiteral("path")).toString();
    teardownOne(udi, [this, udi, udis, label, done, retried, path](int error, const QString &details) {
        if (error == Solid::NoError) {
            teardownAll(udis, label, done);
            return;
        }
        if (error == Solid::UserCanceled) {
            done(QStringLiteral("cancel"));
            return;
        }
        const bool busy = error == Solid::DeviceBusy || details.contains(QLatin1String("busy"), Qt::CaseInsensitive);
        if (busy && !retried) {
            // fm just moved its views and terminals off it: a shell needs a moment to leave.
            QTimer::singleShot(m_fakeDevices ? 50 : 900, this, [this, udi, udis, label, done] { teardownAll(QStringList{udi} + udis, label, done, true); });
            return;
        }
        done(busy ? busyMessage(label, path, details)
                  : QStringLiteral("Couldn't unmount “%1”").arg(label) + (details.isEmpty() ? QString() : QStringLiteral(": ") + details));
    });
}

void Fm::powerOff(const QString &drive, std::function<void()> done)
{
    if (m_fakeDevices) {
        QTimer::singleShot(0, this, [this, drive, done] {
            m_deviceLog << QStringLiteral("poweroff ") + drive;
            // A powered-off drive disappears.
            QVariantList keep;
            for (const QVariant &v : std::as_const(m_rawDevices))
                if (v.toMap().value(QStringLiteral("drive")).toString() != drive)
                    keep << v;
            m_rawDevices = keep;
            done();
        });
        return;
    }
    if (!drive.startsWith(QLatin1String("/org/freedesktop/UDisks2/drives/"))
        || !udisksProps(drive, QStringLiteral("Drive")).value(QStringLiteral("CanPowerOff")).toBool()) {
        done();   // unmounted is safe to unplug too
        return;
    }
    QDBusMessage m = QDBusMessage::createMethodCall(kUDisks, drive, QStringLiteral("org.freedesktop.UDisks2.Drive"), QStringLiteral("PowerOff"));
    m << QVariantMap();
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(m, 30000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [watcher, done] {
        if (watcher->isError())
            qWarning("irixfm: power off failed: %s", qPrintable(watcher->error().message()));
        watcher->deleteLater();
        done();
    });
}

void Fm::unmountVolume(const QString &udi)
{
    const QString label = rowLabel(m_volumes, udi);
    teardownAll({udi}, label, [this, udi, label](const QString &error) {
        if (error.isEmpty())
            Q_EMIT volumeUnmounted(udi, label);
        else if (error != QLatin1String("cancel"))
            Q_EMIT volumeError(error);
        refreshVolumes();
    });
}

void Fm::ejectVolume(const QString &udi)
{
    const QVariantMap d = rawDevice(udi);
    if (d.isEmpty())
        return;
    const QString label = rowLabel(m_volumes, udi);
    const QString drive = d.value(QStringLiteral("drive")).toString();
    const bool optical = truthy(d, "optical");
    const bool internal = !truthy(d, "removable") && !optical;
    // Finder's eject on a plugged-in drive: every mounted (or unlocked) partition of it.
    QStringList targets;
    if (internal || drive.isEmpty())
        targets << udi;
    else
        for (const QVariant &v : std::as_const(m_rawDevices)) {
            const QVariantMap p = v.toMap();
            if (p.value(QStringLiteral("drive")).toString() == drive && !truthy(p, "cleartext") && !truthy(p, "player")
                && (truthy(p, "mounted") || (truthy(p, "encrypted") && truthy(p, "unlocked"))))
                targets << p.value(QStringLiteral("udi")).toString();
        }
    teardownAll(targets, label, [this, udi, label, drive, optical, internal](const QString &error) {
        if (!error.isEmpty()) {
            if (error != QLatin1String("cancel"))
                Q_EMIT volumeError(error);
            refreshVolumes();
            return;
        }
        if (internal) {
            Q_EMIT volumeEjected(udi, label, QStringLiteral("unmount"));
        } else if (optical) {
            if (m_fakeDevices)
                m_deviceLog << QStringLiteral("eject ") + drive;
            else
                for (Solid::Device p(udi); p.isValid(); p = p.parent())
                    if (auto *od = p.as<Solid::OpticalDrive>()) {
                        od->eject();
                        break;
                    }
            Q_EMIT volumeEjected(udi, label, QStringLiteral("disc"));
        } else {
            powerOff(drive, [this, udi, label] {
                Q_EMIT volumeEjected(udi, label, QStringLiteral("remove"));
                refreshVolumes();
            });
            return;
        }
        refreshVolumes();
    });
}

QVariantMap Fm::volumeInfo(const QString &udi) const
{
    QVariantMap row;
    for (const QVariant &v : m_volumes)
        if (v.toMap().value(QStringLiteral("udi")).toString() == udi)
            row = v.toMap();
    if (row.isEmpty())
        return {};
    const QVariantMap raw = rawDevice(udi);
    const QString path = row.value(QStringLiteral("path")).toString();
    QString free;
    if (row.value(QStringLiteral("mounted")).toBool() && !m_fakeDevices) {
        const QStorageInfo si(path);
        if (si.isValid())
            free = sizeText(si.bytesAvailable()) + QStringLiteral(" available of ") + sizeText(si.bytesTotal());
    }
    QString fs = row.value(QStringLiteral("fsType")).toString();
    if (row.value(QStringLiteral("encrypted")).toBool())
        fs = QStringLiteral("Encrypted (LUKS)");
    return {{QStringLiteral("name"), row.value(QStringLiteral("label"))},
            {QStringLiteral("kind"), row.value(QStringLiteral("kindText"))},
            {QStringLiteral("size"), row.value(QStringLiteral("sizeText"))},
            {QStringLiteral("free"), free},
            {QStringLiteral("fsType"), fs},
            {QStringLiteral("device"), row.value(QStringLiteral("device"))},
            {QStringLiteral("mountPoint"), row.value(QStringLiteral("mounted")).toBool() ? path : QStringLiteral("Not mounted")},
            {QStringLiteral("drive"), raw.value(QStringLiteral("product"))},
            {QStringLiteral("url"), row.value(QStringLiteral("url"))},
            {QStringLiteral("icon"), row.value(QStringLiteral("icon"))}};
}

// ---- Search through subfolders -------------------------------------------------------------

void Fm::startSearch(const QString &root, const QString &text, bool hidden, bool recursive)
{
    const int gen = ++m_searchGen;
    if (text.trimmed().isEmpty())
        return;
    if (KioModel::isUrl(root)) {   // no search in KIO folders: answer "nothing" (the window waits for searchDone)
        QMetaObject::invokeMethod(this, [this, gen] { if (m_searchGen == gen) Q_EMIT searchDone(0, false); }, Qt::QueuedConnection);
        return;
    }
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
    return !KioModel::isUrl(path) && QFileInfo(path).isFile() && archiveFor(path) != nullptr;
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
        if (KioModel::isUrl(p))
            continue;   // local only (names are checked on disk)
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
        if (KioModel::isUrl(p))
            continue;   // symlinks are local
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
    if (KioModel::isUrl(path))
        return {};
    const QFileInfo fi(path);
    return fi.isSymLink() ? fi.symLinkTarget() : QString();
}

void Fm::pasteMove(const QString &dir)
{
    const QMimeData *mime = QGuiApplication::clipboard()->mimeData();
    if (mime && mime->hasUrls())
        undoable(KIO::move(mime->urls(), toUrl(dir)));
}

namespace
{
// Get Info / Quick Look / the preview pane for an item listed through KIO (no I/O: what the
// listing said). `remote` tells the views not to load the file itself.
QVariantMap remoteInfo(const QString &path, const Fm &fm)
{
    const KFileItem it = KioModel::findItem(path);
    const QLocale loc;
    auto date = [&](const QDateTime &d) {
        return d.isValid() ? loc.toString(d, QStringLiteral("dddd, d MMMM yyyy 'at' ") + loc.timeFormat(QLocale::ShortFormat)) : QString();
    };
    const bool known = !it.isNull();
    // the folder being shown (no item for it from some workers) counts as a folder
    const bool isDir = known ? it.isDir() : true;
    const QString mime = known ? it.mimetype() : QStringLiteral("inode/directory");
    const qint64 size = known && !isDir && it.entry().contains(KIO::UDSEntry::UDS_SIZE) ? qint64(it.size()) : -1;
    const KService::Ptr app = isDir ? KService::Ptr() : KApplicationTrader::preferredService(mime);
    auto access = [&](mode_t r, mode_t w) {
        const mode_t p = mode_t(it.permissions());
        const bool cr = p & r, cw = p & w;
        return !known ? QString() : cr && cw ? QStringLiteral("Read & Write") : cr ? QStringLiteral("Read only")
                                   : cw ? QStringLiteral("Write only") : QStringLiteral("No access");
    };
    const QString parent = KioModel::parentUrl(path);
    return {{QStringLiteral("name"), known && it.text() != QLatin1String(".") ? it.text() : KioModel::title(path)},
            {QStringLiteral("path"), path},
            {QStringLiteral("where"), parent.isEmpty() ? path : QUrl(parent).toDisplayString(QUrl::PreferLocalFile)},
            {QStringLiteral("exists"), true},
            {QStringLiteral("remote"), true},
            {QStringLiteral("isDir"), isDir},
            {QStringLiteral("isLink"), known && it.isLink()},
            {QStringLiteral("target"), known ? it.linkDest() : QString()},
            {QStringLiteral("mime"), mime},
            {QStringLiteral("kindText"), isDir ? QStringLiteral("Folder") : known ? it.mimeComment() : QString()},
            {QStringLiteral("size"), size},
            {QStringLiteral("sizeText"), size >= 0 ? fm.humanSize(size) + QStringLiteral(" (%L1 bytes)").arg(size) : QString()},
            {QStringLiteral("created"), known ? date(it.time(KFileItem::CreationTime)) : QString()},
            {QStringLiteral("modified"), known ? date(it.time(KFileItem::ModificationTime)) : QString()},
            {QStringLiteral("accessed"), known ? date(it.time(KFileItem::AccessTime)) : QString()},
            {QStringLiteral("owner"), known ? it.user() : QString()},
            {QStringLiteral("group"), known ? it.group() : QString()},
            {QStringLiteral("permOwner"), access(S_IRUSR, S_IWUSR)},
            {QStringLiteral("permGroup"), access(S_IRGRP, S_IWGRP)},
            {QStringLiteral("permOther"), access(S_IROTH, S_IWOTH)},
            {QStringLiteral("executable"), false},
            {QStringLiteral("tags"), QStringList()},
            {QStringLiteral("openWith"), app ? app->name() : QString()},
            {QStringLiteral("openWithIcon"), app ? app->icon() : QString()}};
}
} // namespace

QVariantMap Fm::info(const QString &path) const
{
    if (KioModel::isUrl(path))
        return remoteInfo(path, *this);
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
    if (KioModel::isUrl(path)) {   // not counted over the network: "--"
        QMetaObject::invokeMethod(this, [this, path] { Q_EMIT folderSizeReady(path, -1, 0, 0); }, Qt::QueuedConnection);
        return;
    }
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
    if (KioModel::isUrl(path))
        return {};   // no downloads for a preview
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

QString Fm::hostName() const
{
    return QSysInfo::machineHostName();
}

void Fm::updateWindow(QQuickWindow *window, int radius, bool shadow, const QVariantList &clear)
{
    if (!window)
        return;
    const QRegion shape = roundedRegion(window->width(), window->height(), radius);
    QRegion blur = shape;
    for (const QVariant &r : clear)
        blur -= r.toRectF().toAlignedRect();
    KWindowEffects::enableBlurBehind(window, true, blur);
    window->setMask(shape);

    if (!shadow) {
        if (m_shadow)
            m_shadow->destroy();
        return;
    }
    if (m_shadow && m_shadow->isCreated())
        return;
    if (!m_shadow) {
        // Shadow image: a blurred rounded rect, shifted down, with the window's own rounded shape cut
        // out (the glass is see-through, so nothing may darken it). The corner tiles reach `radius`
        // into the window rect, so the shadow also fills the area just outside each rounded corner;
        // with rectangle-only tiles that area stayed empty and the bottom corners (where the shadow
        // is strongest) looked notched compared with the top ones.
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
        {
            QPainter p(&img);
            p.setRenderHint(QPainter::Antialiasing);
            p.setCompositionMode(QPainter::CompositionMode_Clear);
            QPainterPath window;
            window.addRoundedRect(QRectF(pad, pad, inner, inner), radius, radius);
            p.fillPath(window, Qt::transparent);
        }
        const int c = pad + radius, mid = pad + inner / 2, end = pad + inner;
        m_shadow = new KWindowShadow(this);
        m_shadow->setTopLeftTile(tile(img, QRect(0, 0, c, c)));
        m_shadow->setTopTile(tile(img, QRect(mid, 0, 1, pad)));
        m_shadow->setTopRightTile(tile(img, QRect(end - radius, 0, c, c)));
        m_shadow->setRightTile(tile(img, QRect(end, mid, pad, 1)));
        m_shadow->setBottomRightTile(tile(img, QRect(end - radius, end - radius, c, c)));
        m_shadow->setBottomTile(tile(img, QRect(mid, end, 1, pad)));
        m_shadow->setBottomLeftTile(tile(img, QRect(0, end - radius, c, c)));
        m_shadow->setLeftTile(tile(img, QRect(0, mid, pad, 1)));
        m_shadow->setPadding(QMargins(pad, pad, pad, pad));
        m_shadow->setWindow(window);
    }
    m_shadow->create();
}

namespace
{
constexpr int kMaxKindCache = 50000;   // entries; the caches are simply dropped when they grow past this

// ms; -1 when the file is gone (never cached then)
qint64 changeTime(const QFileInfo &fi)
{
    return fi.exists() ? fi.metadataChangeTime().toMSecsSinceEpoch() : -1;
}
} // namespace

namespace
{
// kind() without the cache; also run in prefetchKinds()'s worker thread (QMimeDatabase is thread-safe)
QString kindOf(const QFileInfo &fi)
{
    if (fi.isDir())
        return QStringLiteral("folder");
    if (fi.suffix() == QLatin1String("desktop"))
        return QStringLiteral("app");
    // By name first (no disk reads); look inside only when the name says nothing. MatchContent,
    // not MatchDefault: the name was just matched, and doing it again took half the time.
    static const QMimeDatabase db;
    QMimeType mime = db.mimeTypeForFile(fi, QMimeDatabase::MatchExtension);
    if (mime.isDefault())
        mime = db.mimeTypeForFile(fi, QMimeDatabase::MatchContent);
    if (!mime.name().startsWith(QLatin1String("image/")) && fi.isExecutable())
        return QStringLiteral("exe");
    return kindOfMime(mime);
}

// image | exe | doc by MIME type alone (also for items listed through KIO)
QString kindOfMime(const QMimeType &mime)
{
    if (mime.name().startsWith(QLatin1String("image/")))
        return QStringLiteral("image");
    if (mime.inherits(QStringLiteral("application/x-executable")) || mime.inherits(QStringLiteral("application/x-shellscript")))
        return QStringLiteral("exe");
    return QStringLiteral("doc");
}
} // namespace

QString Fm::kind(const QString &path) const
{
    if (KioModel::isUrl(path)) {   // from what KIO listed (no I/O), else by name
        const KFileItem it = KioModel::findItem(path);
        if (!it.isNull())
            return it.isDir() ? QStringLiteral("folder") : kindOfMime(it.determineMimeType());
        static const QMimeDatabase db;
        return kindOfMime(db.mimeTypeForFile(QUrl(path).fileName(), QMimeDatabase::MatchExtension));
    }
    const QFileInfo fi(path);
    const qint64 ctime = changeTime(fi);
    const auto it = m_kindCache.constFind(path);
    if (it != m_kindCache.constEnd() && it->ctime == ctime)
        return it->kind;
    const QString k = kindOf(fi);
    if (ctime >= 0) {
        if (m_kindCache.size() >= kMaxKindCache)
            m_kindCache.clear();
        m_kindCache.insert(path, {ctime, k});
    }
    return k;
}

void Fm::prefetchKinds(const QString &dir)
{
    if (KioModel::isUrl(dir))
        return;
    const qint64 mtime = QFileInfo(dir).lastModified().toMSecsSinceEpoch();
    if (dir.isEmpty() || m_kindPrefetched.value(dir, -1) == mtime)
        return;   // done before, and no file was added, removed or renamed since
    if (m_kindPrefetched.size() > 200)
        m_kindPrefetched.clear();
    m_kindPrefetched.insert(dir, mtime);
    const int gen = ++m_kindGen;
    QPointer<Fm> self(this);
    (void)QtConcurrent::run([self, gen, dir] {
        constexpr int kMax = 20000, kBatch = 200;
        QList<QPair<QString, KindEntry>> batch;
        auto flush = [&] {
            if (!batch.isEmpty() && self)
                QMetaObject::invokeMethod(self, [self, b = batch] {
                    if (!self)
                        return;
                    for (const auto &[path, e] : b) {
                        if (self->m_kindCache.size() >= kMaxKindCache)
                            self->m_kindCache.clear();
                        self->m_kindCache.insert(path, e);
                    }
                });
            batch.clear();
        };
        // In name order, as the views list them by default: the top of the folder comes first.
        const QFileInfoList files = QDir(dir).entryInfoList(QDir::Files | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase);
        for (int i = 0; i < files.size() && i < kMax; ++i) {
            if (!self || self->m_kindGen.loadRelaxed() != gen)
                return;   // another folder was opened
            const qint64 ctime = changeTime(files[i]);
            if (ctime >= 0)
                batch.append({files[i].absoluteFilePath(), {ctime, kindOf(files[i])}});
            if (batch.size() >= kBatch)
                flush();
        }
        flush();
    });
}

QVariantMap Fm::desktopInfo(const QString &path) const
{
    if (KioModel::isUrl(path))
        return {};
    const qint64 ctime = changeTime(QFileInfo(path));
    const auto it = m_desktopCache.constFind(path);
    if (it != m_desktopCache.constEnd() && it->ctime == ctime)
        return it->info;
    const KDesktopFile df(path);
    const QVariantMap info{{QStringLiteral("name"), df.readName()}, {QStringLiteral("icon"), df.readIcon()}};
    if (ctime >= 0) {
        if (m_desktopCache.size() >= kMaxKindCache)
            m_desktopCache.clear();
        m_desktopCache.insert(path, {ctime, info});
    }
    return info;
}

QString Fm::freeSpace(const QString &path) const
{
    if (KioModel::isUrl(path))
        return {};
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
    // KIO URLs (smb://host/share, sftp://user@host/…, remote:/, mtp:/…) aren't checked here: the
    // view lists them asynchronously and shows an error if that fails. Unknown schemes are refused.
    const QString t = text.trimmed();
    if (t == QLatin1String("trash:") || t == QLatin1String("trash:/"))
        return {{QStringLiteral("ok"), true}, {QStringLiteral("dir"), trashDir()}};   // the Trash fm lists as a folder
    if (t == QLatin1String("network:") || t == QLatin1String("network:/"))
        return {{QStringLiteral("ok"), !KProtocolInfo::isKnownProtocol(QStringLiteral("network"))},
                {QStringLiteral("dir"), QStringLiteral("remote:/")}};   // without kio-extras' network:/, remote:/ is the Network
    if (KioModel::isUrl(t)) {
        const QUrl u(t);
        if (!u.isValid() || !KProtocolInfo::isKnownProtocol(u.scheme()))
            return {{QStringLiteral("ok"), false}};
        return {{QStringLiteral("ok"), true}, {QStringLiteral("dir"), KioModel::normalize(t)}};
    }
    if (KioModel::isUrl(base) && !t.startsWith(QLatin1Char('/')) && !t.startsWith(QLatin1Char('~')) && !t.startsWith(QLatin1String("file:"))) {
        QUrl b(KioModel::normalize(base));   // relative to a KIO folder
        if (!b.path().endsWith(QLatin1Char('/')))
            b.setPath(b.path() + QLatin1Char('/'));
        return {{QStringLiteral("ok"), true}, {QStringLiteral("dir"), KioModel::normalize(b.resolved(QUrl(t)).toString())}};
    }
    const QFileInfo fi(expandPath(text, base));
    if (fi.isDir())
        return {{QStringLiteral("ok"), true}, {QStringLiteral("dir"), fi.absoluteFilePath()}};
    if (fi.exists())
        return {{QStringLiteral("ok"), true}, {QStringLiteral("dir"), fi.absolutePath()}, {QStringLiteral("file"), fi.absoluteFilePath()}};
    return {{QStringLiteral("ok"), false}};
}

QString Fm::completePath(const QString &text, const QString &base) const
{
    if (KioModel::isUrl(text) || (KioModel::isUrl(base) && !text.startsWith(QLatin1Char('/')) && !text.startsWith(QLatin1Char('~'))))
        return text;   // no completion over KIO (it would block on the network)
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
    // Remote files: KIO finds the type and hands the URL to the app, or downloads a copy first.
    auto *job = new KIO::OpenUrlJob(toUrl(path));
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
    QProcess::startDetached(QStringLiteral("alacritty"), {QStringLiteral("--working-directory"), KioModel::isUrl(dir) ? QDir::homePath() : dir});
}

void Fm::openInFirefox(const QString &url)
{
    QProcess::startDetached(QStringLiteral("firefox"), url.isEmpty() ? QStringList() : QStringList{url});
}

QString Fm::downloadTarget(const QString &fileName) const
{
    // Downloads go to the Downloads folder, under a free name (never over an existing file).
    const QString folder = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    QDir().mkpath(folder);
    // Only a plain name: a page's suggested name must not climb out of Downloads ("../x", "/x").
    QString name = QFileInfo(fileName).fileName();
    if (name.isEmpty() || name == QLatin1String(".") || name == QLatin1String(".."))
        name = QStringLiteral("download");
    const QFileInfo fi(name);
    for (int i = 1; QFileInfo::exists(folder + QLatin1Char('/') + name); ++i)
        name = fi.completeBaseName() + QStringLiteral(" (%1)").arg(i) + (fi.suffix().isEmpty() ? QString() : QLatin1Char('.') + fi.suffix());
    return folder + QLatin1Char('/') + name;
}

void Fm::newWindow(const QString &dir)
{
    Q_EMIT newWindowRequested(dir);   // a window in this process (one fm, one web profile)
}

void Fm::overview()
{
    QProcess::startDetached(QStringLiteral("qdbus6"),
                            {QStringLiteral("org.kde.kglobalaccel"), QStringLiteral("/component/kwin"),
                             QStringLiteral("org.kde.kglobalaccel.Component.invokeShortcut"), QStringLiteral("Overview")});
}

void Fm::trash(const QStringList &paths)
{
    // Only local items have a Trash; the window asks before deleting remote ones (deletePermanently).
    QStringList local;
    for (const QString &p : paths)
        if (!KioModel::isUrl(p))
            local << p;
    if (!local.isEmpty())
        undoable(KIO::trash(toUrls(local)));
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
        undoable(KIO::copy(mime->urls(), toUrl(dir)));
}

void Fm::drop(const QList<QUrl> &urls, const QString &dir, bool move)
{
    const QUrl dest = toUrl(dir);
    QList<QUrl> todo;
    for (const QUrl &u : urls) {
        // Dropping an item onto its own folder (or onto itself) is a no-op.
        if (u.isLocalFile() && dest.isLocalFile()) {
            const QString local = u.toLocalFile();
            if (QFileInfo(local).absolutePath() == QDir(dir).absolutePath() || local == dir)
                continue;
        } else {
            const QString item = KioModel::normalize(u.toString()), target = KioModel::normalize(dest.toString());
            if (item == target || KioModel::parentUrl(item) == target)
                continue;
        }
        todo << u;
    }
    if (todo.isEmpty())
        return;
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
    if (KioModel::isUrl(dir) || std::any_of(urls.begin(), urls.end(), [](const QUrl &u) { return !u.isLocalFile(); })) {
        // KIO URLs: move within the same server (scheme + host), copy between servers and the disk.
        const QUrl d(dir);
        const bool same = KioModel::isUrl(dir) && std::all_of(urls.begin(), urls.end(), [&](const QUrl &u) {
            return u.scheme() == d.scheme() && u.authority() == d.authority();
        });
        drop(urls, dir, same && !copyModifierHeld());
        return;
    }
    const QString destRoot = QStorageInfo(dir).rootPath();
    bool sameDisk = true;
    for (const QUrl &u : urls)
        sameDisk &= QStorageInfo(QFileInfo(u.toLocalFile()).absolutePath()).rootPath() == destRoot;
    drop(urls, dir, sameDisk && !copyModifierHeld());
}

QString Fm::makeFolder(const QString &dir)
{
    if (KioModel::isUrl(dir)) {   // a free name among the items the view has listed
        QString name = QStringLiteral("New Folder");
        for (int i = 2; !KioModel::findItem(KioModel::childUrl(dir, name)).isNull(); ++i)
            name = QStringLiteral("New Folder %1").arg(i);
        const QString url = KioModel::childUrl(dir, name);
        KIO::SimpleJob *job = KIO::mkdir(QUrl(url));
        KIO::FileUndoManager::self()->recordJob(KIO::FileUndoManager::Mkdir, {}, QUrl(url), job);
        track(job);
        return url;
    }
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
    if (KioModel::isUrl(path)) {
        const QString parent = KioModel::parentUrl(path);
        if (parent.isEmpty() || newName.isEmpty() || newName.contains(QLatin1Char('/')) || newName == QUrl(path).fileName())
            return;
        undoable(KIO::moveAs(QUrl(path), QUrl(KioModel::childUrl(parent, newName)), KIO::HideProgressInfo));
        return;
    }
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
    if (KioModel::isUrl(path))
        return {};   // tags are xattrs on local files
    return readTags(path);
}

void Fm::setTags(const QString &path, const QStringList &list)
{
    if (KioModel::isUrl(path))
        return;
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
    if (!here.isEmpty() && !KioModel::isUrl(here))
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
