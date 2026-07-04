/*
    KWin - the KDE window manager
    SPDX-FileCopyrightText: 2026 KWin Tiling Fork
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "layoutengine.h"
#include "tile.h"

#include <QList>
#include <QPointer>

namespace KWin
{

class CustomTile;
class RootTile;
class Window;

/**
 * Scrolling (viewport-based) layout.
 *
 * Windows are arranged in columns on an infinite horizontal strip, similar to
 * the Niri Wayland compositor. Each window occupies its own column (initially
 * one window per column). The viewport scrolls to follow the focused window
 * and snaps it to the left edge of the screen.
 *
 * Layout behaviour by window count:
 *   1 window:   Fills 100% of the tiling area.
 *   2 windows:  Each gets 50% width (split evenly).
 *   3+ windows: New windows open to the RIGHT of the focused window at 50%
 *               width. Existing windows keep their current widths. The total
 *               virtual strip width = sum of all column widths, scrollable.
 *
 * Windows extend beyond the visible area and are partially off-screen. The
 * compositor clips rendering at the output boundary, so only the visible
 * portion of each window is drawn.
 *
 * Focus left/right moves between columns and scrolls the viewport to snap
 * the newly focused column to the left edge. Up/down is a no-op until
 * vertical stacking within a column is implemented.
 *
 * Per-monitor isolation is handled automatically because each TileManager
 * is owned by a single LogicalOutput.
 */
class KWIN_EXPORT ScrollingLayoutEngine : public LayoutEngine
{
    Q_OBJECT

public:
    explicit ScrollingLayoutEngine(QObject *parent = nullptr);
    ~ScrollingLayoutEngine() override;

    LayoutKind layoutKind() const override { return LayoutKind::Scrolling; }
    void attach(RootTile *root) override;
    void addWindow(Window *window) override;
    void removeWindow(Window *window) override;
    void moveWindow(Window *window, int delta) override;
    void beginMoveWindow(Window *window) override;
    bool endMoveWindow(Window *window, Window *target) override;
    void cancelMoveWindow(Window *window) override;
    void reflow() override;
    void pruneEmptyLeaves() override;

    QList<Window *> windows() const override;
    Window *primaryWindow() const override;
    Window *windowInDirection(Window *from, FocusDirection direction) const override;

    // --- Scrolling / viewport support ---
    bool supportsViewport() const override { return true; }
    void setViewportOffset(qreal offset) override { m_viewportOffset = offset; reflow(); }
    qreal viewportOffset() const override { return m_viewportOffset; }

    void focusColumnLeft() override;
    void focusColumnRight() override;
    void moveColumnLeft() override;
    void moveColumnRight() override;

    // --- Column width resize ---
    bool supportsPerTileResize() const override { return true; }
    void adjustTileSize(Window *window, qreal weightDelta, Qt::Orientation axis) override;
    void adjustColumnRatio(qreal delta) override { Q_UNUSED(delta) }
    void interactiveResizeEnded(Window *window, const RectF &before, const RectF &after,
                                Qt::Edge edge, const QSizeF &outputSize) override;

    /**
     * Default column width as a fraction of the output width.
     * 0.5 = 50% of screen width per column.
     */
    qreal defaultColumnWidth() const { return m_defaultColumnWidth; }
    void setDefaultColumnWidth(qreal width);

Q_SIGNALS:
    void activeColumnChanged(int index);
    void viewportOffsetChanged(qreal offset);

private:
    int indexOfWindow(Window *window) const;

    /**
     * Calculate the cumulative X position (in relative units) of a column
     * at the given index, before applying the viewport offset.
     */
    qreal columnX(int index) const;

    /**
     * Snap the viewport offset so the column at @p index is positioned
     * at the left edge of the screen.
     */
    void snapViewportToColumn(int index);

    /**
     * Recalculate the viewport offset to bring the active column into view.
     */
    void updateViewport();

    QPointer<RootTile> m_root;

    // One leaf tile per window.
    QList<QPointer<CustomTile>> m_leaves;

    // Column widths in relative units (fractions of output width).
    // Parallel to m_leaves.
    QList<qreal> m_columnWidths;

    // Viewport scroll offset in relative units.
    // A positive offset scrolls the strip to the left (revealing content to the right).
    qreal m_viewportOffset = 0.0;

    // Index of the currently active / focused column.
    int m_activeColumnIdx = 0;

    // Default column width in relative units (fraction of output width).
    // Applied to new windows when there are 3+ windows and no custom resize
    // has been performed.
    qreal m_defaultColumnWidth = 0.5;

    // The width used for the first window (fills the screen).
    static constexpr qreal m_firstWindowWidth = 1.0;

    // Minimum / maximum column width as a fraction of output width.
    static constexpr qreal m_minColumnWidth = 0.15;
    static constexpr qreal m_maxColumnWidth = 1.0;

    // Source leaf remembered during an interactive drag-move.
    QPointer<CustomTile> m_moveSourceLeaf;
    int m_moveSourceIndex = -1;
};

} // namespace KWin
