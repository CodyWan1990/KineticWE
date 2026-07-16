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

/*!
 * A single column in the Columns layout.
 *
 * Each column owns its leaf tiles (top-to-bottom), the vertical resize weight
 * for each leaf, and its share of the total available width.  The column's
 * width is expressed as a fraction between 0 and 1; all columns' widths sum
 * to exactly 1.0 (see ColumnsLayoutEngine::normalizeColumnWidths).
 */
struct KWIN_EXPORT Column {
    QList<QPointer<CustomTile>> leaves;  // top-to-bottom order
    QList<qreal> weights;                // parallel vertical weights
    qreal width = 0.0;                   // fraction of total width [0, 1]
};

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
    /**
     * Normalise column widths so they sum to exactly 1.0.
     * Called after any interactive adjustment to prevent floating-point drift
     * that would break the screen-edge detection in Tile::windowGeometry().
     */
    void normalizeColumnWidths();

    QPointer<RootTile> m_root;

    // Dynamic list of columns (each a self-contained Column struct).
    // m_columns.size() is the current column count (1, 2, or 3).
    // All column widths sum to exactly 1.0 (rebalanced on column-count changes,
    // normalised after interactive adjustments).
    QList<Column> m_columns;

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
