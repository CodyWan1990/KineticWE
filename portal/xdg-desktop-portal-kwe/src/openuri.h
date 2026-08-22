/*
 * SPDX-FileCopyrightText: 2026 KineticWE contributors
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#ifndef XDG_DESKTOP_PORTAL_KDE_OPENURI_H
#define XDG_DESKTOP_PORTAL_KDE_OPENURI_H

#include <QDBusAbstractAdaptor>
#include <QDBusObjectPath>
#include <QDBusUnixFileDescriptor>

class QDBusMessage;

// org.freedesktop.impl.portal.OpenURI: opens a URI, a local file, or a
// directory using the user's default handlers (KIO / mimeapps resolution).
//
// Unlike the interactive portals this returns synchronously: each method emits
// (o handle, s uri) back to the frontend immediately after launching the open.
// The handle mirrors the request handle the frontend passed in; the uri is the
// resolved local path for file/directory opens (unchanged for remote URIs).
class OpenURIPortal : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.impl.portal.OpenURI")
public:
    explicit OpenURIPortal(QObject *parent);

public Q_SLOTS:
    void OpenURI(const QDBusObjectPath &handle,
                 const QString &app_id,
                 const QString &parent_window,
                 const QString &uri,
                 const QVariantMap &options,
                 QDBusObjectPath &replyHandle,
                 QString &replyUri);

    void OpenFile(const QDBusObjectPath &handle,
                  const QString &app_id,
                  const QString &parent_window,
                  const QDBusUnixFileDescriptor &fd,
                  const QVariantMap &options,
                  QDBusObjectPath &replyHandle,
                  QString &replyUri);

    void OpenDirectory(const QDBusObjectPath &handle,
                       const QString &app_id,
                       const QString &parent_window,
                       const QString &directory,
                       const QVariantMap &options,
                       QDBusObjectPath &replyHandle,
                       QString &replyUri);
};

#endif // XDG_DESKTOP_PORTAL_KDE_OPENURI_H