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
 * Center tile layout.
 *
 * A three-column tiling layout where the master (focused) windows occupy a
 * centred column and any extra windows form two stacks, one on the left and
 * one on the right. The two side stacks share the remaining screen width
 * equally.
 *
 * The number of master windows in the centre column is controlled by
 * setMasterSize() (default 1). The relative width of the centre column
 * versus the side stacks is controlled by setMasterRatio() (default 0.6).
 *
 * Layout cases by number of tiled windows N:
 *   N <= masterSize
 *       All windows stacked vertically in the centre column. The two side
 *       stacks are empty (zero width).
 *   N == masterSize + 1
 *       The first masterSize windows fill the centre column (left side of
 *       the screen at masterRatio width). The remaining window fills the
 *       right side, occupying the rest of the screen.
 *   N > masterSize + 1
 *       Three columns: left stack, centre master column, right stack. The
 *       side stacks share the remaining width equally.
 *
 * Inspired by the CenterTileLayout class in the truetile kwin script
 * (https://github.com/truegil/truetile).
 */
class KWIN_EXPORT CenterTileLayoutEngine : public LayoutEngine
{
    Q_OBJECT

public:
    explicit CenterTileLayoutEngine(QObject *parent = nullptr);
    ~CenterTileLayoutEngine() override;

    LayoutKind layoutKind() const override { return LayoutKind::CenterTile; }
    void attach(RootTile *root) override;
    void addWindow(Window *window) override;
    void removeWindow(Window *window) override;
    void moveWindow(Window *window, int delta) override;
    void beginMoveWindow(Window *window) override;
    bool endMoveWindow(Window *window, Window *target) override;
    bool insertWindow(Window *window, Window *target, DropZone zone) override;
    void cancelMoveWindow(Window *window) override;
    void reflow() override;
    void pruneEmptyLeaves() override;

    QList<Window *> windows() const override;
    Window *primaryWindow() const override;
    Window *windowInDirection(Window *from, FocusDirection direction) const override;

    qreal masterRatio() const { return m_masterRatio; }
    void setMasterRatio(qreal ratio);

    int masterSize() const { return m_masterSize; }
    void setMasterSize(int count);

    bool supportsPerTileResize() const override { return true; }
    void adjustTileSize(Window *window, qreal weightDelta, Qt::Orientation axis) override;
    void adjustColumnRatio(qreal delta) override { setMasterRatio(m_masterRatio + delta); }
    void interactiveResizeEnded(Window *window, const RectF &before, const RectF &after,
                                Qt::Edge edge, const QSizeF &outputSize) override;

    /**
     * Emitted whenever the master ratio changes, either from an interactive
     * user action (mouse resize, keyboard shortcut) or from a programmatic
     * call to setMasterRatio(). The TilingController connects to this signal
     * to persist the last-used ratio across sessions.
     */
Q_SIGNALS:
    void masterRatioChanged(qreal ratio);

private:
    /**
     * Returns the flat (0-based) index of @p window across all three
     * columns, ordered left stack → master column → right stack.
     * Returns -1 if the window is not in this engine.
     */
    int indexOfWindow(Window *window) const;

    /** Returns the current number of leaves in each column. */
    struct ColumnSizes {
        int leftStack = 0;
        int masterColumn = 0;
        int rightStack = 0;
    };
    ColumnSizes columnSizes() const;

    int totalLeafCount() const {
        return m_leftLeaves.count() + m_masterLeaves.count() + m_rightLeaves.count();
    }

    /**
     * Index of @p window inside the master column, or -1 if it lives in a
     * side stack.
     */
    int masterIndexOfWindow(Window *window) const;
    /**
     * Returns 0 for left stack, 1 for master column, 2 for right stack.
     * Returns -1 if the window is not in this engine.
     */
    int columnIndexOfWindow(Window *window) const;

    /**
     * Find @p window in a specific column list, returning its index within
     * that column. Sets @p idx to the found index, or leaves it unchanged and
     * returns false when the window is not found.
     */
    bool findInColumn(const QList<QPointer<CustomTile>> &leaves,
                      Window *window, int &idx) const;

    QPointer<RootTile> m_root;

    // Three explicit column-wise leaf lists.  The logical layout order is
    // left stack, then master column, then right stack.
    QList<QPointer<CustomTile>> m_leftLeaves;
    QList<QPointer<CustomTile>> m_masterLeaves;
    QList<QPointer<CustomTile>> m_rightLeaves;

    // Per-column weight lists parallel to the leaf lists above.
    QList<qreal> m_leftWeights;
    QList<qreal> m_masterWeights;
    QList<qreal> m_rightWeights;

    // Toggle for alternating newly-added windows between the left and right
    // side stacks once the master column is full.
    bool m_nextWindowGoesRight = false;

    // Master-column width fraction of the output.  Initial default is 0.75;
    // updated from the persisted CenterTileLastMasterRatio config value.
    qreal m_masterRatio = 0.75;
    int m_masterSize = 1;
    static constexpr qreal m_minMasterRatio = 0.2;
    static constexpr qreal m_maxMasterRatio = 0.95;
    static constexpr int m_minMasterSize = 1;
    static constexpr int m_maxMasterSize = 10;
    static constexpr qreal m_minWeight = 0.1;
    static constexpr qreal m_maxWeight = 10.0;

    // Source leaf and column position remembered during an interactive
    // drag-move so the window can be restored or swapped on release.
    QPointer<CustomTile> m_moveSourceLeaf;
    int m_moveSourceColumn = -1;      // 0=left, 1=master, 2=right
    int m_moveSourceColumnIndex = -1; // index within that column
};

} // namespace KWin
