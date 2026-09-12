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

namespace
{
// The Keybinds settings page shows one section per kglobalaccel component, so
// each group of actions below registers under its own component name.
const QString s_noctaliaComponent = QStringLiteral("noctalia");
const QString s_noctaliaDisplay = QStringLiteral("Noctalia");
const QString s_audioComponent = QStringLiteral("audio");
const QString s_audioDisplay = QStringLiteral("Audio");
const QString s_mediaUiComponent = QStringLiteral("media-ui");
const QString s_mediaUiDisplay = QStringLiteral("Media & UI");
} // namespace

NoctaliaSettings::NoctaliaSettings(QObject *parent)
    : QObject(parent)
{
    // Session & Shell
    registerShortcut(s_noctaliaComponent, s_noctaliaDisplay, QStringLiteral("NoctaliaReloadConfig"),
                     i18n("Reload Noctalia Config"),
                     {QStringLiteral("msg"), QStringLiteral("config-reload")});

    registerProcessAction(s_noctaliaComponent, s_noctaliaDisplay, QStringLiteral("NoctaliaRestartShell"),
                          i18n("Restart Noctalia Shell"),
                          {QStringLiteral("pkill"), QStringLiteral("-x"), QStringLiteral("noctalia-kwe")},
                          QKeySequence(Qt::META | Qt::ALT | Qt::Key_R));

    registerShortcut(s_noctaliaComponent, s_noctaliaDisplay, QStringLiteral("NoctaliaToggleSettings"),
                     i18n("Toggle Noctalia Settings"),
                     {QStringLiteral("msg"), QStringLiteral("settings-toggle")});

    registerShortcut(s_noctaliaComponent, s_noctaliaDisplay, QStringLiteral("NoctaliaWindowSwitcher"),
                     i18n("Open Noctalia Window Switcher"),
                     {QStringLiteral("msg"), QStringLiteral("window-switcher")},
                     QKeySequence(Qt::ALT | Qt::Key_Tab));

    registerShortcut(s_noctaliaComponent, s_noctaliaDisplay, QStringLiteral("NoctaliaSessionLock"),
                     i18n("Lock Session"),
                     {QStringLiteral("msg"), QStringLiteral("session"), QStringLiteral("lock")},
                     QKeySequence(Qt::META | Qt::Key_L));

    registerShortcut(s_noctaliaComponent, s_noctaliaDisplay, QStringLiteral("NoctaliaSessionLogout"),
                     i18n("Log Out"),
                     {QStringLiteral("msg"), QStringLiteral("session"), QStringLiteral("logout")});

    registerShortcut(s_noctaliaComponent, s_noctaliaDisplay, QStringLiteral("NoctaliaSessionSuspend"),
                     i18n("Suspend"),
                     {QStringLiteral("msg"), QStringLiteral("session"), QStringLiteral("suspend")});

    registerShortcut(s_noctaliaComponent, s_noctaliaDisplay, QStringLiteral("NoctaliaSessionReboot"),
                     i18n("Reboot"),
                     {QStringLiteral("msg"), QStringLiteral("session"), QStringLiteral("reboot")});

    registerShortcut(s_noctaliaComponent, s_noctaliaDisplay, QStringLiteral("NoctaliaSessionShutdown"),
                     i18n("Shut Down"),
                     {QStringLiteral("msg"), QStringLiteral("session"), QStringLiteral("shutdown")});

    // Surfaces - Panels
    registerShortcut(s_noctaliaComponent, s_noctaliaDisplay, QStringLiteral("NoctaliaToggleBar"),
                     i18n("Toggle Bar"),
                     {QStringLiteral("msg"), QStringLiteral("bar-toggle")});

    registerShortcut(s_noctaliaComponent, s_noctaliaDisplay, QStringLiteral("NoctaliaToggleLauncher"),
                     i18n("Toggle Launcher"),
                     {QStringLiteral("msg"), QStringLiteral("panel-toggle"), QStringLiteral("launcher")});

    registerShortcut(s_noctaliaComponent, s_noctaliaDisplay, QStringLiteral("NoctaliaToggleControlCenter"),
                     i18n("Toggle Control Center"),
                     {QStringLiteral("msg"), QStringLiteral("panel-toggle"), QStringLiteral("control-center")});

    registerShortcut(s_noctaliaComponent, s_noctaliaDisplay, QStringLiteral("NoctaliaToggleSessionMenu"),
                     i18n("Toggle Session Menu"),
                     {QStringLiteral("msg"), QStringLiteral("panel-toggle"), QStringLiteral("session")});

    registerShortcut(s_noctaliaComponent, s_noctaliaDisplay, QStringLiteral("NoctaliaToggleClipboard"),
                     i18n("Toggle Clipboard"),
                     {QStringLiteral("msg"), QStringLiteral("panel-toggle"), QStringLiteral("clipboard")});

    registerShortcut(s_noctaliaComponent, s_noctaliaDisplay, QStringLiteral("NoctaliaToggleWallpaperPicker"),
                     i18n("Toggle Wallpaper Picker"),
                     {QStringLiteral("msg"), QStringLiteral("panel-toggle"), QStringLiteral("wallpaper")});

    // Surfaces - Dock
    registerShortcut(s_noctaliaComponent, s_noctaliaDisplay, QStringLiteral("NoctaliaDockShow"),
                     i18n("Show Dock"),
                     {QStringLiteral("msg"), QStringLiteral("dock-show")});

    registerShortcut(s_noctaliaComponent, s_noctaliaDisplay, QStringLiteral("NoctaliaDockHide"),
                     i18n("Hide Dock"),
                     {QStringLiteral("msg"), QStringLiteral("dock-hide")});

    registerShortcut(s_noctaliaComponent, s_noctaliaDisplay, QStringLiteral("NoctaliaDockToggle"),
                     i18n("Toggle Dock"),
                     {QStringLiteral("msg"), QStringLiteral("dock-toggle")});

    registerShortcut(s_noctaliaComponent, s_noctaliaDisplay, QStringLiteral("NoctaliaDockReload"),
                     i18n("Reload Dock"),
                     {QStringLiteral("msg"), QStringLiteral("dock-reload")});

    // Wallpaper & Theme
    registerShortcut(s_noctaliaComponent, s_noctaliaDisplay, QStringLiteral("NoctaliaWallpaperRandom"),
                     i18n("Random Wallpaper"),
                     {QStringLiteral("msg"), QStringLiteral("wallpaper-random")});

    registerShortcut(s_noctaliaComponent, s_noctaliaDisplay, QStringLiteral("NoctaliaToggleThemeMode"),
                     i18n("Toggle Theme Mode"),
                     {QStringLiteral("msg"), QStringLiteral("theme-mode-toggle")});

    registerShortcut(s_noctaliaComponent, s_noctaliaDisplay, QStringLiteral("NoctaliaApplyTemplates"),
                     i18n("Apply Templates"),
                     {QStringLiteral("msg"), QStringLiteral("templates-apply")});

    registerAudioShortcuts();
    registerMediaUiShortcuts();
}

// Speaker and microphone volume, backed by the shell's PipeWire service. The
// standard hardware keys are bound by default so the volume keys work out of
// the box; the user can rebind or clear them like any other shortcut.
void NoctaliaSettings::registerAudioShortcuts()
{
    registerShortcut(s_audioComponent, s_audioDisplay, QStringLiteral("AudioVolumeUp"),
                     i18n("Volume Up"),
                     {QStringLiteral("msg"), QStringLiteral("volume-up")},
                     QKeySequence(Qt::Key_VolumeUp));

    registerShortcut(s_audioComponent, s_audioDisplay, QStringLiteral("AudioVolumeDown"),
                     i18n("Volume Down"),
                     {QStringLiteral("msg"), QStringLiteral("volume-down")},
                     QKeySequence(Qt::Key_VolumeDown));

    registerShortcut(s_audioComponent, s_audioDisplay, QStringLiteral("AudioVolumeMute"),
                     i18n("Mute"),
                     {QStringLiteral("msg"), QStringLiteral("volume-mute")},
                     QKeySequence(Qt::Key_VolumeMute));

    registerShortcut(s_audioComponent, s_audioDisplay, QStringLiteral("AudioMicMute"),
                     i18n("Mute Microphone"),
                     {QStringLiteral("msg"), QStringLiteral("mic-mute")},
                     QKeySequence(Qt::Key_MicMute));

    registerShortcut(s_audioComponent, s_audioDisplay, QStringLiteral("AudioMicVolumeUp"),
                     i18n("Microphone Volume Up"),
                     {QStringLiteral("msg"), QStringLiteral("mic-volume-up")},
                     QKeySequence(Qt::Key_MicVolumeUp));

    registerShortcut(s_audioComponent, s_audioDisplay, QStringLiteral("AudioMicVolumeDown"),
                     i18n("Microphone Volume Down"),
                     {QStringLiteral("msg"), QStringLiteral("mic-volume-down")},
                     QKeySequence(Qt::Key_MicVolumeDown));
}

void NoctaliaSettings::unregisterRemovedMediaUiShortcuts()
{
    // KineticWE used to surface Noctalia's region/fullscreen screenshot
    // commands under Media & UI. Screenshots belong to Spectacle now, so the
    // actions are no longer registered. The daemon re-creates them from the
    // [media-ui] group of an existing kglobalshortcutsrc at startup, so they
    // have to be purged explicitly. This runs after KGlobalAccelD::init() has
    // loaded that config: the compositor creates Input (and its
    // GlobalShortcutsManager) before the Workspace constructs NoctaliaSettings.
    // setShortcut() with empty chords under NoAutoloading marks the action as
    // known and keeps .desktop autoloading from resurrecting it;
    // removeAllShortcuts() then makes the daemon drop it and rewrite the group
    // without the stale keys.
    const QStringList removed{
        QStringLiteral("MediaUiScreenshotRegion"),
        QStringLiteral("MediaUiScreenshotFullscreen"),
    };
    for (const QString &actionName : removed) {
        QAction action;
        action.setObjectName(actionName);
        action.setProperty("componentName", s_mediaUiComponent);
        action.setProperty("componentDisplayName", s_mediaUiDisplay);
        KGlobalAccel::self()->setShortcut(&action, {QKeySequence()}, KGlobalAccel::NoAutoloading);
        KGlobalAccel::self()->removeAllShortcuts(&action);
    }
}

// MPRIS transport plus user-facing shell actions. The transport keys are bound
// by default; the notification, clipboard and wallpaper actions start unbound
// because there is no universal key for them.
void NoctaliaSettings::registerMediaUiShortcuts()
{
    unregisterRemovedMediaUiShortcuts();

    registerShortcut(s_mediaUiComponent, s_mediaUiDisplay, QStringLiteral("MediaUiPlayPause"),
                     i18n("Play / Pause"),
                     {QStringLiteral("msg"), QStringLiteral("media"), QStringLiteral("toggle")},
                     QKeySequence(Qt::Key_MediaPlay));

    registerShortcut(s_mediaUiComponent, s_mediaUiDisplay, QStringLiteral("MediaUiPlay"),
                     i18n("Play"),
                     {QStringLiteral("msg"), QStringLiteral("media"), QStringLiteral("play")});

    registerShortcut(s_mediaUiComponent, s_mediaUiDisplay, QStringLiteral("MediaUiPause"),
                     i18n("Pause"),
                     {QStringLiteral("msg"), QStringLiteral("media"), QStringLiteral("pause")});

    registerShortcut(s_mediaUiComponent, s_mediaUiDisplay, QStringLiteral("MediaUiStop"),
                     i18n("Stop"),
                     {QStringLiteral("msg"), QStringLiteral("media"), QStringLiteral("stop")},
                     QKeySequence(Qt::Key_MediaStop));

    registerShortcut(s_mediaUiComponent, s_mediaUiDisplay, QStringLiteral("MediaUiNextTrack"),
                     i18n("Next Track"),
                     {QStringLiteral("msg"), QStringLiteral("media"), QStringLiteral("next")},
                     QKeySequence(Qt::Key_MediaNext));

    registerShortcut(s_mediaUiComponent, s_mediaUiDisplay, QStringLiteral("MediaUiPreviousTrack"),
                     i18n("Previous Track"),
                     {QStringLiteral("msg"), QStringLiteral("media"), QStringLiteral("previous")},
                     QKeySequence(Qt::Key_MediaPrevious));

    registerShortcut(s_mediaUiComponent, s_mediaUiDisplay, QStringLiteral("MediaUiNextPlayer"),
                     i18n("Next Player"),
                     {QStringLiteral("msg"), QStringLiteral("media"), QStringLiteral("next-player")});

    registerShortcut(s_mediaUiComponent, s_mediaUiDisplay, QStringLiteral("MediaUiPreviousPlayer"),
                     i18n("Previous Player"),
                     {QStringLiteral("msg"), QStringLiteral("media"), QStringLiteral("previous-player")});

    registerShortcut(s_mediaUiComponent, s_mediaUiDisplay, QStringLiteral("MediaUiToggleDnd"),
                     i18n("Toggle Do Not Disturb"),
                     {QStringLiteral("msg"), QStringLiteral("notification-dnd-toggle")});

    registerShortcut(s_mediaUiComponent, s_mediaUiDisplay, QStringLiteral("MediaUiClearNotifications"),
                     i18n("Clear Notifications"),
                     {QStringLiteral("msg"), QStringLiteral("notification-clear-active")});

    registerShortcut(s_mediaUiComponent, s_mediaUiDisplay, QStringLiteral("MediaUiClearNotificationHistory"),
                     i18n("Clear Notification History"),
                     {QStringLiteral("msg"), QStringLiteral("notification-clear-history")});

    registerShortcut(s_mediaUiComponent, s_mediaUiDisplay, QStringLiteral("MediaUiClearClipboard"),
                     i18n("Clear Clipboard History"),
                     {QStringLiteral("msg"), QStringLiteral("clipboard-clear")});

    registerShortcut(s_mediaUiComponent, s_mediaUiDisplay, QStringLiteral("MediaUiAnnotate"),
                     i18n("Annotate Screen"),
                     {QStringLiteral("msg"), QStringLiteral("annotate")});

    registerShortcut(s_mediaUiComponent, s_mediaUiDisplay, QStringLiteral("MediaUiNextWallpaper"),
                     i18n("Next Wallpaper"),
                     {QStringLiteral("msg"), QStringLiteral("wallpaper-next")});

    registerShortcut(s_mediaUiComponent, s_mediaUiDisplay, QStringLiteral("MediaUiPreviousWallpaper"),
                     i18n("Previous Wallpaper"),
                     {QStringLiteral("msg"), QStringLiteral("wallpaper-previous")});
}

NoctaliaSettings::~NoctaliaSettings() = default;

void NoctaliaSettings::registerShortcut(const QString &component, const QString &componentDisplayName, const QString &id, const QString &description, const QStringList &args, const QKeySequence &defaultShortcut)
{
    QAction *action = new QAction(this);
    action->setProperty("componentName", component);
    action->setProperty("componentDisplayName", componentDisplayName);
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

void NoctaliaSettings::registerProcessAction(const QString &component, const QString &componentDisplayName, const QString &id, const QString &description, const QStringList &command, const QKeySequence &defaultShortcut)
{
    QAction *action = new QAction(this);
    action->setProperty("componentName", component);
    action->setProperty("componentDisplayName", componentDisplayName);
    action->setObjectName(id);
    action->setText(description);

    QList<QKeySequence> shortcuts;
    if (!defaultShortcut.isEmpty()) {
        shortcuts.append(defaultShortcut);
    }
    KGlobalAccel::setGlobalShortcut(action, shortcuts);

    connect(action, &QAction::triggered, this, [command]() {
        if (command.isEmpty()) {
            return;
        }
        const QString program = command.first();
        const QStringList arguments = command.mid(1);
        QProcess::startDetached(program, arguments);
    });

    m_actions.append(action);
}

} // namespace KWin
