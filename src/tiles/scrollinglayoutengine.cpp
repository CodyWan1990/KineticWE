/*
    KWin - the KDE window manager
    SPDX-FileCopyrightText: 2026 KWin Tiling Fork
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "scrollinglayoutengine.h"
#include "customtile.h"
#include "window.h"
#include "workspace.h"
#include "tilemanager.h"

#include <algorithm>
#include <cmath>

namespace KWin
{

namespace
{
// Default column width after the first window (as fraction of output width).
constexpr qreal DefaultColumnWidth = 0.5;
// Minimum column width fraction.
constexpr qreal MinColumnWidth = 0.15;
constexpr qreal MaxColumnWidth = 1.0;
} // namespace

ScrollingLayoutEngine::ScrollingLayoutEngine(QObject *parent)
    : LayoutEngine(parent)
{
}

ScrollingLayoutEngine::~ScrollingLayoutEngine() = default;

void ScrollingLayoutEngine::attach(RootTile *root)
{
    m_root = root;
    if (!m_root) {
        return;
    }

    // Remove any pre-existing children so the engine owns the root completely.
    const QList<Tile *> existingChildren = m_root->childTiles();
    for (Tile *child : existingChildren) {
        if (CustomTile *custom = qobject_cast<CustomTile *>(child)) {
            m_root->destroyChild(custom);
        }
    }
    m_root->setLayoutDirection(Tile::LayoutDirection::Floating);
    m_root->setRelativeGeometry(RectF(0, 0, 1, 1));

    m_leaves.clear();
    m_columnWidths.clear();
    m_viewportOffset = 0.0;
    m_activeColumnIdx = 0;
}

void ScrollingLayoutEngine::addWindow(Window *window)
{
    if (!m_root) {
        return;
    }

    // Determine the insertion position: to the right of the focused column.
    // Clamp to the current leaf count so we never insert beyond the list bounds
    // (e.g. when m_leaves is empty, insertIndex becomes 0, not 1).
    const int insertIndex = qMin(m_activeColumnIdx + 1, m_leaves.count());

    // Create a new leaf tile at the insertion position.
    CustomTile *leaf = m_root->createChildAt(RectF(0, 0, 1, 1), Tile::LayoutDirection::Floating, m_leaves.count());
    m_leaves.insert(insertIndex, leaf);

    // Determine column width
    const int count = m_leaves.count();
    qreal newWidth;
    if (count == 1) {
        newWidth = m_firstWindowWidth;
    } else if (count == 2) {
        // First split: both windows get 50%
        newWidth = 0.5;
        // Update the existing first window from 100% to 50%
        m_columnWidths[0] = 0.5;
    } else {
        newWidth = m_defaultColumnWidth;
    }
    m_columnWidths.insert(insertIndex, newWidth);

    if (!leaf->manage(window)) {
        qWarning() << "ScrollingLayoutEngine: failed to manage window" << window->caption() << "in leaf, desktop mismatch?";
        m_root->destroyChild(leaf);
        m_leaves.removeAt(insertIndex);
        m_columnWidths.removeAt(insertIndex);
        // Restore first window width if we were in the 2-window case
        if (m_leaves.count() == 1) {
            m_columnWidths[0] = m_firstWindowWidth;
        }
        return;
    }

    // Focus the new window by making it the active column.
    m_activeColumnIdx = insertIndex;

    reflow();
}

void ScrollingLayoutEngine::removeWindow(Window *window)
{
    const int idx = indexOfWindow(window);
    if (idx < 0) {
        return;
    }

    CustomTile *leaf = m_leaves.takeAt(idx);
    m_columnWidths.removeAt(idx);
    leaf->unmanage(window);
    m_root->destroyChild(leaf);

    // After removal, adjust the active column index.
    if (m_leaves.isEmpty()) {
        m_activeColumnIdx = 0;
    } else if (m_activeColumnIdx >= m_leaves.count()) {
        m_activeColumnIdx = m_leaves.count() - 1;
    }
    // Window count went from 2 to 1: resize the remaining window to 100%.
    if (m_leaves.count() == 1) {
        m_columnWidths[0] = m_firstWindowWidth;
    }

    reflow();
}

void ScrollingLayoutEngine::pruneEmptyLeaves()
{
    if (!m_root) {
        return;
    }

    bool changed = false;
    for (int i = m_leaves.count() - 1; i >= 0; --i) {
        const auto &leaf = m_leaves[i];
        if (leaf && !leaf->windows().isEmpty()) {
            continue;
        }
        m_columnWidths.removeAt(i);
        if (leaf) {
            m_root->destroyChild(leaf);
        }
        m_leaves.removeAt(i);
        changed = true;
    }

    if (changed) {
        if (m_leaves.count() == 1) {
            m_columnWidths[0] = m_firstWindowWidth;
        }
        if (m_activeColumnIdx >= m_leaves.count() && !m_leaves.isEmpty()) {
            m_activeColumnIdx = m_leaves.count() - 1;
        }
        reflow();
    }
}

void ScrollingLayoutEngine::moveWindow(Window *window, int delta)
{
    const int idx = indexOfWindow(window);
    if (idx < 0) {
        return;
    }

    const int newIdx = qBound(0, idx + delta, m_leaves.count() - 1);
    if (newIdx == idx) {
        return;
    }

    m_leaves.move(idx, newIdx);
    m_columnWidths.move(idx, newIdx);
    m_activeColumnIdx = newIdx;

    reflow();
}

void ScrollingLayoutEngine::beginMoveWindow(Window *window)
{
    m_moveSourceIndex = indexOfWindow(window);
    m_moveSourceLeaf = (m_moveSourceIndex >= 0 && m_moveSourceIndex < m_leaves.count())
        ? m_leaves[m_moveSourceIndex] : nullptr;
}

bool ScrollingLayoutEngine::endMoveWindow(Window *window, Window *target)
{
    if (!window || !m_moveSourceLeaf || m_moveSourceIndex < 0) {
        return false;
    }

    if (target) {
        // Swap positions: put the dragged window where the target is.
        const int targetIdx = indexOfWindow(target);
        if (targetIdx >= 0) {
            m_leaves.move(m_moveSourceIndex, targetIdx);
            m_columnWidths.move(m_moveSourceIndex, targetIdx);
            m_activeColumnIdx = targetIdx;
            reflow();
        }
    }
    // If no target, the window stays where it was.

    m_moveSourceLeaf = nullptr;
    m_moveSourceIndex = -1;
    return true;
}

void ScrollingLayoutEngine::cancelMoveWindow(Window *window)
{
    Q_UNUSED(window)
    m_moveSourceLeaf = nullptr;
    m_moveSourceIndex = -1;
}

void ScrollingLayoutEngine::reflow()
{
    if (!m_root || m_leaves.isEmpty()) {
        return;
    }

    // Calculate the absolute output geometry.
    const RectF tileArea = workspace()->clientArea(MaximizeArea, m_root->manager()->output());

    // Get gap settings from the root tile.
    const qreal gapLeft = m_root->gapMargins().left();
    const qreal gapTop = m_root->gapMargins().top();
    const qreal gapBottom = m_root->gapMargins().bottom();
    const qreal gapBetween = m_root->gapBetween();

    // Available area for window placement.
    const qreal viewAreaY = tileArea.y() + gapTop;
    const qreal viewAreaHeight = tileArea.height() - gapTop - gapBottom;

    // Calculate the viewport: the visible area of the scrollable strip.
    // The viewport offset is in output-relative coordinates (fractions of output width).

    for (int i = 0; i < m_leaves.count(); ++i) {
        CustomTile *leaf = m_leaves[i];
        if (!leaf) {
            continue;
        }

        // Calculate this column's X position in relative (output-fraction) coordinates.
        qreal relX = columnX(i) - m_viewportOffset;

        // Apply the left gap for the first visible column and between-gaps otherwise.
        // Since we're in a scrolling layout, the gap is applied relative to the viewport.
        // Windows are positioned at their calculated absolute positions.

        const qreal colWidth = m_columnWidths[i];

        // The relative geometry of the tile: can extend beyond [0, 1] in X.
        // This is intentional for the scrolling layout.
        leaf->setRelativeGeometry(RectF(relX, 0, colWidth, 1));

        // Get the windows in this leaf and position them.
        // We directly position each window at its absolute coordinate, which can
        // be off-screen. The compositor clips rendering to the output naturally.
        const QList<Window *> leafWindows = leaf->windows();
        for (Window *w : leafWindows) {
            // Calculate absolute position in pixels.
            // We use the tile's absoluteGeometry as a base, but extend beyond
            // the output by applying the offset in absolute terms.
            const qreal absX = tileArea.x() + (relX * tileArea.width());
            // Apply the left gap on the first column (when at viewport left edge),
            // and between-gaps on shared edges.
            qreal effectiveX = absX;
            if (i == 0 && m_viewportOffset <= 0) {
                effectiveX += gapLeft;
            } else if (i > 0) {
                effectiveX += gapBetween / 2.0;
            }
            if (i < m_leaves.count() - 1) {
                // Apply right side gap when there's a neighbour to the right.
                // Handled by the next column's left gap.
            }

            const qreal effectiveWidth = colWidth * tileArea.width() - gapBetween;
            const qreal effectiveY = viewAreaY;
            const qreal effectiveHeight = viewAreaHeight;

            w->moveResize(RectF(effectiveX, effectiveY, effectiveWidth, effectiveHeight));
        }
    }

    Q_EMIT layoutChanged();
}

QList<Window *> ScrollingLayoutEngine::windows() const
{
    QList<Window *> result;
    result.reserve(m_leaves.count());
    for (const auto &leaf : m_leaves) {
        if (leaf && !leaf->windows().isEmpty()) {
            result.append(leaf->windows().first());
        }
    }
    return result;
}

Window *ScrollingLayoutEngine::primaryWindow() const
{
    if (m_leaves.isEmpty() || !m_leaves.first()) {
        return nullptr;
    }
    const QList<Window *> ws = m_leaves.first()->windows();
    return ws.isEmpty() ? nullptr : ws.first();
}

Window *ScrollingLayoutEngine::windowInDirection(Window *from, FocusDirection direction) const
{
    const QList<Window *> ws = windows();
    if (ws.isEmpty()) {
        return nullptr;
    }

    const int idx = from ? ws.indexOf(from) : -1;
    if (idx < 0) {
        return ws.first();
    }

    switch (direction) {
    case FocusDirection::Left:
        if (idx > 0) {
            return ws[idx - 1];
        }
        return nullptr; // No wrap-around; leftmost window stays focused.
    case FocusDirection::Right:
        if (idx < ws.count() - 1) {
            return ws[idx + 1];
        }
        return nullptr; // No wrap-around; rightmost window stays focused.
    case FocusDirection::Up:
    case FocusDirection::Down:
        // No vertical movement yet (column stacking not implemented).
        return nullptr;
    }

    return nullptr;
}

void ScrollingLayoutEngine::focusColumnLeft()
{
    if (m_activeColumnIdx > 0) {
        m_activeColumnIdx--;
        snapViewportToColumn(m_activeColumnIdx);
        Q_EMIT activeColumnChanged(m_activeColumnIdx);
    }
}

void ScrollingLayoutEngine::focusColumnRight()
{
    if (m_activeColumnIdx < m_leaves.count() - 1) {
        m_activeColumnIdx++;
        snapViewportToColumn(m_activeColumnIdx);
        Q_EMIT activeColumnChanged(m_activeColumnIdx);
    }
}

void ScrollingLayoutEngine::moveColumnLeft()
{
    // Move the active column one position to the left in the layout order.
    if (m_activeColumnIdx > 0) {
        m_leaves.swapItemsAt(m_activeColumnIdx, m_activeColumnIdx - 1);
        m_columnWidths.swapItemsAt(m_activeColumnIdx, m_activeColumnIdx - 1);
        m_activeColumnIdx--;
        reflow();
    }
}

void ScrollingLayoutEngine::moveColumnRight()
{
    // Move the active column one position to the right in the layout order.
    if (m_activeColumnIdx < m_leaves.count() - 1) {
        m_leaves.swapItemsAt(m_activeColumnIdx, m_activeColumnIdx + 1);
        m_columnWidths.swapItemsAt(m_activeColumnIdx, m_activeColumnIdx + 1);
        m_activeColumnIdx++;
        reflow();
    }
}

void ScrollingLayoutEngine::adjustTileSize(Window *window, qreal weightDelta, Qt::Orientation axis)
{
    if (axis != Qt::Horizontal) {
        return; // No vertical resizing yet.
    }

    const int idx = indexOfWindow(window);
    if (idx < 0) {
        return;
    }

    qreal newWidth = std::clamp(m_columnWidths[idx] + weightDelta, m_minColumnWidth, m_maxColumnWidth);
    if (qFuzzyCompare(newWidth, m_columnWidths[idx])) {
        return;
    }

    m_columnWidths[idx] = newWidth;
    reflow();
}

void ScrollingLayoutEngine::interactiveResizeEnded(Window *window, const RectF &before, const RectF &after,
                                                     Qt::Edge edge, const QSizeF &outputSize)
{
    if (!window || before.size() == QSizeF() || after.size() == QSizeF()
        || outputSize.width() <= 0 || outputSize.height() <= 0) {
        return;
    }

    // Only horizontal resizes are meaningful in the scrolling layout.
    if (edge == Qt::LeftEdge || edge == Qt::RightEdge) {
        const int idx = indexOfWindow(window);
        if (idx < 0) {
            return;
        }

        const qreal delta = (edge == Qt::RightEdge)
            ? (after.right() - before.right()) / outputSize.width()
            : (before.left() - after.left()) / outputSize.width();

        if (qFuzzyIsNull(delta)) {
            return;
        }

        qreal newWidth = std::clamp(m_columnWidths[idx] + delta, m_minColumnWidth, m_maxColumnWidth);
        if (qFuzzyCompare(newWidth, m_columnWidths[idx])) {
            return;
        }

        m_columnWidths[idx] = newWidth;
        reflow();
        return;
    }

    // Vertical resize is a no-op for this layout.
}

void ScrollingLayoutEngine::setDefaultColumnWidth(qreal width)
{
    m_defaultColumnWidth = std::clamp(width, m_minColumnWidth, m_maxColumnWidth);
}

int ScrollingLayoutEngine::indexOfWindow(Window *window) const
{
    for (int i = 0; i < m_leaves.count(); ++i) {
        if (m_leaves[i] && m_leaves[i]->windows().contains(window)) {
            return i;
        }
    }
    return -1;
}

qreal ScrollingLayoutEngine::columnX(int index) const
{
    qreal x = 0.0;
    const int end = qMin<int>(index, m_columnWidths.count());
    for (int i = 0; i < end; ++i) {
        x += m_columnWidths[i];
    }
    return x;
}

void ScrollingLayoutEngine::snapViewportToColumn(int index)
{
    // The viewport offset should position the target column at the left edge.
    // offset = sum of all column widths before this column.
    const qreal targetX = columnX(index);
    m_viewportOffset = targetX;

    // Clamp the viewport offset so we don't scroll past the right end.
    if (!m_columnWidths.isEmpty()) {
        const qreal totalWidth = columnX(m_columnWidths.count());
        const qreal maxOffset = std::max(0.0, totalWidth - 1.0);
        m_viewportOffset = std::min(m_viewportOffset, maxOffset);
    }
    m_viewportOffset = std::max(0.0, m_viewportOffset);

    reflow();
    Q_EMIT viewportOffsetChanged(m_viewportOffset);
}

void ScrollingLayoutEngine::updateViewport()
{
    snapViewportToColumn(m_activeColumnIdx);
}

} // namespace KWin
