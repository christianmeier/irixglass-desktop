// fm: folders that aren't local (smb://, sftp://, ftp://, fish://, remote:/, mtp:, afc:, tar:, …),
// listed through KIO. Local folders keep Qt's FolderListModel (tuned and tested); the views switch
// to this model when the window's `dir` is a URL with another scheme than file.
#pragma once

#include <QAbstractListModel>
#include <QDateTime>
#include <QHash>
#include <QList>
#include <QPointer>
#include <QUrl>
#include <QVariantList>
#include <KFileItem>

class KCoreDirLister;
namespace KIO { class Job; }

// A list model on KCoreDirLister with FolderListModel's roles (fileName, filePath, fileIsDir,
// fileUrl, fileModified, fileSize, …) plus fileIsRemote and fileMimeType. filePath is the item's
// URL as a string (the target URL for links such as remote:/'s "Shared Folders (SMB)" → smb:/),
// so the window keeps keying selection, tabs and history by plain strings.
class KioModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(QString url READ url WRITE setUrl NOTIFY urlChanged)
    // FolderListModel's numbers: Unsorted 0, Name 1, Time 2, Size 3, Type 4. Folders always first.
    Q_PROPERTY(int sortField READ sortField WRITE setSortField NOTIFY sortChanged)
    Q_PROPERTY(bool sortReversed READ sortReversed WRITE setSortReversed NOTIFY sortChanged)
    Q_PROPERTY(bool showHidden READ showHidden WRITE setShowHidden NOTIFY showHiddenChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString errorString READ errorString NOTIFY statusChanged)

public:
    // Null, Ready and Loading have FolderListModel's values, so `status !== FolderListModel.Ready` works for both.
    enum Status { Null = 0, Ready = 1, Loading = 2, Error = 3 };
    Q_ENUM(Status)
    enum Roles { FileName = Qt::UserRole + 1, FilePath, FileUrl, FileURL, FileIsDir, FileModified, FileAccessed, FileSize,
                 FileBaseName, FileSuffix, FileIsRemote, FileMimeType };

    explicit KioModel(QObject *parent = nullptr);
    ~KioModel() override;

    QString url() const { return m_url; }
    void setUrl(const QString &url);
    int sortField() const { return m_sortField; }
    void setSortField(int f);
    bool sortReversed() const { return m_reversed; }
    void setSortReversed(bool r);
    bool showHidden() const { return m_showHidden; }
    void setShowHidden(bool s);
    int count() const { return int(m_rows.size()); }
    int status() const { return m_status; }
    QString errorString() const { return m_error; }

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE QVariant get(int index, const QString &role) const;
    Q_INVOKABLE int indexOf(const QString &path) const;
    Q_INVOKABLE void reload();

    // ---- URL helpers (also the QML singleton `Kio`, see KioUrl) ----
    // True for "scheme:…" with a scheme other than file (fm's local folders are plain paths).
    static bool isUrl(const QString &s);
    // One spelling per folder: no trailing slash, ".." resolved, "smb://" → "smb:/", "remote:" → "remote:/".
    static QString normalize(const QString &url);
    static QString parentUrl(const QString &url);           // "" at the top
    static QString childUrl(const QString &dir, const QString &name);
    static QString title(const QString &url);                // tab title: the last folder's (display) name
    static QVariantList crumbs(const QString &url);         // [{label, path}] for the path bar
    // An item some live model has listed (or its folder's root item); null when unknown.
    static KFileItem findItem(const QString &path);
    // Readable names for URLs whose path segments aren't (mtp:udi=…, afc://<udid>/, remote:/…):
    // learnt from listed items' display names and from a stat of a device's root.
    static QString displayName(const QString &url);
    static void setDisplayName(const QString &url, const QString &name);

Q_SIGNALS:
    void urlChanged();
    void sortChanged();
    void showHiddenChanged();
    void countChanged();
    void statusChanged();
    void redirected(const QString &from, const QString &to);

private:
    struct Row {
        KFileItem item;
        QString path;
    };
    static QString pathOf(const KFileItem &item);
    bool visible(const KFileItem &item) const { return m_showHidden || !item.isHidden(); }
    bool lessThan(const Row &a, const Row &b) const;
    int insertPos(const Row &r) const;
    int rowOf(const QString &path) const;
    void insertItem(const KFileItem &item);
    void removePath(const QString &path);
    void resort();
    void setStatus(Status s, const QString &error = {});
    void onNewItems(const KFileItemList &items);
    void onDeleted(const KFileItemList &items);
    void onRefresh(const QList<QPair<KFileItem, KFileItem>> &items);
    void onClear();

    KCoreDirLister *m_lister = nullptr;
    QString m_url;
    int m_sortField = 1;
    bool m_reversed = false;
    bool m_showHidden = false;
    Status m_status = Null;
    QString m_error;
    QList<Row> m_rows;                 // shown, sorted
    QHash<QString, KFileItem> m_all;   // every listed item (also hidden ones), by path
};

// The QML singleton `Kio` (import IrixFm): KioModel's URL helpers. `rev` changes when a display
// name is learnt, so bindings that pass it (crumbs(dir, rev)) re-read.
class KioUrl : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int rev READ rev NOTIFY revChanged)
public:
    static KioUrl *instance();
    int rev() const { return m_rev; }
    Q_INVOKABLE bool isUrl(const QString &s) const { return KioModel::isUrl(s); }
    Q_INVOKABLE QString normalize(const QString &s) const { return KioModel::normalize(s); }
    Q_INVOKABLE QString parentUrl(const QString &s) const { return KioModel::parentUrl(s); }
    Q_INVOKABLE QString title(const QString &s, int rev = 0) const { Q_UNUSED(rev) return KioModel::title(s); }
    Q_INVOKABLE QVariantList crumbs(const QString &s, int rev = 0) const { Q_UNUSED(rev) return KioModel::crumbs(s); }
    Q_INVOKABLE void setDisplayName(const QString &url, const QString &name) { KioModel::setDisplayName(url, name); }
    void bump() { ++m_rev; Q_EMIT revChanged(); }
Q_SIGNALS:
    void revChanged();
private:
    int m_rev = 0;
};
