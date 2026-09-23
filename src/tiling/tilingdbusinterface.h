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
     * "AutoGrid", "Columns", "Emerge") in effect for the active output's
     * current desktop, or an empty string when native tiling is disabled.
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

    /**
     * Flip the active output's outer (left/right) gaps between the
     * [Tiling] GapToggleWide / GapToggleCentered presets and apply them
     * live (same action as the "Tiling Toggle Outer Gaps" shortcut).
     */
    Q_SCRIPTABLE void toggleOuterGaps();

    /**
     * Toggle float-only tiling for the active output's current desktop (same
     * action as the "Tiling Toggle Float Only Workspace" shortcut). Enabling
     * it floats every tiled window on that workspace; disabling it re-tiles
     * the windows it floated.
     */
    Q_SCRIPTABLE void toggleFloatOnly();

    /**
     * Column indices of the active output's current-desktop Emerge strip that
     * sit fully off the leading (left/up) edge of the viewport, ordered from
     * the farthest column to the nearest. Empty when the active layout is not
     * Emerge or the strip is empty.
     */
    Q_SCRIPTABLE QStringList emergeColumnsLeft() const;

    /**
     * As emergeColumnsLeft(), for the columns fully off the trailing
     * (right/down) edge, ordered from the nearest column to the farthest.
     */
    Q_SCRIPTABLE QStringList emergeColumnsRight() const;

    /**
     * Scroll the active output's Emerge strip to the column at @p index and
     * focus its window. No-op when the active layout is not Emerge or the
     * index is out of range.
     */
    Q_SCRIPTABLE void focusEmergeColumn(int index);

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

    /**
     * Emitted whenever the active output's current-desktop Emerge strip
     * reflows (window opened/closed, focus or view scroll). Consumers should
     * re-query emergeColumnsLeft()/emergeColumnsRight() rather than assume the
     * change applies to the output they last queried.
     */
    void emergeViewportChanged();

private:
    TilingController *m_controller;
};

} // namespace KWin
