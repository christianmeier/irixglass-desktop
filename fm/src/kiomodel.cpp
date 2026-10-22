#include "kiomodel.h"

#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <KCoreDirLister>
#include <KIO/Job>
#include <KIO/JobUiDelegateFactory>
#include <KIO/ListJob>
#include <KIO/StatJob>
#include <KJobUiDelegate>

namespace
{
QList<KioModel *> &models()
{
    static QList<KioModel *> list;
    return list;
}

QHash<QString, QString> &names()
{
    static QHash<QString, QString> h;
    return h;
}

// Listing jobs get KIO's UI delegate (without automatic error dialogs: errors show in the view), so
// SSL questions can be asked; password prompts come from kpasswdserver (kiod) either way.
class Lister : public KCoreDirLister
{
public:
    using KCoreDirLister::KCoreDirLister;

protected:
    void jobStarted(KIO::ListJob *job) override
    {
        if (!job->uiDelegate())
            job->setUiDelegate(KIO::createDefaultJobUiDelegate(KJobUiDelegate::Flags{}, nullptr));
    }
};

QString lastSegment(const QUrl &u)
{
    const QStringList parts = u.path(QUrl::FullyDecoded).split(QLatin1Char('/'), Qt::SkipEmptyParts);
    return parts.isEmpty() ? QString() : parts.last();
}

// Devices whose URLs carry ids instead of names: their root gets its display name from a stat.
bool deviceScheme(const QString &scheme)
{
    static const QSet<QString> s{QStringLiteral("mtp"), QStringLiteral("afc"), QStringLiteral("obexftp"),
                                 QStringLiteral("kdeconnect"), QStringLiteral("bluetooth")};
    return s.contains(scheme);
}

void askDisplayName(const QUrl &url)
{
    static QSet<QString> asked;
    const QString key = url.toString();
    if (asked.contains(key))
        return;
    asked.insert(key);
    auto *job = KIO::stat(url, KIO::StatJob::SourceSide, KIO::StatDefaultDetails, KIO::HideProgressInfo);
    QObject::connect(job, &KJob::result, job, [job, key] {
        if (job->error())
            return;
        const QString name = job->statResult().stringValue(KIO::UDSEntry::UDS_DISPLAY_NAME);
        if (!name.isEmpty())
            KioModel::setDisplayName(key, name);
    });
}

QString rootLabel(const QUrl &u)
{
    const QString scheme = u.scheme();
    if (scheme == QLatin1String("remote"))
        return QStringLiteral("Network");
    if (!u.host().isEmpty())
        return u.userName().isEmpty() ? u.host() : u.userName() + QLatin1Char('@') + u.host();
    if (scheme == QLatin1String("smb"))
        return QStringLiteral("Shared Folders");
    if (scheme == QLatin1String("mtp") || scheme == QLatin1String("afc") || scheme == QLatin1String("obexftp"))
        return QStringLiteral("Devices");
    if (scheme == QLatin1String("trash"))
        return QStringLiteral("Trash");
    return scheme + QLatin1Char(':');
}
} // namespace

// ---- URL helpers ----------------------------------------------------------------------------

bool KioModel::isUrl(const QString &s)
{
    static const QRegularExpression re(QStringLiteral("^([A-Za-z][A-Za-z0-9+.-]*):"));
    const auto m = re.match(s);
    return m.hasMatch() && m.captured(1).compare(QLatin1String("file"), Qt::CaseInsensitive) != 0;
}

QString KioModel::normalize(const QString &url)
{
    const QString t = url.trimmed();
    QUrl u(t);
    if (!u.isValid() || u.scheme().isEmpty())
        return t;
    if (u.path().startsWith(QLatin1Char('/')))
        u = u.adjusted(QUrl::StripTrailingSlash | QUrl::NormalizePathSegments);
    if (u.host().isEmpty() && u.path().isEmpty()) {   // "smb://", "remote:" → "smb:/", "remote:/"
        u.setAuthority(QString());
        u.setPath(QStringLiteral("/"));
    }
    return u.toString();
}

QString KioModel::parentUrl(const QString &url)
{
    const QUrl u(normalize(url));
    const QString path = u.path();
    if (!path.startsWith(QLatin1Char('/')) || path == QLatin1String("/"))
        return {};   // the top (or an opaque device URL such as mtp:udi=…)
    return normalize(u.adjusted(QUrl::RemoveFilename | QUrl::StripTrailingSlash).toString());
}

QString KioModel::childUrl(const QString &dir, const QString &name)
{
    QUrl u(normalize(dir));
    QString path = u.path(QUrl::FullyDecoded);
    if (!path.endsWith(QLatin1Char('/')))
        path += QLatin1Char('/');
    u.setPath(path + name, QUrl::DecodedMode);
    return normalize(u.toString());
}

QVariantList KioModel::crumbs(const QString &url)
{
    const QString dir = normalize(url);
    const QUrl u(dir);
    auto crumb = [](const QString &label, const QString &path) { return QVariantMap{{QStringLiteral("label"), label}, {QStringLiteral("path"), path}}; };
    const QString path = u.path(QUrl::FullyDecoded);
    if (!path.startsWith(QLatin1Char('/'))) {   // mtp:udi=… (kio_mtp redirects it to mtp:/<device>/ once listed)
        if (deviceScheme(u.scheme()))
            askDisplayName(u);
        const QString name = displayName(dir);
        return {crumb(name.isEmpty() ? (path.isEmpty() ? rootLabel(u) : QStringLiteral("Device")) : name, dir)};
    }
    QUrl root = u;
    root.setPath(QStringLiteral("/"));
    const QString rootPath = normalize(root.toString());
    if (deviceScheme(u.scheme()) && !u.host().isEmpty())
        askDisplayName(root);   // afc://<udid>/: the phone's name
    QString rootName = displayName(rootPath);
    QVariantList out{crumb(rootName.isEmpty() ? rootLabel(u) : rootName, path.isEmpty() ? dir : rootPath)};
    QString acc;
    const QStringList parts = path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (int i = 0; i < parts.size(); ++i) {
        acc += QLatin1Char('/') + parts[i];
        QUrl c = u;
        c.setPath(acc, QUrl::DecodedMode);
        const QString cp = normalize(c.toString());
        if (i == 0 && deviceScheme(u.scheme()) && u.host().isEmpty())
            askDisplayName(c);   // mtp:/<device>, afc:/<device>
        const QString name = displayName(cp);
        out << crumb(name.isEmpty() ? parts[i] : name, cp);
    }
    return out;
}

QString KioModel::title(const QString &url)
{
    const QVariantList c = crumbs(url);
    return c.isEmpty() ? url : c.last().toMap().value(QStringLiteral("label")).toString();
}

QString KioModel::displayName(const QString &url)
{
    return names().value(url);
}

void KioModel::setDisplayName(const QString &url, const QString &name)
{
    const QString key = normalize(url);
    if (name.isEmpty() || names().value(key) == name)
        return;
    names().insert(key, name);
    KioUrl::instance()->bump();
}

KFileItem KioModel::findItem(const QString &path)
{
    for (const KioModel *m : std::as_const(models())) {
        const auto it = m->m_all.constFind(path);
        if (it != m->m_all.constEnd())
            return *it;
        if (path == m->m_url) {
            const KFileItem root = m->m_lister->rootItem();
            if (!root.isNull())
                return root;
        }
    }
    return {};
}

KioUrl *KioUrl::instance()
{
    static KioUrl *k = new KioUrl;
    return k;
}

// ---- the model ----------------------------------------------------------------------------------

KioModel::KioModel(QObject *parent)
    : QAbstractListModel(parent)
    , m_lister(new Lister(this))
{
    models() << this;
    m_lister->setAutoErrorHandlingEnabled(false);
    m_lister->setShowHiddenFiles(true);   // filtered here, so toggling needs no new listing
    m_lister->setAutoUpdate(true);
    connect(m_lister, &KCoreDirLister::newItems, this, &KioModel::onNewItems);
    connect(m_lister, &KCoreDirLister::itemsDeleted, this, &KioModel::onDeleted);
    connect(m_lister, &KCoreDirLister::refreshItems, this, &KioModel::onRefresh);
    connect(m_lister, &KCoreDirLister::clear, this, &KioModel::onClear);
    connect(m_lister, &KCoreDirLister::completed, this, [this] {
        const KFileItem root = m_lister->rootItem();
        // a device's root names the device (only when the worker says so: tar's root is the archive's)
        const QString shown = root.isNull() ? QString() : root.entry().stringValue(KIO::UDSEntry::UDS_DISPLAY_NAME);
        if (!shown.isEmpty() && shown != QLatin1String(".") && shown != lastSegment(QUrl(m_url)) && deviceScheme(QUrl(m_url).scheme()))
            setDisplayName(m_url, shown);
        if (m_status == Loading)
            setStatus(Ready);
    });
    connect(m_lister, &KCoreDirLister::canceled, this, [this] {
        if (m_status == Loading)
            setStatus(Ready);
    });
    connect(m_lister, &KCoreDirLister::jobError, this, [this](KIO::Job *job) { setStatus(Error, job->errorString()); });
    connect(m_lister, &KCoreDirLister::redirection, this, [this](const QUrl &, const QUrl &to) {
        const QString target = normalize(to.toString());
        if (target == m_url)
            return;
        const QString from = m_url;
        m_url = target;
        Q_EMIT urlChanged();
        Q_EMIT redirected(from, target);
    });
}

KioModel::~KioModel()
{
    models().removeAll(this);
}

void KioModel::setStatus(Status s, const QString &error)
{
    if (s == m_status && error == m_error)
        return;
    m_status = s;
    m_error = error;
    Q_EMIT statusChanged();
}

void KioModel::setUrl(const QString &url)
{
    const QString n = url.isEmpty() ? QString() : normalize(url);
    if (n == m_url)
        return;
    m_url = n;
    Q_EMIT urlChanged();
    onClear();
    if (m_url.isEmpty()) {
        m_lister->stop();
        setStatus(Null);
        return;
    }
    setStatus(Loading);
    m_lister->openUrl(QUrl(m_url));
}

void KioModel::reload()
{
    if (m_url.isEmpty())
        return;
    setStatus(Loading);
    m_lister->openUrl(QUrl(m_url), KCoreDirLister::Reload);
}

void KioModel::setSortField(int f)
{
    if (f == m_sortField)
        return;
    m_sortField = f;
    resort();
    Q_EMIT sortChanged();
}

void KioModel::setSortReversed(bool r)
{
    if (r == m_reversed)
        return;
    m_reversed = r;
    resort();
    Q_EMIT sortChanged();
}

void KioModel::setShowHidden(bool s)
{
    if (s == m_showHidden)
        return;
    m_showHidden = s;
    resort();
    Q_EMIT showHiddenChanged();
}

QString KioModel::pathOf(const KFileItem &item)
{
    // Links to other places (remote:/'s entries → smb:/, mtp:/ …) open their target.
    const QUrl target = item.targetUrl();
    return normalize((item.isDir() && target.isValid() && target != item.url() ? target : item.url()).toString());
}

bool KioModel::lessThan(const Row &a, const Row &b) const
{
    const bool ad = a.item.isDir(), bd = b.item.isDir();
    if (ad != bd)
        return ad;   // folders first, also reversed (as FolderListModel / QDir::DirsFirst)
    int c = 0;
    auto cmp = [](auto x, auto y) { return x < y ? -1 : x > y ? 1 : 0; };
    switch (m_sortField) {
    case 2:   // Time: newest first
        c = cmp(b.item.time(KFileItem::ModificationTime), a.item.time(KFileItem::ModificationTime));
        break;
    case 3:   // Size: largest first
        c = cmp(b.item.size(), a.item.size());
        break;
    case 4:   // Type: by extension
        c = QFileInfo(a.item.text()).suffix().compare(QFileInfo(b.item.text()).suffix(), Qt::CaseInsensitive);
        break;
    default:
        break;
    }
    if (c == 0)
        c = a.item.text().compare(b.item.text(), Qt::CaseInsensitive);
    if (c == 0)
        c = a.path.compare(b.path);
    return m_reversed ? c > 0 : c < 0;
}

int KioModel::insertPos(const Row &r) const
{
    return int(std::upper_bound(m_rows.begin(), m_rows.end(), r, [this](const Row &x, const Row &y) { return lessThan(x, y); }) - m_rows.begin());
}

int KioModel::rowOf(const QString &path) const
{
    for (int i = 0; i < m_rows.size(); ++i)
        if (m_rows[i].path == path)
            return i;
    return -1;
}

void KioModel::insertItem(const KFileItem &item)
{
    const QString path = pathOf(item);
    if (m_all.contains(path))
        removePath(path);
    m_all.insert(path, item);
    if (item.isDir() && item.entry().contains(KIO::UDSEntry::UDS_DISPLAY_NAME) && item.text() != lastSegment(QUrl(path)))
        setDisplayName(path, item.text());   // e.g. remote:/'s "Shared Folders (SMB)" for smb:/
    if (!visible(item))
        return;
    const Row r{item, path};
    const int at = insertPos(r);
    beginInsertRows({}, at, at);
    m_rows.insert(at, r);
    endInsertRows();
}

void KioModel::removePath(const QString &path)
{
    m_all.remove(path);
    const int i = rowOf(path);
    if (i < 0)
        return;
    beginRemoveRows({}, i, i);
    m_rows.removeAt(i);
    endRemoveRows();
}

void KioModel::resort()
{
    beginResetModel();
    m_rows.clear();
    for (auto it = m_all.cbegin(); it != m_all.cend(); ++it)
        if (visible(*it))
            m_rows << Row{*it, it.key()};
    std::sort(m_rows.begin(), m_rows.end(), [this](const Row &a, const Row &b) { return lessThan(a, b); });
    endResetModel();
    Q_EMIT countChanged();
}

void KioModel::onNewItems(const KFileItemList &items)
{
    const int before = count();
    for (const KFileItem &item : items)
        insertItem(item);
    if (count() != before)
        Q_EMIT countChanged();
}

void KioModel::onDeleted(const KFileItemList &items)
{
    const int before = count();
    for (const KFileItem &item : items)
        removePath(pathOf(item));
    if (count() != before)
        Q_EMIT countChanged();
}

void KioModel::onRefresh(const QList<QPair<KFileItem, KFileItem>> &items)
{
    const int before = count();
    for (const auto &[oldItem, newItem] : items) {
        removePath(pathOf(oldItem));
        insertItem(newItem);
    }
    if (count() != before)
        Q_EMIT countChanged();
}

void KioModel::onClear()
{
    if (m_all.isEmpty() && m_rows.isEmpty())
        return;
    beginResetModel();
    m_rows.clear();
    m_all.clear();
    endResetModel();
    Q_EMIT countChanged();
}

int KioModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : count();
}

QHash<int, QByteArray> KioModel::roleNames() const
{
    return {{FileName, "fileName"}, {FilePath, "filePath"}, {FileUrl, "fileUrl"}, {FileURL, "fileURL"},
            {FileIsDir, "fileIsDir"}, {FileModified, "fileModified"}, {FileAccessed, "fileAccessed"},
            {FileSize, "fileSize"}, {FileBaseName, "fileBaseName"}, {FileSuffix, "fileSuffix"},
            {FileIsRemote, "fileIsRemote"}, {FileMimeType, "fileMimeType"}};
}

QVariant KioModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_rows.size())
        return {};
    const Row &r = m_rows[index.row()];
    const KFileItem &it = r.item;
    switch (role) {
    case FileName:
        return it.text();
    case FilePath:
        return r.path;
    case FileUrl:
    case FileURL:
        return QUrl(r.path);
    case FileIsDir:
        return it.isDir();
    case FileModified:
        return it.time(KFileItem::ModificationTime);
    case FileAccessed:
        return it.time(KFileItem::AccessTime);
    case FileSize:
        return it.entry().contains(KIO::UDSEntry::UDS_SIZE) ? qint64(it.size()) : qint64(0);
    case FileBaseName:
        return QFileInfo(it.text()).completeBaseName();
    case FileSuffix:
        return QFileInfo(it.text()).suffix();
    case FileIsRemote:
        return !it.url().isLocalFile();
    case FileMimeType:
        return it.mimetype();
    default:
        return {};
    }
}

QVariant KioModel::get(int index, const QString &role) const
{
    const QHash<int, QByteArray> roles = roleNames();
    const int r = roles.key(role.toUtf8(), -1);
    return r < 0 ? QVariant() : data(this->index(index), r);
}

int KioModel::indexOf(const QString &path) const
{
    return rowOf(path);
}
