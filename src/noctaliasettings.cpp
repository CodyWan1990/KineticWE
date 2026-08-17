/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2025 The KineticWE Contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "noctaliasettings.h"

#include <KGlobalAccel>
#include <KLocalizedString>

#include <QAction>
#include <QKeySequence>
#include <QProcess>

namespace KWin
{

NoctaliaSettings::NoctaliaSettings(QObject *parent)
    : QObject(parent)
{
    // Session & Shell
    registerShortcut(QStringLiteral("NoctaliaReloadConfig"),
                     i18n("Reload Noctalia Config"),
                     {QStringLiteral("msg"), QStringLiteral("config-reload")});

    registerShortcut(QStringLiteral("NoctaliaToggleSettings"),
                     i18n("Toggle Noctalia Settings"),
                     {QStringLiteral("msg"), QStringLiteral("settings-toggle")});

    registerShortcut(QStringLiteral("NoctaliaWindowSwitcher"),
                     i18n("Open Noctalia Window Switcher"),
                     {QStringLiteral("msg"), QStringLiteral("window-switcher")},
                     QKeySequence(Qt::ALT | Qt::Key_Tab));

    registerShortcut(QStringLiteral("NoctaliaSessionLock"),
                     i18n("Lock Session"),
                     {QStringLiteral("msg"), QStringLiteral("session"), QStringLiteral("lock")});

    registerShortcut(QStringLiteral("NoctaliaSessionLogout"),
                     i18n("Log Out"),
                     {QStringLiteral("msg"), QStringLiteral("session"), QStringLiteral("logout")});

    registerShortcut(QStringLiteral("NoctaliaSessionSuspend"),
                     i18n("Suspend"),
                     {QStringLiteral("msg"), QStringLiteral("session"), QStringLiteral("suspend")});

    registerShortcut(QStringLiteral("NoctaliaSessionReboot"),
                     i18n("Reboot"),
                     {QStringLiteral("msg"), QStringLiteral("session"), QStringLiteral("reboot")});

    registerShortcut(QStringLiteral("NoctaliaSessionShutdown"),
                     i18n("Shut Down"),
                     {QStringLiteral("msg"), QStringLiteral("session"), QStringLiteral("shutdown")});

    // Surfaces - Panels
    registerShortcut(QStringLiteral("NoctaliaToggleBar"),
                     i18n("Toggle Bar"),
                     {QStringLiteral("msg"), QStringLiteral("bar-toggle")});

    registerShortcut(QStringLiteral("NoctaliaToggleLauncher"),
                     i18n("Toggle Launcher"),
                     {QStringLiteral("msg"), QStringLiteral("panel-toggle"), QStringLiteral("launcher")});

    registerShortcut(QStringLiteral("NoctaliaToggleControlCenter"),
                     i18n("Toggle Control Center"),
                     {QStringLiteral("msg"), QStringLiteral("panel-toggle"), QStringLiteral("control-center")});

    registerShortcut(QStringLiteral("NoctaliaToggleSessionMenu"),
                     i18n("Toggle Session Menu"),
                     {QStringLiteral("msg"), QStringLiteral("panel-toggle"), QStringLiteral("session")});

    registerShortcut(QStringLiteral("NoctaliaToggleClipboard"),
                     i18n("Toggle Clipboard"),
                     {QStringLiteral("msg"), QStringLiteral("panel-toggle"), QStringLiteral("clipboard")});

    registerShortcut(QStringLiteral("NoctaliaToggleWallpaperPicker"),
                     i18n("Toggle Wallpaper Picker"),
                     {QStringLiteral("msg"), QStringLiteral("panel-toggle"), QStringLiteral("wallpaper")});

    // Surfaces - Dock
    registerShortcut(QStringLiteral("NoctaliaDockShow"),
                     i18n("Show Dock"),
                     {QStringLiteral("msg"), QStringLiteral("dock-show")});

    registerShortcut(QStringLiteral("NoctaliaDockHide"),
                     i18n("Hide Dock"),
                     {QStringLiteral("msg"), QStringLiteral("dock-hide")});

    registerShortcut(QStringLiteral("NoctaliaDockToggle"),
                     i18n("Toggle Dock"),
                     {QStringLiteral("msg"), QStringLiteral("dock-toggle")});

    registerShortcut(QStringLiteral("NoctaliaDockReload"),
                     i18n("Reload Dock"),
                     {QStringLiteral("msg"), QStringLiteral("dock-reload")});

    // Wallpaper & Theme
    registerShortcut(QStringLiteral("NoctaliaWallpaperRandom"),
                     i18n("Random Wallpaper"),
                     {QStringLiteral("msg"), QStringLiteral("wallpaper-random")});

    registerShortcut(QStringLiteral("NoctaliaToggleThemeMode"),
                     i18n("Toggle Theme Mode"),
                     {QStringLiteral("msg"), QStringLiteral("theme-mode-toggle")});

    registerShortcut(QStringLiteral("NoctaliaApplyTemplates"),
                     i18n("Apply Templates"),
                     {QStringLiteral("msg"), QStringLiteral("templates-apply")});
}

NoctaliaSettings::~NoctaliaSettings() = default;

void NoctaliaSettings::registerShortcut(const QString &id, const QString &description, const QStringList &args, const QKeySequence &defaultShortcut)
{
    QAction *action = new QAction(this);
    action->setProperty("componentName", QStringLiteral("noctalia"));
    action->setProperty("componentDisplayName", QStringLiteral("Noctalia"));
    action->setObjectName(id);
    action->setText(description);

    QList<QKeySequence> shortcuts;
    if (!defaultShortcut.isEmpty()) {
        shortcuts.append(defaultShortcut);
    }
    KGlobalAccel::setGlobalShortcut(action, shortcuts);

    connect(action, &QAction::triggered, this, [args]() {
        QProcess::startDetached(QStringLiteral("noctalia-kwe"), args);
    });

    m_actions.append(action);
}

} // namespace KWin
