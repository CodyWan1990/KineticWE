/*
    KWin - the KDE window manager
    SPDX-FileCopyrightText: 2026 KWin Tiling Fork

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "centertilelayoutengine.h"
#include "customtile.h"
#include "window.h"

#include <QtMath>

#include <algorithm>

namespace KWin
{

namespace
{
// Per-tile weight applied to a freshly-added leaf.
constexpr qreal DefaultWeight = 1.0;

// Minimum share of a column that a single leaf is allowed to take.
constexpr qreal MinWeightShare = 0.05;
} // namespace

CenterTileLayoutEngine::CenterTileLayoutEngine(QObject *parent)
    : LayoutEngine(parent)
{
}

CenterTileLayoutEngine::~CenterTileLayoutEngine() = default;

void CenterTileLayoutEngine::attach(RootTile *root)
{
    m_root = root;
    if (!m_root) {
        return;
    }

    // Take full ownership of the root tile. Remove any pre-existing default
    // layout children that would fight with the engine's geometry calculations.
    const QList<Tile *> existingChildren = m_root->childTiles();
    for (Tile *child : existingChildren) {
        if (CustomTile *custom = qobject_cast<CustomTile *>(child)) {
            m_root->destroyChild(custom);
        }
    }
    m_root->setLayoutDirection(Tile::LayoutDirection::Floating);
    m_root->setRelativeGeometry(RectF(0, 0, 1, 1));
}

void CenterTileLayoutEngine::addWindow(Window *window)
{
    if (!m_root) {
        return;
    }

    CustomTile *leaf = m_root->createChildAt(RectF(0, 0, 1, 1), Tile::LayoutDirection::Floating,
                                              totalLeafCount());

    // If the master column still has room, fill it.  Otherwise add the new
    // window to a side stack, alternating left / right.
    if (m_masterLeaves.count() < m_masterSize) {
        m_masterLeaves.append(leaf);
        m_masterWeights.append(DefaultWeight);
    } else if (m_nextWindowGoesRight) {
        m_rightLeaves.append(leaf);
        m_rightWeights.append(DefaultWeight);
        m_nextWindowGoesRight = false;
    } else {
        m_leftLeaves.append(leaf);
        m_leftWeights.append(DefaultWeight);
        m_nextWindowGoesRight = true;
    }

    if (!leaf->manage(window)) {
        qWarning() << "CenterTileLayoutEngine: failed to manage window" << window->caption()
                    << "in leaf, desktop mismatch?";
        // Remember which column the leaf was appended to *before* destroyChild
        // zeroes out the QPointer (destroyChild does a synchronous delete tile).
        const bool wasMaster = (!m_masterLeaves.isEmpty() && m_masterLeaves.last() == leaf);
        const bool wasLeft   = (!m_leftLeaves.isEmpty()   && m_leftLeaves.last()   == leaf);
        const bool wasRight  = (!m_rightLeaves.isEmpty()  && m_rightLeaves.last()  == leaf);
        m_root->destroyChild(leaf);
        if (wasMaster) {
            m_masterLeaves.removeLast();
            m_masterWeights.removeLast();
        } else if (wasLeft) {
            m_leftLeaves.removeLast();
            m_leftWeights.removeLast();
        } else if (wasRight) {
            m_rightLeaves.removeLast();
            m_rightWeights.removeLast();
        }
        return;
    }

    reflow();
}

void CenterTileLayoutEngine::removeWindow(Window *window)
{
    // Find which column and index the window is in, then remove it from that
    // column's leaf and weight lists.

    // Search left stack.
    for (int i = 0; i < m_leftLeaves.count(); ++i) {
        if (m_leftLeaves[i] && m_leftLeaves[i]->windows().contains(window)) {
            m_leftWeights.removeAt(i);
            CustomTile *leaf = m_leftLeaves.takeAt(i);
            leaf->unmanage(window);
            m_root->destroyChild(leaf);
            reflow();
            return;
        }
    }
    // Search master column.
    for (int i = 0; i < m_masterLeaves.count(); ++i) {
        if (m_masterLeaves[i] && m_masterLeaves[i]->windows().contains(window)) {
            m_masterWeights.removeAt(i);
            CustomTile *leaf = m_masterLeaves.takeAt(i);
            leaf->unmanage(window);
            m_root->destroyChild(leaf);

            // Promote a side-stack window to fill the empty master slot so the
            // centre column doesn't end up with fewer windows than expected.
            if (!m_leftLeaves.isEmpty() || !m_rightLeaves.isEmpty()) {
                CustomTile *promotedLeaf = nullptr;
                qreal promotedWeight = DefaultWeight;
                // Pick from the stack that has more windows to keep the balance.
                if (m_leftLeaves.count() >= m_rightLeaves.count()) {
                    promotedLeaf = m_leftLeaves.takeLast();
                    promotedWeight = m_leftWeights.takeLast();
                } else {
                    promotedLeaf = m_rightLeaves.takeLast();
                    promotedWeight = m_rightWeights.takeLast();
                }
                m_masterLeaves.insert(i, promotedLeaf);
                m_masterWeights.insert(i, promotedWeight);
            }

            reflow();
            return;
        }
    }
    // Search right stack.
    for (int i = 0; i < m_rightLeaves.count(); ++i) {
        if (m_rightLeaves[i] && m_rightLeaves[i]->windows().contains(window)) {
            m_rightWeights.removeAt(i);
            CustomTile *leaf = m_rightLeaves.takeAt(i);
            leaf->unmanage(window);
            m_root->destroyChild(leaf);
            reflow();
            return;
        }
    }
}

void CenterTileLayoutEngine::pruneEmptyLeaves()
{
    if (!m_root) {
        return;
    }

    bool changed = false;
    auto pruneColumn = [&](QList<QPointer<CustomTile>> &leaves, QList<qreal> &weights) {
        for (int i = leaves.count() - 1; i >= 0; --i) {
            const auto &leaf = leaves[i];
            if (leaf && !leaf->windows().isEmpty()) {
                continue;
            }
            weights.removeAt(i);
            if (leaf) {
                m_root->destroyChild(leaf);
            }
            leaves.removeAt(i);
            changed = true;
        }
    };
    pruneColumn(m_rightLeaves, m_rightWeights);
    pruneColumn(m_masterLeaves, m_masterWeights);
    pruneColumn(m_leftLeaves, m_leftWeights);

    if (changed) {
        reflow();
    }
}

void CenterTileLayoutEngine::moveWindow(Window *window, int delta)
{
    // Build flat representations of the three columns so we can swap items
    // at a flat index and then redistribute back.
    QList<QPointer<CustomTile>> flatLeaves = m_leftLeaves + m_masterLeaves + m_rightLeaves;
    QList<qreal> flatWeights = m_leftWeights + m_masterWeights + m_rightWeights;

    const int idx = indexOfWindow(window);
    if (idx < 0) {
        return;
    }

    const int newIdx = qBound(0, idx + delta, flatLeaves.count() - 1);
    if (newIdx == idx) {
        return;
    }

    flatLeaves.swapItemsAt(idx, newIdx);
    flatWeights.swapItemsAt(idx, newIdx);

    // Redistribute back using the original column counts.
    const int leftCount = m_leftLeaves.count();
    const int masterCount = m_masterLeaves.count();
    m_leftLeaves = flatLeaves.mid(0, leftCount);
    m_leftWeights = flatWeights.mid(0, leftCount);
    m_masterLeaves = flatLeaves.mid(leftCount, masterCount);
    m_masterWeights = flatWeights.mid(leftCount, masterCount);
    m_rightLeaves = flatLeaves.mid(leftCount + masterCount);
    m_rightWeights = flatWeights.mid(leftCount + masterCount);

    reflow();
}

void CenterTileLayoutEngine::beginMoveWindow(Window *window)
{
    // Remember which column and index the dragged window originated from so
    // endMoveWindow / cancelMoveWindow can act on the correct list.
    m_moveSourceLeaf.clear();
    m_moveSourceColumn = -1;
    m_moveSourceColumnIndex = -1;

    int idx;
    if (findInColumn(m_leftLeaves, window, idx)) {
        m_moveSourceLeaf = m_leftLeaves[idx];
        m_moveSourceColumn = 0;
        m_moveSourceColumnIndex = idx;
    } else if (findInColumn(m_masterLeaves, window, idx)) {
        m_moveSourceLeaf = m_masterLeaves[idx];
        m_moveSourceColumn = 1;
        m_moveSourceColumnIndex = idx;
    } else if (findInColumn(m_rightLeaves, window, idx)) {
        m_moveSourceLeaf = m_rightLeaves[idx];
        m_moveSourceColumn = 2;
        m_moveSourceColumnIndex = idx;
    }
}

bool CenterTileLayoutEngine::endMoveWindow(Window *window, Window *target)
{
    QPointer<CustomTile> sourceLeaf = m_moveSourceLeaf;
    m_moveSourceLeaf.clear();
    m_moveSourceColumn = -1;
    m_moveSourceColumnIndex = -1;

    if (!sourceLeaf) {
        return false;
    }

    if (target && target != window) {
        // Find the target window's leaf via columnOfWindow and swap.
        const int tgtCol = columnIndexOfWindow(target);
        if (tgtCol >= 0) {
            auto *targetLeaves = (tgtCol == 0) ? &m_leftLeaves
                                : (tgtCol == 1) ? &m_masterLeaves
                                                : &m_rightLeaves;
            int tgtIdx;
            if (findInColumn(*targetLeaves, target, tgtIdx)) {
                CustomTile *targetLeaf = targetLeaves->at(tgtIdx);
                targetLeaf->unmanage(target);
                targetLeaf->manage(window);
                sourceLeaf->manage(target);
                reflow();
                return true;
            }
        }
    }

    // No valid target: restore the window to its original leaf.
    if (!sourceLeaf->windows().contains(window)) {
        sourceLeaf->manage(window);
    }
    reflow();
    return true;
}

bool CenterTileLayoutEngine::insertWindow(Window *window, Window *target, DropZone zone)
{
    if (!window || !target || window == target) {
        return false;
    }

    // Recover a window detached by an interactive drag from its remembered leaf.
    if (indexOfWindow(window) < 0) {
        const bool leafKnown = m_moveSourceLeaf
            && (m_leftLeaves.contains(m_moveSourceLeaf) || m_masterLeaves.contains(m_moveSourceLeaf)
                || m_rightLeaves.contains(m_moveSourceLeaf));
        if (!leafKnown || !m_moveSourceLeaf->windows().isEmpty()) {
            return false;
        }
        m_moveSourceLeaf->manage(window);
    }

    const int srcIdx = indexOfWindow(window);
    const int targetIdx = indexOfWindow(target);
    if (srcIdx < 0 || targetIdx < 0) {
        return false;
    }

    QList<QPointer<CustomTile>> flatLeaves = m_leftLeaves + m_masterLeaves + m_rightLeaves;
    QList<qreal> flatWeights = m_leftWeights + m_masterWeights + m_rightWeights;
    const int leftCount = m_leftLeaves.count();
    const int masterCount = m_masterLeaves.count();

    m_moveSourceLeaf.clear();
    m_moveSourceColumn = -1;
    m_moveSourceColumnIndex = -1;

    if (zone == DropZone::Swap) {
        CustomTile *srcLeaf = flatLeaves[srcIdx];
        CustomTile *targetLeaf = flatLeaves[targetIdx];
        targetLeaf->unmanage(target);
        targetLeaf->manage(window);
        srcLeaf->manage(target);
        reflow();
        return true;
    }

    // Left/Above = before the target in the flat (left/master/right) order;
    // Right/Below = after it. Re-split at the same per-column counts so a
    // window that crosses into a region joins that column.
    const bool after = (zone == DropZone::InsertRight || zone == DropZone::InsertBelow);
    int insertAt = after ? targetIdx + 1 : targetIdx;
    if (srcIdx < insertAt) {
        insertAt -= 1;
    }
    flatLeaves.move(srcIdx, insertAt);
    flatWeights.move(srcIdx, insertAt);

    m_leftLeaves = flatLeaves.mid(0, leftCount);
    m_leftWeights = flatWeights.mid(0, leftCount);
    m_masterLeaves = flatLeaves.mid(leftCount, masterCount);
    m_masterWeights = flatWeights.mid(leftCount, masterCount);
    m_rightLeaves = flatLeaves.mid(leftCount + masterCount);
    m_rightWeights = flatWeights.mid(leftCount + masterCount);
    reflow();
    return true;
}

void CenterTileLayoutEngine::cancelMoveWindow(Window *window)
{
    QPointer<CustomTile> sourceLeaf = m_moveSourceLeaf;
    const int srcCol = m_moveSourceColumn;
    m_moveSourceLeaf.clear();
    m_moveSourceColumn = -1;
    m_moveSourceColumnIndex = -1;

    if (!sourceLeaf) {
        return;
    }

    // Verify the source leaf is still in one of our columns.
    bool found = m_leftLeaves.contains(sourceLeaf)
              || m_masterLeaves.contains(sourceLeaf)
              || m_rightLeaves.contains(sourceLeaf);
    if (!found) {
        return;
    }

    // The window moved to another output. Remove the now-empty source leaf
    // so the original layout reflows and doesn't leave a gap behind.
    if (sourceLeaf->windows().isEmpty() || sourceLeaf->windows().contains(window)) {
        if (sourceLeaf->windows().contains(window)) {
            sourceLeaf->unmanage(window);
        }
        if (srcCol == 0) {
            m_leftLeaves.removeOne(sourceLeaf);
        } else if (srcCol == 1) {
            m_masterLeaves.removeOne(sourceLeaf);
        } else if (srcCol == 2) {
            m_rightLeaves.removeOne(sourceLeaf);
        }
        m_root->destroyChild(sourceLeaf);
        reflow();
    }
}

void CenterTileLayoutEngine::reflow()
{
    const int totalCount = totalLeafCount();
    if (!m_root || totalCount == 0) {
        return;
    }

    if (totalCount == 1) {
        // Single window takes the full screen.  Reset weights only for the
        // column that actually holds the window; the other two columns are
        // cleared so they are empty when the next window arrives.
        if (!m_masterLeaves.isEmpty() && m_masterLeaves.first()) {
            m_masterLeaves.first()->setRelativeGeometry(RectF(0, 0, 1, 1));
            m_masterWeights = {DefaultWeight};
            m_leftWeights.clear();
            m_rightWeights.clear();
        } else if (!m_leftLeaves.isEmpty() && m_leftLeaves.first()) {
            m_leftLeaves.first()->setRelativeGeometry(RectF(0, 0, 1, 1));
            m_leftWeights = {DefaultWeight};
            m_masterWeights.clear();
            m_rightWeights.clear();
        } else if (!m_rightLeaves.isEmpty() && m_rightLeaves.first()) {
            m_rightLeaves.first()->setRelativeGeometry(RectF(0, 0, 1, 1));
            m_rightWeights = {DefaultWeight};
            m_masterWeights.clear();
            m_leftWeights.clear();
        }
        Q_EMIT layoutChanged();
        return;
    }

    // Helper: distribute a contiguous run of leaves by their weights over a
    // column's full height.
    auto distribute = [](const QList<qreal> &weights, qreal columnH) {
        QList<qreal> heights(weights.size(), 0.0);
        if (weights.isEmpty() || columnH <= 0.0) {
            return heights;
        }
        qreal totalWeight = 0.0;
        for (qreal w : weights) {
            totalWeight += std::max(w, qreal(0.0001));
        }
        for (int i = 0; i < weights.size(); ++i) {
            heights[i] = (std::max(weights[i], qreal(0.0001)) / totalWeight) * columnH;
        }
        const qreal floor = columnH * MinWeightShare;
        for (int pass = 0; pass < 4; ++pass) {
            bool anyStarved = false;
            for (int i = 0; i < heights.size(); ++i) {
                if (heights[i] < floor) {
                    qreal deficit = floor - heights[i];
                    int donorIdx = -1;
                    qreal donorH = -1.0;
                    for (int j = 0; j < heights.size(); ++j) {
                        if (j != i && heights[j] > donorH) {
                            donorH = heights[j];
                            donorIdx = j;
                        }
                    }
                    if (donorIdx < 0 || donorH - deficit < floor) {
                        break;
                    }
                    heights[donorIdx] -= deficit;
                    heights[i] = floor;
                    anyStarved = true;
                }
            }
            if (!anyStarved) {
                break;
            }
        }
        return heights;
    };

    // Decide horizontal column widths.
    qreal leftWidth = 0.0;
    qreal masterWidth = 1.0;
    qreal rightWidth = 0.0;
    qreal leftStart = 0.0;
    qreal masterStart = 0.0;
    qreal rightStart = 0.0;

    const int leftCount = m_leftLeaves.count();
    const int masterCount = m_masterLeaves.count();
    const int rightCount = m_rightLeaves.count();

    // Defensive: keep every weight list in lock-step with its leaf list so a
    // reflow never accesses heights[i] out-of-bounds.  This guards against
    // weight/leaf mismatches from any future change that forgets to sync them.
    auto healColumn = [](QList<QPointer<CustomTile>> &leaves, QList<qreal> &weights) {
        while (weights.size() < leaves.size()) {
            weights.append(DefaultWeight);
        }
        while (weights.size() > leaves.size()) {
            weights.removeLast();
        }
    };
    healColumn(m_leftLeaves, m_leftWeights);
    healColumn(m_masterLeaves, m_masterWeights);
    healColumn(m_rightLeaves, m_rightWeights);

    if (leftCount > 0 || rightCount > 0) {
        constexpr qreal minColumnWidth = 0.15;
        const qreal requestedMasterWidth = m_masterRatio;
        const qreal requestedSideWidth = (1.0 - requestedMasterWidth) / 2.0;
        const qreal effectiveSideWidth = std::max(requestedSideWidth, minColumnWidth);
        masterWidth = std::max(0.0, 1.0 - 2.0 * effectiveSideWidth);
        leftWidth = effectiveSideWidth;
        rightWidth = effectiveSideWidth;
        leftStart = 0.0;
        masterStart = leftWidth;
        rightStart = leftWidth + masterWidth;
    }

    // Left stack
    if (leftCount > 0) {
        const QList<qreal> heights = distribute(m_leftWeights, 1.0);
        qreal y = 0.0;
        for (int i = 0; i < leftCount; ++i) {
            if (CustomTile *leaf = m_leftLeaves[i]) {
                leaf->setRelativeGeometry(RectF(leftStart, y, leftWidth, heights[i]));
                y += heights[i];
            }
        }
    }

    // Master column
    if (masterCount > 0) {
        const QList<qreal> heights = distribute(m_masterWeights, 1.0);
        qreal y = 0.0;
        for (int i = 0; i < masterCount; ++i) {
            if (CustomTile *leaf = m_masterLeaves[i]) {
                leaf->setRelativeGeometry(RectF(masterStart, y, masterWidth, heights[i]));
                y += heights[i];
            }
        }
    }

    // Right stack
    if (rightCount > 0) {
        const QList<qreal> heights = distribute(m_rightWeights, 1.0);
        qreal y = 0.0;
        for (int i = 0; i < rightCount; ++i) {
            if (CustomTile *leaf = m_rightLeaves[i]) {
                leaf->setRelativeGeometry(RectF(rightStart, y, rightWidth, heights[i]));
                y += heights[i];
            }
        }
    }

    Q_EMIT layoutChanged();
}

CenterTileLayoutEngine::ColumnSizes CenterTileLayoutEngine::columnSizes() const
{
    return {m_leftLeaves.count(), m_masterLeaves.count(), m_rightLeaves.count()};
}

int CenterTileLayoutEngine::columnIndexOfWindow(Window *window) const
{
    int idx;
    if (findInColumn(m_leftLeaves, window, idx)) {
        return 0;
    }
    if (findInColumn(m_masterLeaves, window, idx)) {
        return 1;
    }
    if (findInColumn(m_rightLeaves, window, idx)) {
        return 2;
    }
    return -1;
}

int CenterTileLayoutEngine::masterIndexOfWindow(Window *window) const
{
    int idx;
    if (findInColumn(m_masterLeaves, window, idx)) {
        return idx;
    }
    return -1;
}

void CenterTileLayoutEngine::setMasterRatio(qreal ratio)
{
    ratio = std::clamp(ratio, m_minMasterRatio, m_maxMasterRatio);
    if (qFuzzyCompare(m_masterRatio, ratio)) {
        return;
    }
    m_masterRatio = ratio;
    reflow();
    Q_EMIT masterRatioChanged(m_masterRatio);
}

void CenterTileLayoutEngine::setMasterSize(int count)
{
    count = std::clamp(count, m_minMasterSize, m_maxMasterSize);
    if (m_masterSize == count) {
        return;
    }

    // If shrinking the master, demote excess masters to alternating side stacks.
    while (m_masterLeaves.count() > count) {
        CustomTile *leaf = m_masterLeaves.takeLast();
        qreal weight = m_masterWeights.takeLast();
        if (m_nextWindowGoesRight) {
            m_rightLeaves.append(leaf);
            m_rightWeights.append(weight);
        } else {
            m_leftLeaves.append(leaf);
            m_leftWeights.append(weight);
        }
        m_nextWindowGoesRight = !m_nextWindowGoesRight;
    }

    m_masterSize = count;
    reflow();
}

void CenterTileLayoutEngine::adjustTileSize(Window *window, qreal weightDelta, Qt::Orientation axis)
{
    if (axis == Qt::Horizontal) {
        // Horizontal resize moves the centre column's share of the screen width.
        setMasterRatio(m_masterRatio + weightDelta);
        return;
    }

    // Vertical resize: change the leaf's weight inside its column.
    const int col = columnIndexOfWindow(window);
    QList<qreal> *weights = nullptr;
    if (col == 0) {
        weights = &m_leftWeights;
    } else if (col == 1) {
        weights = &m_masterWeights;
    } else if (col == 2) {
        weights = &m_rightWeights;
    }
    if (!weights) {
        return;
    }

    int idx;
    if (col == 0) {
        if (!findInColumn(m_leftLeaves, window, idx)) return;
    } else if (col == 1) {
        if (!findInColumn(m_masterLeaves, window, idx)) return;
    } else {
        if (!findInColumn(m_rightLeaves, window, idx)) return;
    }

    (*weights)[idx] = std::clamp((*weights)[idx] + weightDelta, m_minWeight, m_maxWeight);
    reflow();
}

QList<Window *> CenterTileLayoutEngine::windows() const
{
    QList<Window *> result;
    result.reserve(totalLeafCount());
    for (const auto &leaf : m_leftLeaves) {
        if (leaf && !leaf->windows().isEmpty()) {
            result.append(leaf->windows().first());
        }
    }
    for (const auto &leaf : m_masterLeaves) {
        if (leaf && !leaf->windows().isEmpty()) {
            result.append(leaf->windows().first());
        }
    }
    for (const auto &leaf : m_rightLeaves) {
        if (leaf && !leaf->windows().isEmpty()) {
            result.append(leaf->windows().first());
        }
    }
    return result;
}

Window *CenterTileLayoutEngine::primaryWindow() const
{
    // The primary window is the topmost master.
    if (!m_masterLeaves.isEmpty() && m_masterLeaves.first()) {
        const QList<Window *> ws = m_masterLeaves.first()->windows();
        if (!ws.isEmpty()) {
            return ws.first();
        }
    }
    return nullptr;
}

Window *CenterTileLayoutEngine::windowInDirection(Window *from, FocusDirection direction) const
{
    const QList<Window *> ws = windows();
    if (ws.isEmpty()) {
        return nullptr;
    }

    const int idx = from ? ws.indexOf(from) : -1;
    if (idx < 0) {
        return ws.first();
    }

    const int count = ws.count();
    const int leftCount = m_leftLeaves.count();
    const int masterCount = m_masterLeaves.count();
    const int rightCount = m_rightLeaves.count();
    const int leftEnd = leftCount;
    const int masterEnd = leftEnd + masterCount;
    const int rightEnd = masterEnd + rightCount;

    auto neighbour = [&](int candidateIdx) -> Window * {
        if (candidateIdx < 0 || candidateIdx >= count) {
            return nullptr;
        }
        return ws[candidateIdx];
    };

    switch (direction) {
    case FocusDirection::Up: {
        if (idx >= leftEnd && idx < masterEnd) {
            const int masterIdx = idx - leftEnd;
            if (masterIdx > 0) {
                return ws[leftEnd + masterIdx - 1];
            }
            if (leftCount > 0) {
                return ws[leftEnd - 1];
            }
            return nullptr;
        }
        if (idx < leftEnd) {
            return neighbour(idx - 1);
        }
        if (idx > masterEnd) {
            return neighbour(idx - 1);
        }
        if (idx == masterEnd && masterCount > 0) {
            return ws[leftEnd + masterCount - 1];
        }
        return nullptr;
    }
    case FocusDirection::Down: {
        if (idx < leftEnd) {
            if (idx < leftEnd - 1) {
                return ws[idx + 1];
            }
            if (masterCount > 0) {
                return ws[leftEnd];
            }
            if (rightCount > 0) {
                return ws[masterEnd];
            }
            return nullptr;
        }
        if (idx >= leftEnd && idx < masterEnd) {
            const int masterIdx = idx - leftEnd;
            if (masterIdx < masterCount - 1) {
                return ws[leftEnd + masterIdx + 1];
            }
            if (rightCount > 0) {
                return ws[masterEnd];
            }
            return nullptr;
        }
        return neighbour(idx + 1);
    }
    case FocusDirection::Left: {
        if (idx >= masterEnd && idx < rightEnd) {
            if (masterCount > 0) {
                return ws[leftEnd];
            }
            if (leftCount > 0) {
                return ws[leftEnd - 1];
            }
            return nullptr;
        }
        if (idx >= leftEnd && idx < masterEnd) {
            if (leftCount > 0) {
                return ws[leftEnd - 1];
            }
            return nullptr;
        }
        return nullptr;
    }
    case FocusDirection::Right: {
        if (idx < leftEnd) {
            if (masterCount > 0) {
                return ws[leftEnd];
            }
            if (rightCount > 0) {
                return ws[masterEnd];
            }
            return nullptr;
        }
        if (idx >= leftEnd && idx < masterEnd) {
            if (rightCount > 0) {
                return ws[masterEnd];
            }
            return nullptr;
        }
        return nullptr;
    }
    }
    return nullptr;
}

int CenterTileLayoutEngine::indexOfWindow(Window *window) const
{
    // Left stack
    for (int i = 0; i < m_leftLeaves.count(); ++i) {
        if (m_leftLeaves[i] && m_leftLeaves[i]->windows().contains(window)) {
            return i;
        }
    }
    // Master column
    const int leftOffset = m_leftLeaves.count();
    for (int i = 0; i < m_masterLeaves.count(); ++i) {
        if (m_masterLeaves[i] && m_masterLeaves[i]->windows().contains(window)) {
            return leftOffset + i;
        }
    }
    // Right stack
    const int masterOffset = leftOffset + m_masterLeaves.count();
    for (int i = 0; i < m_rightLeaves.count(); ++i) {
        if (m_rightLeaves[i] && m_rightLeaves[i]->windows().contains(window)) {
            return masterOffset + i;
        }
    }
    return -1;
}

bool CenterTileLayoutEngine::findInColumn(const QList<QPointer<CustomTile>> &leaves,
                                           Window *window, int &idx) const
{
    for (int i = 0; i < leaves.count(); ++i) {
        if (leaves[i] && leaves[i]->windows().contains(window)) {
            idx = i;
            return true;
        }
    }
    return false;
}

void CenterTileLayoutEngine::interactiveResizeEnded(Window *window, const RectF &before, const RectF &after,
                                                     Qt::Edge edge, const QSizeF &outputSize)
{
    if (!window || before.size() == QSizeF() || after.size() == QSizeF()
        || outputSize.width() <= 0 || outputSize.height() <= 0) {
        return;
    }

    const int col = columnIndexOfWindow(window);
    if (col < 0) {
        return;
    }

    // --- Horizontal resize: move both dividers symmetrically (adjust masterRatio) ---
    if (edge == Qt::LeftEdge || edge == Qt::RightEdge) {
        // Screen-edge checks
        if (col == 0 && edge == Qt::LeftEdge)  return;
        if (col == 2 && edge == Qt::RightEdge) return;

        const qreal edgeDelta = (edge == Qt::RightEdge)
            ? (after.right() - before.right())
            : (before.left() - after.left());

        const qreal sign = (col == 1) ? 1.0 : -1.0;
        const qreal ratioDelta = sign * edgeDelta / outputSize.width();
        if (qFuzzyIsNull(ratioDelta)) {
            return;
        }
        setMasterRatio(m_masterRatio + std::clamp(ratioDelta, qreal(-0.5), qreal(0.5)));
        return;
    }

    // --- Vertical resize: push / pull within the window's column ---

    QList<qreal> *weights = nullptr;
    if (col == 0) {
        weights = &m_leftWeights;
    } else if (col == 1) {
        weights = &m_masterWeights;
    } else {
        weights = &m_rightWeights;
    }
    if (!weights || weights->isEmpty()) {
        return;
    }

    int colIdx;
    if (col == 0) {
        if (!findInColumn(m_leftLeaves, window, colIdx)) return;
    } else if (col == 1) {
        if (!findInColumn(m_masterLeaves, window, colIdx)) return;
    } else {
        if (!findInColumn(m_rightLeaves, window, colIdx)) return;
    }

    const int neighbourIdx = (edge == Qt::BottomEdge) ? colIdx + 1 : colIdx - 1;
    if (neighbourIdx < 0 || neighbourIdx >= weights->count()) {
        return;
    }

    const qreal weightDelta = (edge == Qt::BottomEdge)
        ? (after.bottom() - before.bottom()) / outputSize.height()
        : (before.top() - after.top()) / outputSize.height();
    if (qFuzzyIsNull(weightDelta)) {
        return;
    }

    qreal newSelf = std::clamp((*weights)[colIdx] + weightDelta, m_minWeight, m_maxWeight);
    qreal actualDelta = newSelf - (*weights)[colIdx];
    qreal newNeighbour = std::clamp((*weights)[neighbourIdx] - actualDelta, m_minWeight, m_maxWeight);
    if (qFuzzyCompare((*weights)[colIdx], newSelf) && qFuzzyCompare((*weights)[neighbourIdx], newNeighbour)) {
        return;
    }

    (*weights)[colIdx] = newSelf;
    (*weights)[neighbourIdx] = newNeighbour;
    reflow();
}

} // namespace KWin
