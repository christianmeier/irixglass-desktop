#pragma once

#include <QDBusContext>
#include <QDBusError>
#include <QObject>
#include <QStringList>
#include <functional>

// org.freedesktop.FileManager1 (https://www.freedesktop.org/wiki/Specifications/file-manager-interface/):
// "Show in folder" / "Open containing folder" in Firefox, Chromium, KDE apps, VS Code… Exported by
// the running fm at /org/freedesktop/FileManager1; main.cpp sets the handlers, which open the
// windows. A handler returns false when there was nothing local to show (the caller gets an error).
class FileManager1 : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.FileManager1")
public:
    using Handler = std::function<bool(const QStringList &uris, const QString &startupId)>;
    Handler showFolders, showItems, showItemProperties;

public Q_SLOTS:
    Q_SCRIPTABLE void ShowFolders(const QStringList &uris, const QString &startupId) { run(showFolders, uris, startupId); }
    Q_SCRIPTABLE void ShowItems(const QStringList &uris, const QString &startupId) { run(showItems, uris, startupId); }
    Q_SCRIPTABLE void ShowItemProperties(const QStringList &uris, const QString &startupId) { run(showItemProperties, uris, startupId); }

private:
    void run(const Handler &handler, const QStringList &uris, const QString &startupId)
    {
        if ((!handler || !handler(uris, startupId)) && calledFromDBus())
            sendErrorReply(QDBusError::InvalidArgs, QStringLiteral("fm found no local file or folder to show in: ") + uris.join(QStringLiteral(", ")));
    }
};
