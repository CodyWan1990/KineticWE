/*
 * SPDX-FileCopyrightText: 2026 KineticWE contributors
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "openuri.h"
#include "openuri_debug.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QFileInfo>
#include <QUrl>

#include <KIO/OpenUrlJob>

using namespace Qt::StringLiterals;

OpenURIPortal::OpenURIPortal(QObject *parent)
    : QDBusAbstractAdaptor(parent)
{
}

void OpenURIPortal::OpenURI(const QDBusObjectPath &handle,
                            const QString &app_id,
                            const QString &parent_window,
                            const QString &uri,
                            const QVariantMap &options,
                            QDBusObjectPath &replyHandle,
                            QString &replyUri)
{
    qCDebug(XdgDesktopPortalKdeOpenURI) << "OpenURI called with parameters:";
    qCDebug(XdgDesktopPortalKdeOpenURI) << "    handle: " << handle.path();
    qCDebug(XdgDesktopPortalKdeOpenURI) << "    app_id: " << app_id;
    qCDebug(XdgDesktopPortalKdeOpenURI) << "    parent_window: " << parent_window;
    qCDebug(XdgDesktopPortalKdeOpenURI) << "    uri: " << uri;
    qCDebug(XdgDesktopPortalKdeOpenURI) << "    options: " << options;

    replyHandle = handle;
    replyUri = uri;

    auto job = new KIO::OpenUrlJob(QUrl(uri));
    job->setStartupId(options.value(u"activation_token"_s).value<QByteArray>());
    job->start();
}

void OpenURIPortal::OpenFile(const QDBusObjectPath &handle,
                             const QString &app_id,
                             const QString &parent_window,
                             const QDBusUnixFileDescriptor &fd,
                             const QVariantMap &options,
                             QDBusObjectPath &replyHandle,
                             QString &replyUri)
{
    qCDebug(XdgDesktopPortalKdeOpenURI) << "OpenFile called with parameters:";
    qCDebug(XdgDesktopPortalKdeOpenURI) << "    handle: " << handle.path();
    qCDebug(XdgDesktopPortalKdeOpenURI) << "    app_id: " << app_id;
    qCDebug(XdgDesktopPortalKdeOpenURI) << "    parent_window: " << parent_window;

    replyHandle = handle;
    replyUri.clear();

    if (!fd.isValid()) {
        return;
    }

    // Resolve the file/directory path from the fd so the default handler can
    // be chosen. The fd is a dup of the caller's open descriptor.
    const QString path = QFileInfo(QStringLiteral("/proc/self/fd/%1").arg(fd.fileDescriptor())).symLinkTarget();
    if (path.isEmpty()) {
        return;
    }
    replyUri = QUrl::fromLocalFile(path).toString();

    auto job = new KIO::OpenUrlJob(QUrl::fromLocalFile(path));
    job->setStartupId(options.value(u"activation_token"_s).value<QByteArray>());
    job->start();
}

void OpenURIPortal::OpenDirectory(const QDBusObjectPath &handle,
                                  const QString &app_id,
                                  const QString &parent_window,
                                  const QString &directory,
                                  const QVariantMap &options,
                                  QDBusObjectPath &replyHandle,
                                  QString &replyUri)
{
    qCDebug(XdgDesktopPortalKdeOpenURI) << "OpenDirectory called with parameters:";
    qCDebug(XdgDesktopPortalKdeOpenURI) << "    handle: " << handle.path();
    qCDebug(XdgDesktopPortalKdeOpenURI) << "    app_id: " << app_id;
    qCDebug(XdgDesktopPortalKdeOpenURI) << "    parent_window: " << parent_window;
    qCDebug(XdgDesktopPortalKdeOpenURI) << "    directory: " << directory;

    replyHandle = handle;
    replyUri = QUrl::fromLocalFile(directory).toString();

    auto job = new KIO::OpenUrlJob(QUrl::fromLocalFile(directory));
    job->setStartupId(options.value(u"activation_token"_s).value<QByteArray>());
    job->start();
}

#include "moc_openuri.cpp"