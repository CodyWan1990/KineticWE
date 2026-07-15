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
 * Columns layout.
 *
 * Windows are arranged in equal-width columns. The number of columns grows
 * from 1 to a maximum of 3 as windows are added, then additional windows
 * stack vertically within the focused window's column.
 *
 *   - 1 window  → 1 column, full width
 *   - 2 windows → 2 columns, 50/50
 *   - 3 windows → 3 columns, 33/33/33
 *   - 4+ windows → max 3 columns; new windows stack in the focused window's
 *     column (or the column with the fewest windows if no tiled window is
 *     focused).
 *
 * Navigation: Up/Down within a column, Left/Right between columns at the
 * same row (clamped to the last row of the target column).
 */
class KWIN_EXPORT ColumnsLayoutEngine : public LayoutEngine
{
    Q_OBJECT

public:
    explicit ColumnsLayoutEngine(QObject *parent = nullptr);
    ~ColumnsLayoutEngine() override;

    LayoutKind layoutKind() const override { return LayoutKind::Columns; }
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

    bool supportsPerTileResize() const override { return true; }
    void adjustTileSize(Window *window, qreal weightDelta, Qt::Orientation axis) override;
    void adjustColumnRatio(qreal delta) override;
    void interactiveResizeEnded(Window *window, const RectF &before, const RectF &after,
                                Qt::Edge edge, const QSizeF &outputSize) override;

private:
    /** Flat index (0-based across all columns, left-to-right, top-to-bottom). */
    int indexOfWindow(Window *window) const;
    /** Returns (column, row) for a given flat index. */
    QPair<int, int> columnRowFromFlatIndex(int flatIdx) const;
    /** Column index (0-based) of @p window, or -1 if not found. */
    int columnOfWindow(Window *window) const;
    /** Returns the column with the fewest windows; ties go to the leftmost. */
    int columnWithFewestWindows() const;
    /** Total number of windows across all columns. */
    int totalLeafCount() const;

    /** Remove the column at @p col, destroying all its leaves. */
    void removeColumn(int col);
    /** Redistribute total width equally among all current columns. */
    void rebalanceColumnWidths();

    QPointer<RootTile> m_root;

    // Dynamic list of columns. Each inner list holds the column's leaf tiles
    // in top-to-bottom order. m_columnLeaves.size() is the current column
    // count (1, 2, or 3).
    QList<QList<QPointer<CustomTile>>> m_columnLeaves;
    // Parallel weight lists, one per column, same structure.
    QList<QList<qreal>> m_columnWeights;
    // Parallel per-column width fractions. Sums to 1.0. Rebalanced to equal
    // when the column count changes; user can adjust via mouse resize.
    QList<qreal> m_columnWidths;

    // Source leaf remembered during an interactive drag-move so the window can
    // be swapped with another tiled window or restored to its original place.
    QPointer<CustomTile> m_moveSourceLeaf;
    int m_moveSourceColumn = -1;
    int m_moveSourceRow = -1;

    static constexpr int m_maxColumns = 3;
    static constexpr qreal m_minWeight = 0.1;
    static constexpr qreal m_maxWeight = 10.0;
    static constexpr qreal m_defaultWeight = 1.0;
};

} // namespace KWin
