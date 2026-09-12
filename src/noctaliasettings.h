/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2025 The KineticWE Contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <QObject>
#include <QKeySequence>

class QAction;

namespace KWin
{

class NoctaliaSettings : public QObject
{
    Q_OBJECT

public:
    explicit NoctaliaSettings(QObject *parent = nullptr);
    ~NoctaliaSettings() override;

private:
    void registerShortcut(const QString &component, const QString &componentDisplayName, const QString &id, const QString &description, const QStringList &args, const QKeySequence &defaultShortcut = QKeySequence());
    void registerProcessAction(const QString &component, const QString &componentDisplayName, const QString &id, const QString &description, const QStringList &command, const QKeySequence &defaultShortcut = QKeySequence());
    // Audio (volume / microphone) and Media & UI (playback transport plus
    // notification, clipboard and wallpaper commands) grouped as their own
    // Keybinds sections, each wired to the shell's `noctalia-kwe msg` IPC
    // commands.
    void registerAudioShortcuts();
    // Drops the region/fullscreen screenshot actions an older KineticWE
    // registered under Media & UI (screenshots now belong to Spectacle). The
    // daemon reloads them from kglobalshortcutsrc, so they must be removed, not
    // merely left unregistered. Called from registerMediaUiShortcuts().
    void unregisterRemovedMediaUiShortcuts();
    void registerMediaUiShortcuts();
    // Monitor brightness and keyboard backlight, backed by the shell's
    // brightness service. Standard laptop function keys are bound by default.
    void registerBrightnessShortcuts();
    QList<QAction *> m_actions;
};

} // namespace KWin
