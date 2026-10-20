// fm: IRIX-style file manager themed like macOS Tahoe. C++ side: window effects and KIO file actions.
#pragma once

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
    Q_PROPERTY(QString home READ home CONSTANT)
    Q_PROPERTY(QString userName READ userName CONSTANT)
    Q_PROPERTY(QString assetDir MEMBER m_assetDir CONSTANT)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY undoChanged)
    Q_PROPERTY(QString undoText READ undoText NOTIFY undoChanged)

public:
    Fm(const QString &startDir, const QString &assetDir, QObject *parent = nullptr);

    QString home() const;
    QString userName() const;
    bool canUndo() const;
    QString undoText() const;

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
    Q_INVOKABLE void newWindow(const QString &dir);
    Q_INVOKABLE void overview();

    Q_INVOKABLE void trash(const QStringList &paths);
    Q_INVOKABLE void copyToClipboard(const QStringList &paths);
    Q_INVOKABLE void copyPathText(const QString &text);
    Q_INVOKABLE bool canPaste() const;
    Q_INVOKABLE void paste(const QString &dir);
    Q_INVOKABLE void drop(const QList<QUrl> &urls, const QString &dir, bool move);
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
    Q_INVOKABLE void folderSize(const QString &path);               // answers with folderSizeReady
    Q_INVOKABLE QString textPreview(const QString &path, int maxBytes = 262144) const;  // "" for binary files

Q_SIGNALS:
    void undoChanged();
    void folderSizeReady(const QString &path, qint64 bytes, qint64 files, qint64 folders);

private:
    static QString freeName(const QString &dir, const QString &base, const QString &suffix);
    QString m_startDir;
    QString m_assetDir;
    QPointer<KWindowShadow> m_shadow;
};
