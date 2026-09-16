/*
 * SPDX-FileCopyrightText: 2016 Red Hat Inc
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 *
 * SPDX-FileCopyrightText: 2016 Jan Grulich <jgrulich@redhat.com>
 */

#include <QApplication>
#include <QCommandLineParser>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QIcon>
#include <QQuickStyle>

#include <KAboutData>
#include <KColorScheme>
#include <KConfigGroup>
#include <KCrash>
#include <KLocalizedString>
#include <KSharedConfig>

#include "../version.h"
#include "debug.h"
#include "desktopportal.h"

#include <signal.h>

using namespace Qt::StringLiterals;

int main(int argc, char *argv[])
{
    // Do not try to reconnect to the compositor as KWayland can't handle that
    // Reconsider enabling when we do not use KWayland anymore
    qunsetenv("QT_WAYLAND_RECONNECT");
    // Never load the session's Qt platform theme in the portal backend. With
    // qt5ct/qt6ct's standard_dialogs=xdgdesktopportal the theme plugin calls
    // org.freedesktop.portal.Settings.ReadAll and queries the FileChooser
    // version while QApplication is being constructed -- i.e. straight back
    // into the xdg-desktop-portal frontend that is still activating this very
    // process. That deadlocks the frontend until systemd's 15s TimeoutStartSec
    // fires, so the frontend is killed and restarted on every login, delaying
    // the whole session. The portal is a system service; it must not depend on
    // the desktop portal theme.
    qunsetenv("QT_QPA_PLATFORMTHEME");
    QCoreApplication::setAttribute(Qt::AA_DisableSessionManager);
    QApplication a(argc, argv);
    a.setQuitOnLastWindowClosed(false);
    a.setQuitLockEnabled(false);

    // Because the platform theme above is deliberately not loaded, reproduce
    // the three things it would otherwise provide, or the portal's own
    // Kirigami/QML dialogs (screen-share chooser, file chooser, ...) fall back
    // to Qt's stock light palette, Kirigami's hard-coded Basic theme and
    // KIconLoader's light "breeze" fallback. All are plain local setup -- no
    // portal round-trips -- so the deadlock the unset above avoids cannot come
    // back.
    a.setPalette(KColorScheme::createApplicationPalette(KSharedConfig::openConfig(u"kdeglobals"_s)));
    // Kirigami only loads its palette-driven theme (styles/org.kde.desktop)
    // when the desktop QQuick Controls style is active, which nothing selects
    // on a session that does not run the KDE platform theme.
    QQuickStyle::setStyle(u"org.kde.desktop"_s);
    // ...and the same platform theme normally publishes the icon theme. With
    // QIcon::themeName() left empty, KIconLoader falls back to "breeze" (the
    // light variant), so the KIO file chooser's toolbar icons come out dark on
    // the now-dark background. Read the user's choice from kdeglobals, the
    // source KDE apps and the colour scheme above already use.
    const KConfigGroup iconGroup(KSharedConfig::openConfig(u"kdeglobals"_s), u"Icons"_s);
    const QString iconTheme = iconGroup.readEntry("Theme", QString());
    if (!iconTheme.isEmpty()) {
        QIcon::setThemeName(iconTheme);
    }

    // Guard against an app closing the reading end when the clipboard portal writes current clipboard contents
    signal(SIGPIPE, SIG_IGN);

    QCommandLineParser parser;
    QCommandLineOption replaceOption(u"replace"_s, u"Replace running instance"_s);
    parser.addOption(replaceOption);

    KAboutData about(QStringLiteral("xdg-desktop-portal-kwe"), QString(), QStringLiteral(XDPK_VERSION_STRING));
    about.setDesktopFileName(QStringLiteral("org.freedesktop.impl.portal.desktop.kwe"));
    about.setupCommandLine(&parser);
    KAboutData::setApplicationData(about);

    parser.process(a);
    about.processCommandLine(&parser);

    KCrash::initialize();

    const auto dbusQueueOption = parser.isSet(replaceOption) ? QDBusConnectionInterface::ReplaceExistingService : QDBusConnectionInterface::DontQueueService;

    QDBusConnection sessionBus = QDBusConnection::sessionBus();
    if (sessionBus.interface()->registerService(u"org.freedesktop.impl.portal.desktop.kwe"_s, dbusQueueOption, QDBusConnectionInterface::AllowReplacement)) {
        DesktopPortal *desktopPortal = new DesktopPortal(&a);
        if (sessionBus.registerObject(QStringLiteral("/org/freedesktop/portal/desktop"), desktopPortal, QDBusConnection::ExportAdaptors)) {
            qCDebug(XdgDesktopPortalKde) << "Desktop portal registered successfully";
        } else {
            qCDebug(XdgDesktopPortalKde) << "Failed to register desktop portal";
        }
    } else {
        qCDebug(XdgDesktopPortalKde) << "Failed to register org.freedesktop.impl.portal.desktop.kwe service";
        return 1;
    }

    return a.exec();
}
