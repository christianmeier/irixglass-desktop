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

public:
    Fm(const QString &startDir, const QString &assetDir, QObject *parent = nullptr);

    QString home() const;
    QString userName() const;

    // Blur behind + input mask in the window's rounded shape, plus a compositor-drawn shadow.
    Q_INVOKABLE void updateWindow(QQuickWindow *window, int radius, bool shadow);

    Q_INVOKABLE QString kind(const QString &path) const;  // folder | app | image | exe | doc
    Q_INVOKABLE QVariantMap desktopInfo(const QString &path) const;  // {name, icon}
    Q_INVOKABLE QString freeSpace(const QString &path) const;
    Q_INVOKABLE QString humanSize(qint64 bytes) const;
    Q_INVOKABLE QString uriList(const QStringList &paths) const;

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

private:
    QString m_startDir;
    QString m_assetDir;
    QPointer<KWindowShadow> m_shadow;
};
