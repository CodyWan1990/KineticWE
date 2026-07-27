/*
    KWin - the KDE window manager
    SPDX-FileCopyrightText: 2026 KWin Tiling Fork

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "columnslayoutengine.h"
#include "customtile.h"
#include "core/output.h"
#include "tilemanager.h"
#include "window.h"
#include "workspace.h"

#include <QtMath>

#include <algorithm>

namespace KWin
{

namespace
{
constexpr qreal MinWeightShare = 0.05;
} // namespace

ColumnsLayoutEngine::ColumnsLayoutEngine(QObject *parent)
    : LayoutEngine(parent)
{
}

ColumnsLayoutEngine::~ColumnsLayoutEngine() = default;

void ColumnsLayoutEngine::attach(RootTile *root)
{
    m_root = root;
    if (!m_root) {
        return;
    }

    const QList<Tile *> existingChildren = m_root->childTiles();
    for (Tile *child : existingChildren) {
        if (CustomTile *custom = qobject_cast<CustomTile *>(child)) {
            m_root->destroyChild(custom);
        }
    }
    m_root->setLayoutDirection(Tile::LayoutDirection::Floating);
    m_root->setRelativeGeometry(RectF(0, 0, 1, 1));
}

void ColumnsLayoutEngine::addWindow(Window *window)
{
    if (!m_root) {
        return;
    }

    CustomTile *leaf = m_root->createChildAt(RectF(0, 0, 1, 1), Tile::LayoutDirection::Floating,
                                              totalLeafCount());

    const int numCols = m_columns.size();

    if (numCols < m_maxColumns) {
        Column col;
        col.leaves.append(leaf);
        col.weights.append(m_defaultWeight);
        col.width = 0.0; // placeholder; rebalanced below
        m_columns.append(col);
        rebalanceColumnWidths();
    } else {
        int col = columnOfWindow(workspace() ? workspace()->activeWindow() : nullptr);
        if (col < 0) {
            col = columnWithFewestWindows();
        }
        if (col < 0 || col >= m_columns.size()) {
            col = 0;
        }
        m_columns[col].leaves.append(leaf);
        m_columns[col].weights.append(m_defaultWeight);
    }

    if (!leaf->manage(window)) {
        qWarning() << "ColumnsLayoutEngine: failed to manage window" << window->caption();
        for (int c = 0; c < m_columns.size(); ++c) {
            if (!m_columns[c].leaves.isEmpty() && m_columns[c].leaves.last() == leaf) {
                m_root->destroyChild(leaf);
                m_columns[c].leaves.removeLast();
                m_columns[c].weights.removeLast();
                if (m_columns[c].leaves.isEmpty()) {
                    removeColumn(c);
                }
                return;
            }
        }
        m_root->destroyChild(leaf);
        return;
    }

    reflow();
}

void ColumnsLayoutEngine::removeWindow(Window *window)
{
    for (int c = 0; c < m_columns.size(); ++c) {
        for (int r = 0; r < m_columns[c].leaves.size(); ++r) {
            if (m_columns[c].leaves[r] && m_columns[c].leaves[r]->windows().contains(window)) {
                CustomTile *leaf = m_columns[c].leaves[r];
                leaf->unmanage(window);
                m_root->destroyChild(leaf);
                m_columns[c].leaves.removeAt(r);
                m_columns[c].weights.removeAt(r);

                if (m_columns[c].leaves.isEmpty()) {
                    removeColumn(c);
                    rebalanceColumnWidths();
                }
                reflow();
                return;
            }
        }
    }
}

void ColumnsLayoutEngine::moveWindow(Window *window, int delta)
{
    if (delta == 0) {
        return;
    }

    const int flatIdx = indexOfWindow(window);
    if (flatIdx < 0) {
        return;
    }

    const int total = totalLeafCount();
    const int newFlatIdx = qBound(0, flatIdx + delta, total - 1);
    if (newFlatIdx == flatIdx) {
        return;
    }

    const auto [srcCol, srcRow] = columnRowFromFlatIndex(flatIdx);
    const auto [dstCol, dstRow] = columnRowFromFlatIndex(newFlatIdx);
    if (srcCol < 0 || dstCol < 0) {
        return;
    }

    if (srcCol == dstCol) {
        m_columns[srcCol].leaves.swapItemsAt(srcRow, dstRow);
        m_columns[srcCol].weights.swapItemsAt(srcRow, dstRow);
    } else {
        // Swap the two leaves across columns.
        CustomTile *srcLeaf = m_columns[srcCol].leaves[srcRow];
        qreal srcWeight = m_columns[srcCol].weights[srcRow];

        m_columns[srcCol].leaves[srcRow] = m_columns[dstCol].leaves[dstRow];
        m_columns[srcCol].weights[srcRow] = m_columns[dstCol].weights[dstRow];

        m_columns[dstCol].leaves[dstRow] = srcLeaf;
        m_columns[dstCol].weights[dstRow] = srcWeight;
    }

    reflow();
}

void ColumnsLayoutEngine::reflow()
{
    if (!m_root) {
        return;
    }

    const int numCols = m_columns.size();
    if (numCols == 0) {
        return;
    }

    // Ensure column sizes are in sync.
    while (m_columns.size() > numCols) {
        m_columns.removeLast();
    }
    if (m_columns.size() == numCols) {
        qreal total = 0;
        for (const Column &col : m_columns) {
            total += col.width;
        }
        if (qFuzzyIsNull(total) || !qFuzzyCompare(total, 1.0)) {
            rebalanceColumnWidths();
        } else {
            // Ensure the total is *exactly* 1.0 to prevent floating-point drift
            // from breaking the screen-edge detection in Tile::windowGeometry().
            normalizeColumnWidths();
        }
    }

    qreal x = 0;
    for (int c = 0; c < numCols; ++c) {
        Column &col = m_columns[c];
        const int rows = col.leaves.size();
        if (rows == 0) {
            x += col.width;
            continue;
        }

        qreal totalWeight = 0;
        for (int r = 0; r < rows; ++r) {
            totalWeight += col.weights[r];
        }
        if (qFuzzyIsNull(totalWeight)) {
            totalWeight = rows;
        }

        const qreal colWidth = col.width;
        qreal y = 0;

        for (int r = 0; r < rows; ++r) {
            const qreal height = col.weights[r] / totalWeight;
            col.leaves[r]->setRelativeGeometry(RectF(x, y, colWidth, height));
            y += height;
        }

        x += colWidth;
    }
}

void ColumnsLayoutEngine::pruneEmptyLeaves()
{
    bool changed = false;

    for (int c = m_columns.size() - 1; c >= 0; --c) {
        for (int r = m_columns[c].leaves.size() - 1; r >= 0; --r) {
            const auto &leaf = m_columns[c].leaves[r];
            if (leaf && !leaf->windows().isEmpty()) {
                continue;
            }
            if (leaf) {
                m_root->destroyChild(leaf);
            }
            m_columns[c].leaves.removeAt(r);
            m_columns[c].weights.removeAt(r);
            changed = true;
        }

        if (m_columns[c].leaves.isEmpty()) {
            removeColumn(c);
            rebalanceColumnWidths();
            changed = true;
        }
    }

    if (changed) {
        reflow();
    }
}

QList<Window *> ColumnsLayoutEngine::windows() const
{
    QList<Window *> result;
    for (const auto &col : m_columns) {
        for (const auto &leaf : col.leaves) {
            if (leaf) {
                result.append(leaf->windows());
            }
        }
    }
    return result;
}

Window *ColumnsLayoutEngine::primaryWindow() const
{
    if (m_columns.isEmpty() || m_columns[0].leaves.isEmpty() || !m_columns[0].leaves[0]) {
        return nullptr;
    }
    const QList<Window *> ws = m_columns[0].leaves[0]->windows();
    return ws.isEmpty() ? nullptr : ws.first();
}

Window *ColumnsLayoutEngine::windowInDirection(Window *from, FocusDirection direction) const
{
    const QList<Window *> ws = windows();
    if (ws.isEmpty()) {
        return nullptr;
    }

    const int flatIdx = from ? ws.indexOf(from) : -1;
    if (flatIdx < 0) {
        return ws.first();
    }

    const auto [col, row] = columnRowFromFlatIndex(flatIdx);
    if (col < 0) {
        return nullptr;
    }

    switch (direction) {
    case FocusDirection::Up:
        if (row > 0) {
            return ws[flatIdx - 1];
        }
        return nullptr;
    case FocusDirection::Down:
        if (row < m_columns[col].leaves.size() - 1) {
            return ws[flatIdx + 1];
        }
        return nullptr;
    case FocusDirection::Left: {
        if (col == 0) {
            return nullptr;
        }
        const int targetCol = col - 1;
        const int targetRow = qMin(row, m_columns[targetCol].leaves.size() - 1);
        if (targetRow < 0) {
            return nullptr;
        }
        int idx = 0;
        for (int c = 0; c < targetCol; ++c) {
            idx += m_columns[c].leaves.size();
        }
        idx += targetRow;
        return ws.value(idx);
    }
    case FocusDirection::Right: {
        if (col >= m_columns.size() - 1) {
            return nullptr;
        }
        const int targetCol = col + 1;
        const int targetRow = qMin(row, m_columns[targetCol].leaves.size() - 1);
        if (targetRow < 0) {
            return nullptr;
        }
        int idx = 0;
        for (int c = 0; c < targetCol; ++c) {
            idx += m_columns[c].leaves.size();
        }
        idx += targetRow;
        return ws.value(idx);
    }
    }

    return nullptr;
}

void ColumnsLayoutEngine::adjustTileSize(Window *window, qreal weightDelta, Qt::Orientation axis)
{
    if (axis != Qt::Vertical || qFuzzyIsNull(weightDelta)) {
        return;
    }

    const int col = columnOfWindow(window);
    if (col < 0 || col >= m_columns.size()) {
        return;
    }

    int row = -1;
    for (int r = 0; r < m_columns[col].leaves.size(); ++r) {
        if (m_columns[col].leaves[r] && m_columns[col].leaves[r]->windows().contains(window)) {
            row = r;
            break;
        }
    }
    if (row < 0) {
        return;
    }

    QList<qreal> &weights = m_columns[col].weights;
    if (weights.isEmpty()) {
        return;
    }

    const int neighbourIdx = (row < weights.size() - 1) ? row + 1 : row - 1;
    if (neighbourIdx < 0 || neighbourIdx >= weights.size() || neighbourIdx == row) {
        return;
    }

    qreal newSelf = std::clamp(weights[row] + weightDelta, m_minWeight, m_maxWeight);
    qreal actualDelta = newSelf - weights[row];
    qreal newNeighbour = std::clamp(weights[neighbourIdx] - actualDelta, m_minWeight, m_maxWeight);

    if (qFuzzyCompare(weights[row], newSelf) && qFuzzyCompare(weights[neighbourIdx], newNeighbour)) {
        return;
    }

    weights[row] = newSelf;
    weights[neighbourIdx] = newNeighbour;
    reflow();
}

int ColumnsLayoutEngine::indexOfWindow(Window *window) const
{
    int idx = 0;
    for (const auto &col : m_columns) {
        for (const auto &leaf : col.leaves) {
            if (leaf && leaf->windows().contains(window)) {
                return idx;
            }
            ++idx;
        }
    }
    return -1;
}

QPair<int, int> ColumnsLayoutEngine::columnRowFromFlatIndex(int flatIdx) const
{
    int running = 0;
    for (int c = 0; c < m_columns.size(); ++c) {
        const int colSize = m_columns[c].leaves.size();
        if (flatIdx < running + colSize) {
            return {c, flatIdx - running};
        }
        running += colSize;
    }
    return {-1, -1};
}

int ColumnsLayoutEngine::columnOfWindow(Window *window) const
{
    for (int c = 0; c < m_columns.size(); ++c) {
        for (const auto &leaf : m_columns[c].leaves) {
            if (leaf && leaf->windows().contains(window)) {
                return c;
            }
        }
    }
    return -1;
}

QPair<int, int> ColumnsLayoutEngine::columnAndRowOfWindow(Window *window) const
{
    for (int c = 0; c < m_columns.size(); ++c) {
        for (int r = 0; r < m_columns[c].leaves.size(); ++r) {
            if (m_columns[c].leaves[r] && m_columns[c].leaves[r]->windows().contains(window)) {
                return {c, r};
            }
        }
    }
    return {-1, -1};
}

int ColumnsLayoutEngine::columnWithFewestWindows() const
{
    int bestCol = 0;
    int fewest = m_columns.isEmpty() ? 0 : m_columns[0].leaves.size();
    for (int c = 1; c < m_columns.size(); ++c) {
        if (m_columns[c].leaves.size() < fewest) {
            fewest = m_columns[c].leaves.size();
            bestCol = c;
        }
    }
    return bestCol;
}

int ColumnsLayoutEngine::totalLeafCount() const
{
    int count = 0;
    for (const auto &col : m_columns) {
        count += col.leaves.size();
    }
    return count;
}

void ColumnsLayoutEngine::removeColumn(int col)
{
    if (col < 0 || col >= m_columns.size()) {
        return;
    }
    for (const auto &leaf : m_columns[col].leaves) {
        if (leaf) {
            m_root->destroyChild(leaf);
        }
    }
    m_columns.removeAt(col);
}

void ColumnsLayoutEngine::rebalanceColumnWidths()
{
    const int n = m_columns.size();
    if (n == 0) {
        return;
    }

    // Without a root tile, fall back to equal widths.
    if (!m_root) {
        const qreal w = 1.0 / n;
        for (int i = 0; i < n; ++i) {
            m_columns[i].width = w;
        }
        return;
    }

    // Retrieve gap values from the tile tree and the output size so we can
    // convert pixel-based gaps into relative (0–1) units.  The column widths
    // are then computed so that every column's *usable content* (i.e. the
    // portion left after subtracting its share of the gaps) is identical.
    const qreal gapBetween = m_root->gapBetween();
    const QMarginsF gapMargins = m_root->gapMargins();
    const qreal outputWidth = m_root->manager()->output()->geometryF().width();

    if (outputWidth <= 0) {
        const qreal w = 1.0 / n;
        for (int i = 0; i < n; ++i) {
            m_columns[i].width = w;
        }
        return;
    }

    // Convert pixel gaps to relative fractions of the output width.
    const qreal gl = gapMargins.left() / outputWidth;
    const qreal gr = gapMargins.right() / outputWidth;
    const qreal gb = gapBetween / outputWidth;

    // Total relative width consumed by gaps between the leftmost and rightmost
    // content edges: left margin + (n-1) between-gaps + right margin.
    const qreal totalGapRel = gl + (n > 1 ? (n - 1) * gb : 0) + gr;

    // Single column: full width; gaps are handled by the margin system.
    if (n == 1) {
        m_columns[0].width = 1.0;
        return;
    }

    // If the configured gaps leave no room for window content, fall back to
    // equal widths and let the user adjust.
    if (totalGapRel >= 1.0) {
        const qreal w = 1.0 / n;
        for (int i = 0; i < n; ++i) {
            m_columns[i].width = w;
        }
        return;
    }

    // Available relative width for content, split evenly among all columns.
    const qreal contentPerCol = (1.0 - totalGapRel) / n;
    constexpr qreal minColWidth = 0.05;
    constexpr qreal maxColWidth = 0.95;

    // --- Gap-aware column widths ---
    //
    // Each column's relative tile width = its content share + the gaps it
    // borders.  The centre column (or columns) pays the between-gap on both
    // sides, so it gets an extra gb relative to the side columns.
    //
    //   col 0   :  contentPerCol + gapLeft      + gapBetween/2
    //   col 1..n-2: contentPerCol + gapBetween
    //   col n-1  : contentPerCol + gapBetween/2 + gapRight

    m_columns[0].width = std::clamp(contentPerCol + gl + gb / 2.0, minColWidth, maxColWidth);

    for (int i = 1; i < n - 1; ++i) {
        m_columns[i].width = std::clamp(contentPerCol + gb, minColWidth, maxColWidth);
    }

    m_columns[n - 1].width = std::clamp(contentPerCol + gb / 2.0 + gr, minColWidth, maxColWidth);

    // Normalise so the total is *exactly* 1.0, preventing floating-point drift
    // from breaking screen-edge detection in Tile::windowGeometry().
    qreal total = 0;
    for (int i = 0; i < n; ++i) {
        total += m_columns[i].width;
    }
    if (!qFuzzyIsNull(total)) {
        qreal sum = 0;
        for (int i = 0; i < n - 1; ++i) {
            const qreal clamped = std::clamp(m_columns[i].width / total, qreal(0.05), qreal(0.95));
            m_columns[i].width = clamped;
            sum += clamped;
        }
        m_columns[n - 1].width = qreal(1.0) - sum;
    }
}

void ColumnsLayoutEngine::normalizeColumnWidths()
{
    const int n = m_columns.size();
    if (n == 0) {
        return;
    }
    qreal total = 0;
    for (const auto &col : m_columns) {
        total += col.width;
    }
    if (qFuzzyIsNull(total)) {
        rebalanceColumnWidths();
        return;
    }
    // Scale all columns proportionally so that the last column is set to
    // 1.0 - sum_of_others, making the total bit-exact 1.0.  This prevents
    // floating-point drift from breaking the screen-edge detection in
    // Tile::windowGeometry().
    qreal sum = 0;
    for (int i = 0; i < n - 1; ++i) {
        const qreal clamped = std::clamp(m_columns[i].width / total, qreal(0.08), qreal(0.85));
        m_columns[i].width = clamped;
        sum += clamped;
    }
    m_columns[n - 1].width = qreal(1.0) - sum;
    // If the last column is out of range, rebalance entirely.
    if (m_columns[n - 1].width < qreal(0.08) || m_columns[n - 1].width > qreal(0.85)) {
        rebalanceColumnWidths();
    }
}

void ColumnsLayoutEngine::beginMoveWindow(Window *window)
{
    const int col = columnOfWindow(window);
    if (col < 0 || col >= m_columns.size()) {
        return;
    }
    for (int r = 0; r < m_columns[col].leaves.size(); ++r) {
        if (m_columns[col].leaves[r] && m_columns[col].leaves[r]->windows().contains(window)) {
            m_moveSourceLeaf = m_columns[col].leaves[r];
            m_moveSourceColumn = col;
            m_moveSourceRow = r;
            return;
        }
    }
}

bool ColumnsLayoutEngine::endMoveWindow(Window *window, Window *target)
{
    QPointer<CustomTile> sourceLeaf = m_moveSourceLeaf;
    m_moveSourceLeaf.clear();
    m_moveSourceColumn = -1;
    m_moveSourceRow = -1;

    if (!sourceLeaf) {
        return false;
    }

    if (target && target != window) {
        for (int c = 0; c < m_columns.size(); ++c) {
            for (int r = 0; r < m_columns[c].leaves.size(); ++r) {
                if (m_columns[c].leaves[r] && m_columns[c].leaves[r]->windows().contains(target)) {
                    CustomTile *targetLeaf = m_columns[c].leaves[r];
                    targetLeaf->unmanage(target);
                    targetLeaf->manage(window);
                    sourceLeaf->manage(target);
                    reflow();
                    return true;
                }
            }
        }
    }

    if (!sourceLeaf->windows().contains(window)) {
        sourceLeaf->manage(window);
    }
    reflow();
    return true;
}

bool ColumnsLayoutEngine::endMoveWindowOnZone(Window *window, Window *target, DropZone zone)
{
    if (zone == DropZone::Swap || !target || target == window) {
        return endMoveWindow(window, target);
    }

    QPointer<CustomTile> sourceLeaf = m_moveSourceLeaf;
    const int srcCol = m_moveSourceColumn;
    const int srcRow = m_moveSourceRow;
    m_moveSourceLeaf.clear();
    m_moveSourceColumn = -1;
    m_moveSourceRow = -1;

    if (!sourceLeaf || srcCol < 0 || srcCol >= m_columns.size()
        || srcRow < 0 || srcRow >= m_columns[srcCol].leaves.size()
        || m_columns[srcCol].leaves[srcRow] != sourceLeaf) {
        return false;
    }

    // Locate the target window's column and row.
    const auto [targetCol0, targetRow0] = columnAndRowOfWindow(target);
    if (targetCol0 < 0) {
        // The target vanished mid-drag; restore the window to its source.
        if (!sourceLeaf->windows().contains(window)) {
            sourceLeaf->manage(window);
        }
        reflow();
        return true;
    }
    int targetCol = targetCol0;
    int targetRow = targetRow0;

    // Detach the dragged window's leaf from its column. The leaf itself is
    // moved into the target column, so the window stays managed throughout.
    m_columns[srcCol].leaves.removeAt(srcRow);
    m_columns[srcCol].weights.removeAt(srcRow);

    if (m_columns[srcCol].leaves.isEmpty()) {
        // The dragged window was alone in its column; the column goes away
        // and the remaining columns take over its share of the width.
        removeColumn(srcCol);
        rebalanceColumnWidths();
        if (srcCol < targetCol) {
            targetCol -= 1;
        }
    } else if (srcCol == targetCol && srcRow < targetRow) {
        // Same-column reorder: removing the source shifted the target up.
        targetRow -= 1;
    }

    const int insertRow = (zone == DropZone::InsertAbove) ? targetRow : targetRow + 1;
    m_columns[targetCol].leaves.insert(insertRow, sourceLeaf);
    m_columns[targetCol].weights.insert(insertRow, m_defaultWeight);
    // A real interactive drag detaches the window from its tile on the first
    // movement (Window::updateInteractiveMoveResize untiles moved windows), so
    // the leaf we just moved is usually empty. Re-attach the window so it
    // snaps into its new slot instead of staying floating and leaving an
    // empty "ghost" tile behind.
    if (!sourceLeaf->windows().contains(window)) {
        sourceLeaf->manage(window);
    }
    reflow();
    return true;
}

void ColumnsLayoutEngine::consumeWindow(Window *window)
{
    if (!m_root || !window) {
        return;
    }

    const auto [col, row] = columnAndRowOfWindow(window);
    if (col < 0) {
        return;
    }

    // Prefer the column to the right; fall back to the one to the left.
    int targetCol = col + 1;
    if (targetCol >= m_columns.size()) {
        targetCol = col - 1;
    }
    if (targetCol < 0) {
        // Only one column: nowhere to be consumed into.
        return;
    }

    // Detach the window's leaf from its column and append it at the bottom
    // of the target column.
    QPointer<CustomTile> leaf = m_columns[col].leaves.takeAt(row);
    m_columns[col].weights.removeAt(row);

    if (m_columns[col].leaves.isEmpty()) {
        removeColumn(col);
        rebalanceColumnWidths();
        if (col < targetCol) {
            targetCol -= 1;
        }
    }

    m_columns[targetCol].leaves.append(leaf);
    m_columns[targetCol].weights.append(m_defaultWeight);
    reflow();
}

void ColumnsLayoutEngine::expelWindow(Window *window)
{
    if (!m_root || !window) {
        return;
    }

    const auto [col, row] = columnAndRowOfWindow(window);
    if (col < 0) {
        return;
    }
    if (m_columns[col].leaves.size() <= 1) {
        // Already alone in its column.
        return;
    }
    if (m_columns.size() >= m_maxColumns) {
        // At the configured column cap.
        return;
    }

    // Detach the window's leaf and give it its own column.
    QPointer<CustomTile> leaf = m_columns[col].leaves.takeAt(row);
    const qreal weight = m_columns[col].weights.takeAt(row);

    Column newColumn;
    newColumn.leaves.append(leaf);
    newColumn.weights.append(weight);
    newColumn.width = 0.0; // placeholder; rebalanced below

    // The new column goes to the right of the old one; if the old column is
    // the rightmost, the new column goes to its left instead.
    int insertAt = col + 1;
    if (col == m_columns.size() - 1) {
        insertAt = col;
    }
    m_columns.insert(insertAt, newColumn);

    rebalanceColumnWidths();
    reflow();
}

void ColumnsLayoutEngine::setMaxColumns(int maxColumns)
{
    m_maxColumns = std::clamp(maxColumns, m_minMaxColumns, m_maxMaxColumns);
}


void ColumnsLayoutEngine::cancelMoveWindow(Window *window)
{
    QPointer<CustomTile> sourceLeaf = m_moveSourceLeaf;
    m_moveSourceLeaf.clear();
    m_moveSourceColumn = -1;
    m_moveSourceRow = -1;

    if (!sourceLeaf) {
        return;
    }

    bool found = false;
    for (const auto &col : m_columns) {
        if (col.leaves.contains(sourceLeaf)) {
            found = true;
            break;
        }
    }
    if (!found) {
        return;
    }

    if (sourceLeaf->windows().isEmpty() || sourceLeaf->windows().contains(window)) {
        if (sourceLeaf->windows().contains(window)) {
            sourceLeaf->unmanage(window);
        }

        for (int c = 0; c < m_columns.size(); ++c) {
            for (int r = 0; r < m_columns[c].leaves.size(); ++r) {
                if (m_columns[c].leaves[r] == sourceLeaf) {
                    m_columns[c].leaves.removeAt(r);
                    m_columns[c].weights.removeAt(r);

                    if (m_columns[c].leaves.isEmpty()) {
                        removeColumn(c);
                    } else {
                        rebalanceColumnWidths();
                    }

                    m_root->destroyChild(sourceLeaf);
                    reflow();
                    return;
                }
            }
        }
    }
}

void ColumnsLayoutEngine::adjustColumnRatio(qreal delta)
{
    const int numCols = m_columns.size();
    if (numCols < 2) {
        return;
    }

    // Find the column of the focused window to choose which boundary to adjust.
    Window *active = workspace() ? workspace()->activeWindow() : nullptr;
    int focusCol = columnOfWindow(active);
    if (focusCol < 0 || focusCol >= numCols) {
        focusCol = 0;
    }

    // Adjust the boundary between focusCol and focusCol + 1.
    // If this is the rightmost column, adjust the left boundary instead.
    int boundary = focusCol;
    if (boundary >= numCols - 1) {
        boundary = numCols - 2;
    }

    constexpr qreal minColWidth = 0.08;
    qreal newLeft = m_columns[boundary].width + delta;
    qreal newRight = m_columns[boundary + 1].width - delta;

    if (newLeft < minColWidth || newRight < minColWidth) {
        return;
    }

    m_columns[boundary].width = newLeft;
    m_columns[boundary + 1].width = newRight;
    normalizeColumnWidths();
    reflow();
}

void ColumnsLayoutEngine::interactiveResizeEnded(Window *window, const RectF &before, const RectF &after,
                                                  Qt::Edge edge, const QSizeF &outputSize)
{
    if (!window || before.size() == QSizeF() || after.size() == QSizeF()
        || outputSize.width() <= 0 || outputSize.height() <= 0) {
        return;
    }

    if (edge == Qt::LeftEdge || edge == Qt::RightEdge) {
        const int col = columnOfWindow(window);
        if (col < 0 || m_columns.size() <= 1) {
            return;
        }

        int leftCol, rightCol;
        qreal sign;
        if (edge == Qt::RightEdge) {
            leftCol = col;
            rightCol = col + 1;
            sign = 1.0;
        } else {
            leftCol = col - 1;
            rightCol = col;
            sign = -1.0;
        }

        // If the resize would push past the screen edge (left edge of leftmost
        // column or right edge of rightmost column), there is no neighbour to
        // transfer width to — this is a no-op (no resize handle should appear
        // for these edges anyway).
        if (leftCol < 0 || rightCol >= m_columns.size()) {
            return;
        }

        const qreal edgeDelta = (edge == Qt::RightEdge)
            ? (after.right() - before.right())
            : (before.left() - after.left());

        const qreal widthDelta = sign * edgeDelta / outputSize.width();
        if (qFuzzyIsNull(widthDelta)) {
            return;
        }

        qreal newLeft = m_columns[leftCol].width + widthDelta;
        qreal newRight = m_columns[rightCol].width - widthDelta;

        constexpr qreal minColWidth = 0.08;
        constexpr qreal maxColWidth = 0.85;
        if (newLeft < minColWidth || newLeft > maxColWidth ||
            newRight < minColWidth || newRight > maxColWidth) {
            return;
        }

        m_columns[leftCol].width = newLeft;
        m_columns[rightCol].width = newRight;
        // Normalise after adjustment to prevent floating-point drift from
        // breaking the screen-edge gap detection in Tile::windowGeometry().
        normalizeColumnWidths();
        reflow();
        return;
    }

    // Vertical push/pull within the window's column.
    const qreal weightDelta = (edge == Qt::BottomEdge)
        ? (after.bottom() - before.bottom()) / outputSize.height()
        : (before.top() - after.top()) / outputSize.height();
    if (qFuzzyIsNull(weightDelta)) {
        return;
    }
    adjustTileSize(window, std::clamp(weightDelta, qreal(-0.5), qreal(0.5)), Qt::Vertical);
}

} // namespace KWin

