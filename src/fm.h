// fm: IRIX-style file manager themed like macOS Tahoe. C++ side: window effects and KIO file actions.
#pragma once

#include <QAtomicInt>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QUrl>
#include <QVariantMap>

class QQuickWindow;
class KWindowShadow;

class Fm : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString startDir MEMBER m_startDir CONSTANT)
    // True when fm was started without a folder: the window then restores its saved tabs.
    Q_PROPERTY(bool restoreSession MEMBER m_restoreSession CONSTANT)
    Q_PROPERTY(QString home READ home CONSTANT)
    Q_PROPERTY(QString userName READ userName CONSTANT)
    Q_PROPERTY(QString assetDir MEMBER m_assetDir CONSTANT)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY undoChanged)
    Q_PROPERTY(QString undoText READ undoText NOTIFY undoChanged)
    Q_PROPERTY(QString trashDir READ trashDir CONSTANT)
    Q_PROPERTY(QVariantList volumes READ volumes NOTIFY volumesChanged)
    // Height of bottom panels that windows go *below* (the floating dock), which a maximized window
    // must keep free; follows Plasma's panel settings live.
    Q_PROPERTY(int dockReserve READ dockReserve NOTIFY dockReserveChanged)

public:
    Fm(const QString &startDir, const QString &assetDir, QObject *parent = nullptr, bool restoreSession = false);

    QString home() const;
    QString userName() const;
    bool canUndo() const;
    QString undoText() const;
    QString trashDir() const;
    QVariantList volumes() const;
    int dockReserve() const { return m_dockReserve; }

    // Blur behind + input mask in the window's rounded shape, plus a compositor-drawn shadow.
    Q_INVOKABLE void updateWindow(QQuickWindow *window, int radius, bool shadow);

    Q_INVOKABLE QString kind(const QString &path) const;  // folder | app | image | exe | doc
    Q_INVOKABLE QVariantMap desktopInfo(const QString &path) const;  // {name, icon}
    Q_INVOKABLE QString freeSpace(const QString &path) const;
    Q_INVOKABLE QString humanSize(qint64 bytes) const;
    Q_INVOKABLE QString uriList(const QStringList &paths) const;

    // Typed path -> {ok, dir, file}. Accepts absolute paths, ~, paths relative to `base` and file:// URLs.
    // A file resolves to its folder plus the file to select.
    Q_INVOKABLE QVariantMap resolvePath(const QString &text, const QString &base) const;
    // Tab completion of the last path component against folder names (case-insensitive).
    Q_INVOKABLE QString completePath(const QString &text, const QString &base) const;

    Q_INVOKABLE void open(const QString &path);
    Q_INVOKABLE void openInDolphin(const QString &path);
    Q_INVOKABLE void openTerminal(const QString &dir);
    Q_INVOKABLE void openInFirefox(const QString &url);
    Q_INVOKABLE void newWindow(const QString &dir);
    Q_INVOKABLE void overview();

    Q_INVOKABLE void trash(const QStringList &paths);
    Q_INVOKABLE void copyToClipboard(const QStringList &paths);
    Q_INVOKABLE void copyPathText(const QString &text);
    Q_INVOKABLE bool canPaste() const;
    Q_INVOKABLE void paste(const QString &dir);
    Q_INVOKABLE void drop(const QList<QUrl> &urls, const QString &dir, bool move);
    // A drag that started inside fm (e.g. between the split view's sides), Finder style: move on the
    // same disk, copy onto another disk, and Ctrl held while dropping copies.
    Q_INVOKABLE void dropInternal(const QList<QUrl> &urls, const QString &dir);
    Q_INVOKABLE bool copyModifierHeld() const;
    Q_INVOKABLE void copyOrMove(const QStringList &paths, const QString &dir, bool move);   // F5 / F6
    Q_INVOKABLE QString makeFolder(const QString &dir);
    Q_INVOKABLE void rename(const QString &path, const QString &newName);

    // Finder file commands. They return the new paths so the view can select them once they exist.
    Q_INVOKABLE QStringList duplicate(const QStringList &paths);    // "name copy", "name copy 2", …
    Q_INVOKABLE QStringList makeAlias(const QStringList &paths);    // symlink "name alias"
    Q_INVOKABLE QString showOriginal(const QString &path) const;    // target of a symlink, or ""
    Q_INVOKABLE void pasteMove(const QString &dir);                 // move the clipboard items here
    Q_INVOKABLE void undo();

    // Get Info and Quick Look.
    Q_INVOKABLE QVariantMap info(const QString &path) const;
    // Answers with folderSizeReady: at once from the size cache when the folder hasn't changed,
    // otherwise after counting it (KIO). cachedFolderSize() returns the last known size ("" if none).
    Q_INVOKABLE void folderSize(const QString &path);
    Q_INVOKABLE QString cachedFolderSize(const QString &path) const;
    Q_INVOKABLE QString textPreview(const QString &path, int maxBytes = 262144) const;  // "" for binary files
    // Rich preview of a text file: {kind: "markdown" | "code" | "plain", html, language}.
    // Markdown is rendered; code (and fenced code in Markdown) is highlighted with the Nord theme
    // when KSyntaxHighlighting knows the language.
    Q_INVOKABLE QVariantMap richPreview(const QString &path, int maxBytes = 262144) const;
    Q_INVOKABLE QStringList highlightThemes() const;

    // Trash: the home trash is listed directly (trashDir); Put Back and Empty go through KIO.
    Q_INVOKABLE void restoreFromTrash(const QStringList &paths);
    Q_INVOKABLE void emptyTrash();
    Q_INVOKABLE void deletePermanently(const QStringList &paths);
    Q_INVOKABLE int trashCount() const;

    // Open With: apps for the item's type (default first); an empty id shows KDE's "Open With" dialog.
    Q_INVOKABLE QVariantList appsFor(const QString &path) const;
    Q_INVOKABLE QVariantList applications() const;   // every visible app with an icon: {name, icon}, by name
    Q_INVOKABLE void openWith(const QStringList &paths, const QString &appId);

    // Removable drives (Solid): mount on click, eject.
    Q_INVOKABLE void mountVolume(const QString &udi);
    Q_INVOKABLE void ejectVolume(const QString &udi);

    // Search by name in a background thread (in root only, or with all subfolders); results
    // arrive in batches as searchBatch, then searchDone.
    Q_INVOKABLE void startSearch(const QString &root, const QString &text, bool hidden, bool recursive);
    Q_INVOKABLE void stopSearch();

    // Compress (zip) and expand archives in a background thread; answers with archiveDone.
    Q_INVOKABLE bool isArchive(const QString &path) const;
    Q_INVOKABLE QString compress(const QStringList &paths);
    Q_INVOKABLE QString expand(const QString &path);

    // Tags (colour labels): the xattr user.xdg.tags, shared with Dolphin and Baloo. `rev` is
    // ignored; QML passes a counter so bindings re-read after a change.
    Q_INVOKABLE QStringList tags(const QString &path, int rev = 0) const;
    Q_INVOKABLE void setTags(const QString &path, const QStringList &tags);
    Q_INVOKABLE void addTag(const QStringList &paths, const QString &tag);
    Q_INVOKABLE void removeTag(const QStringList &paths, const QString &tag);
    Q_INVOKABLE void clearTags(const QStringList &paths);
    // Everything with a tag, from Baloo, fm's own index and the folder being viewed (`here`);
    // answers like startSearch.
    Q_INVOKABLE void startTagSearch(const QString &tag, const QString &here = QString());
    // Applied to every file that has the tag (background); tagsChanged when done.
    Q_INVOKABLE void renameTagEverywhere(const QString &from, const QString &to);
    Q_INVOKABLE void deleteTagEverywhere(const QString &tag);

Q_SIGNALS:
    void undoChanged();
    void folderSizeReady(const QString &path, qint64 bytes, qint64 files, qint64 folders);
    void volumesChanged();
    void volumeMounted(const QString &udi, const QString &path);
    void volumeError(const QString &message);
    void searchBatch(const QVariantList &items);
    void searchDone(int count, bool truncated);
    void archiveDone(const QString &path, bool ok);
    void tagsChanged();
    void dockReserveChanged();

private:
    void refreshVolumes();
    void loadTagIndex();
    void saveTagIndex() const;
    void indexTags(const QString &path, const QStringList &tags);
    QStringList taggedCandidates(const QString &tag) const;  // index + Baloo, not yet verified
    void startNextSize();
    void loadSizeCache();
    void readDockReserve();
    void saveSizeCacheSoon();
    static QString freeName(const QString &dir, const QString &base, const QString &suffix);
    QString m_startDir;
    bool m_restoreSession = false;
    QString m_assetDir;
    QPointer<KWindowShadow> m_shadow;
    QVariantList m_volumes;
    QStringList m_sizeQueue;    // folder-size requests waiting; at most kMaxSizeJobs run at once
    int m_sizeRunning = 0;
    // Folder sizes survive navigation and restarts (~/.cache/irixfm/folder-sizes.json).
    struct SizeEntry {
        qint64 bytes = 0, files = 0, folders = 0;
        qint64 dirMtime = 0;   // the folder's own mtime when counted (changes when its entries change)
        qint64 counted = 0;    // when it was counted (ms since epoch)
    };
    QHash<QString, SizeEntry> m_sizeCache;
    bool m_sizeSavePending = false;
    int m_dockReserve = 0;
    class QFileSystemWatcher *m_panelWatcher = nullptr;
    QAtomicInt m_searchGen;     // bumped to cancel the running search
    QHash<QString, QStringList> m_tagIndex;   // path -> tags, for files fm has tagged
    // kind() and desktopInfo() are asked for every item a view creates (again on every scroll), and
    // kind() may have to read the file. Cached by path, valid while the file's ctime is the same
    // (ctime changes on any write, chmod or rename onto the path).
    struct KindEntry {
        qint64 ctime = 0;
        QString kind;
    };
    struct DesktopEntry {
        qint64 ctime = 0;
        QVariantMap info;
    };
    mutable QHash<QString, KindEntry> m_kindCache;
    mutable QHash<QString, DesktopEntry> m_desktopCache;
};
