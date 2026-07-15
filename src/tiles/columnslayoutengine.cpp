/*
    KWin - the KDE window manager
    SPDX-FileCopyrightText: 2026 KWin Tiling Fork

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "columnslayoutengine.h"
#include "customtile.h"
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

    const int numCols = m_columnLeaves.size();

    if (numCols < m_maxColumns) {
        QList<QPointer<CustomTile>> newCol;
        newCol.append(leaf);
        m_columnLeaves.append(newCol);

        QList<qreal> newWeights;
        newWeights.append(m_defaultWeight);
        m_columnWeights.append(newWeights);

        m_columnWidths.append(0); // placeholder; rebalanced below
        rebalanceColumnWidths();
    } else {
        int col = columnOfWindow(workspace() ? workspace()->activeWindow() : nullptr);
        if (col < 0) {
            col = columnWithFewestWindows();
        }
        if (col < 0 || col >= m_columnLeaves.size()) {
            col = 0;
        }
        m_columnLeaves[col].append(leaf);
        m_columnWeights[col].append(m_defaultWeight);
    }

    if (!leaf->manage(window)) {
        qWarning() << "ColumnsLayoutEngine: failed to manage window" << window->caption();
        for (int c = 0; c < m_columnLeaves.size(); ++c) {
            if (!m_columnLeaves[c].isEmpty() && m_columnLeaves[c].last() == leaf) {
                m_root->destroyChild(leaf);
                m_columnLeaves[c].removeLast();
                m_columnWeights[c].removeLast();
                if (m_columnLeaves[c].isEmpty()) {
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
    for (int c = 0; c < m_columnLeaves.size(); ++c) {
        for (int r = 0; r < m_columnLeaves[c].size(); ++r) {
            if (m_columnLeaves[c][r] && m_columnLeaves[c][r]->windows().contains(window)) {
                CustomTile *leaf = m_columnLeaves[c][r];
                leaf->unmanage(window);
                m_root->destroyChild(leaf);
                m_columnLeaves[c].removeAt(r);
                m_columnWeights[c].removeAt(r);

                if (m_columnLeaves[c].isEmpty()) {
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
        m_columnLeaves[srcCol].swapItemsAt(srcRow, dstRow);
        m_columnWeights[srcCol].swapItemsAt(srcRow, dstRow);
    } else {
        // Swap the two leaves across columns.
        CustomTile *srcLeaf = m_columnLeaves[srcCol][srcRow];
        qreal srcWeight = m_columnWeights[srcCol][srcRow];

        m_columnLeaves[srcCol][srcRow] = m_columnLeaves[dstCol][dstRow];
        m_columnWeights[srcCol][srcRow] = m_columnWeights[dstCol][dstRow];

        m_columnLeaves[dstCol][dstRow] = srcLeaf;
        m_columnWeights[dstCol][dstRow] = srcWeight;
    }

    reflow();
}

void ColumnsLayoutEngine::reflow()
{
    if (!m_root) {
        return;
    }

    const int numCols = m_columnLeaves.size();
    if (numCols == 0) {
        return;
    }

    // Ensure column widths list is in sync.
    while (m_columnWidths.size() < numCols) {
        m_columnWidths.append(0);
    }
    while (m_columnWidths.size() > numCols) {
        m_columnWidths.removeLast();
    }
    if (m_columnWidths.size() == numCols) {
        qreal total = 0;
        for (qreal w : m_columnWidths) {
            total += w;
        }
        if (qFuzzyIsNull(total) || !qFuzzyCompare(total, 1.0)) {
            rebalanceColumnWidths();
        }
    }

    qreal x = 0;
    for (int c = 0; c < numCols; ++c) {
        const int rows = m_columnLeaves[c].size();
        if (rows == 0) {
            x += m_columnWidths[c];
            continue;
        }

        qreal totalWeight = 0;
        for (int r = 0; r < rows; ++r) {
            totalWeight += m_columnWeights[c][r];
        }
        if (qFuzzyIsNull(totalWeight)) {
            totalWeight = rows;
        }

        const qreal colWidth = m_columnWidths[c];
        qreal y = 0;

        for (int r = 0; r < rows; ++r) {
            const qreal height = m_columnWeights[c][r] / totalWeight;
            m_columnLeaves[c][r]->setRelativeGeometry(RectF(x, y, colWidth, height));
            y += height;
        }

        x += colWidth;
    }
}

void ColumnsLayoutEngine::pruneEmptyLeaves()
{
    bool changed = false;

    for (int c = m_columnLeaves.size() - 1; c >= 0; --c) {
        for (int r = m_columnLeaves[c].size() - 1; r >= 0; --r) {
            const auto &leaf = m_columnLeaves[c][r];
            if (leaf && !leaf->windows().isEmpty()) {
                continue;
            }
            if (leaf) {
                m_root->destroyChild(leaf);
            }
            m_columnLeaves[c].removeAt(r);
            m_columnWeights[c].removeAt(r);
            changed = true;
        }

        if (m_columnLeaves[c].isEmpty()) {
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
    for (const auto &col : m_columnLeaves) {
        for (const auto &leaf : col) {
            if (leaf) {
                result.append(leaf->windows());
            }
        }
    }
    return result;
}

Window *ColumnsLayoutEngine::primaryWindow() const
{
    if (m_columnLeaves.isEmpty() || m_columnLeaves[0].isEmpty() || !m_columnLeaves[0][0]) {
        return nullptr;
    }
    const QList<Window *> ws = m_columnLeaves[0][0]->windows();
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
        if (row < m_columnLeaves[col].size() - 1) {
            return ws[flatIdx + 1];
        }
        return nullptr;
    case FocusDirection::Left: {
        if (col == 0) {
            return nullptr;
        }
        const int targetCol = col - 1;
        const int targetRow = qMin(row, m_columnLeaves[targetCol].size() - 1);
        if (targetRow < 0) {
            return nullptr;
        }
        int idx = 0;
        for (int c = 0; c < targetCol; ++c) {
            idx += m_columnLeaves[c].size();
        }
        idx += targetRow;
        return ws.value(idx);
    }
    case FocusDirection::Right: {
        if (col >= m_columnLeaves.size() - 1) {
            return nullptr;
        }
        const int targetCol = col + 1;
        const int targetRow = qMin(row, m_columnLeaves[targetCol].size() - 1);
        if (targetRow < 0) {
            return nullptr;
        }
        int idx = 0;
        for (int c = 0; c < targetCol; ++c) {
            idx += m_columnLeaves[c].size();
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
    if (col < 0 || col >= m_columnLeaves.size()) {
        return;
    }

    int row = -1;
    for (int r = 0; r < m_columnLeaves[col].size(); ++r) {
        if (m_columnLeaves[col][r] && m_columnLeaves[col][r]->windows().contains(window)) {
            row = r;
            break;
        }
    }
    if (row < 0) {
        return;
    }

    QList<qreal> &weights = m_columnWeights[col];
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
    for (const auto &col : m_columnLeaves) {
        for (const auto &leaf : col) {
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
    for (int c = 0; c < m_columnLeaves.size(); ++c) {
        const int colSize = m_columnLeaves[c].size();
        if (flatIdx < running + colSize) {
            return {c, flatIdx - running};
        }
        running += colSize;
    }
    return {-1, -1};
}

int ColumnsLayoutEngine::columnOfWindow(Window *window) const
{
    for (int c = 0; c < m_columnLeaves.size(); ++c) {
        for (const auto &leaf : m_columnLeaves[c]) {
            if (leaf && leaf->windows().contains(window)) {
                return c;
            }
        }
    }
    return -1;
}

int ColumnsLayoutEngine::columnWithFewestWindows() const
{
    int bestCol = 0;
    int fewest = m_columnLeaves.isEmpty() ? 0 : m_columnLeaves[0].size();
    for (int c = 1; c < m_columnLeaves.size(); ++c) {
        if (m_columnLeaves[c].size() < fewest) {
            fewest = m_columnLeaves[c].size();
            bestCol = c;
        }
    }
    return bestCol;
}

int ColumnsLayoutEngine::totalLeafCount() const
{
    int count = 0;
    for (const auto &col : m_columnLeaves) {
        count += col.size();
    }
    return count;
}

void ColumnsLayoutEngine::removeColumn(int col)
{
    if (col < 0 || col >= m_columnLeaves.size()) {
        return;
    }
    for (const auto &leaf : m_columnLeaves[col]) {
        if (leaf) {
            m_root->destroyChild(leaf);
        }
    }
    m_columnLeaves.removeAt(col);
    m_columnWeights.removeAt(col);
    m_columnWidths.removeAt(col);
}

void ColumnsLayoutEngine::rebalanceColumnWidths()
{
    const int n = m_columnLeaves.size();
    if (n == 0) {
        m_columnWidths.clear();
        return;
    }
    const qreal w = 1.0 / n;
    m_columnWidths.resize(n);
    for (int i = 0; i < n; ++i) {
        m_columnWidths[i] = w;
    }
}

void ColumnsLayoutEngine::beginMoveWindow(Window *window)
{
    const int col = columnOfWindow(window);
    if (col < 0 || col >= m_columnLeaves.size()) {
        return;
    }
    for (int r = 0; r < m_columnLeaves[col].size(); ++r) {
        if (m_columnLeaves[col][r] && m_columnLeaves[col][r]->windows().contains(window)) {
            m_moveSourceLeaf = m_columnLeaves[col][r];
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
        for (int c = 0; c < m_columnLeaves.size(); ++c) {
            for (int r = 0; r < m_columnLeaves[c].size(); ++r) {
                if (m_columnLeaves[c][r] && m_columnLeaves[c][r]->windows().contains(target)) {
                    CustomTile *targetLeaf = m_columnLeaves[c][r];
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
    for (const auto &col : m_columnLeaves) {
        if (col.contains(sourceLeaf)) {
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

        for (int c = 0; c < m_columnLeaves.size(); ++c) {
            for (int r = 0; r < m_columnLeaves[c].size(); ++r) {
                if (m_columnLeaves[c][r] == sourceLeaf) {
                    m_columnLeaves[c].removeAt(r);
                    m_columnWeights[c].removeAt(r);

                    if (m_columnLeaves[c].isEmpty()) {
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
    const int numCols = m_columnLeaves.size();
    if (numCols < 2) {
        return;
    }
    const int gaps = numCols - 1;
    const qreal perGap = delta / gaps;

    for (int c = 0; c < gaps; ++c) {
        qreal newLeft = m_columnWidths[c] + perGap;
        qreal newRight = m_columnWidths[c + 1] - perGap;
        constexpr qreal minColWidth = 0.08;
        if (newLeft < minColWidth || newRight < minColWidth) {
            return;
        }
        m_columnWidths[c] = newLeft;
        m_columnWidths[c + 1] = newRight;
    }
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
        if (col < 0 || m_columnWidths.size() <= 1) {
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

        if (leftCol < 0 || rightCol >= m_columnWidths.size()) {
            return;
        }

        const qreal edgeDelta = (edge == Qt::RightEdge)
            ? (after.right() - before.right())
            : (before.left() - after.left());

        const qreal widthDelta = sign * edgeDelta / outputSize.width();
        if (qFuzzyIsNull(widthDelta)) {
            return;
        }

        qreal newLeft = m_columnWidths[leftCol] + widthDelta;
        qreal newRight = m_columnWidths[rightCol] - widthDelta;

        constexpr qreal minColWidth = 0.08;
        constexpr qreal maxColWidth = 0.85;
        if (newLeft < minColWidth || newLeft > maxColWidth ||
            newRight < minColWidth || newRight > maxColWidth) {
            return;
        }

        m_columnWidths[leftCol] = newLeft;
        m_columnWidths[rightCol] = newRight;
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

