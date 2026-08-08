/*
    KWin - the KDE window manager
    SPDX-FileCopyrightText: 2026 KWin Tiling Fork
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

namespace KWin
{

class TilingController;

/**
 * D-Bus bridge for the native tiling state.
 *
 * Registered on the session bus at /Tiling (interface org.kde.KWin.Tiling)
 * so external consumers — the Noctalia bar plugin, scripts, qdbus — can
 * read the live layout of the active output's current desktop, list the
 * layouts enabled in kwinrc, and request layout switches. Runtime layout
 * switches are kept in memory only, so without this interface they are
 * not observable outside the compositor.
 */
class TilingDBusInterface : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.KWin.Tiling")

public:
    explicit TilingDBusInterface(TilingController *controller, QObject *parent = nullptr);

public Q_SLOTS:
    /**
     * Layout kind string ("MasterStack", "Stacked", "CenterTile", "Monocle",
     * "AutoGrid", "Columns") in effect for the active output's current
     * desktop, or an empty string when native tiling is disabled.
     */
    Q_SCRIPTABLE QString currentLayout() const;

    /**
     * Human-readable display name of currentLayout() (e.g. "Master & Stack"),
     * or an empty string when native tiling is disabled.
     */
    Q_SCRIPTABLE QString currentLayoutDisplay() const;

    /**
     * Kind strings of the layouts enabled in kwinrc [Tiling] EnabledLayouts,
     * in cycle order.
     */
    Q_SCRIPTABLE QStringList enabledLayouts() const;

    /**
     * Switch the active output's current desktop to the named layout kind.
     * Unknown kind strings are ignored.
     */
    Q_SCRIPTABLE void setLayout(const QString &kind);

    /**
     * Cycle the active output's current desktop through the enabled layouts.
     */
    Q_SCRIPTABLE void cycleLayout();

Q_SIGNALS:
    /**
     * Emitted whenever any (output, desktop) layout engine changes kind,
     * including reconcile passes after a kwinrc edit and tiling being
     * toggled on/off. Consumers should re-query currentLayout() rather
     * than assume the change applies to the active output.
     */
    void layoutChanged(const QString &kind);

    /**
     * Emitted when the enabled-layouts whitelist changes in kwinrc.
     */
    void enabledLayoutsChanged(const QStringList &kinds);

private:
    TilingController *m_controller;
};

} // namespace KWin
