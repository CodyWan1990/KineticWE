/*
    KWin - the KDE window manager
    SPDX-FileCopyrightText: 2026 KWin Tiling Fork
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "monoclelayoutengine.h"
#include "customtile.h"
#include "window.h"

#include <algorithm>

namespace KWin
{

MonocleLayoutEngine::MonocleLayoutEngine(QObject *parent)
    : LayoutEngine(parent)
{
}

MonocleLayoutEngine::~MonocleLayoutEngine() = default;

void MonocleLayoutEngine::attach(RootTile *root)
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
}

void MonocleLayoutEngine::addWindow(Window *window)
{
    if (!m_root) {
        return;
    }

    // Every window gets a leaf that fills the entire root tile area.
    // New windows are appended to the end of the stack (behind existing ones).
    CustomTile *leaf = m_root->createChildAt(RectF(0, 0, 1, 1), Tile::LayoutDirection::Floating, m_leaves.count());
    m_leaves.append(leaf);
    if (!leaf->manage(window)) {
        qWarning() << "MonocleLayoutEngine: failed to manage window" << window->caption() << "in leaf";
        m_root->destroyChild(leaf);
        m_leaves.removeLast();
        return;
    }

    reflow();
}

void MonocleLayoutEngine::removeWindow(Window *window)
{
    const int idx = indexOfWindow(window);
    if (idx < 0) {
        return;
    }

    CustomTile *leaf = m_leaves.takeAt(idx);
    leaf->unmanage(window);
    m_root->destroyChild(leaf);

    reflow();
}

void MonocleLayoutEngine::pruneEmptyLeaves()
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
        if (leaf) {
            m_root->destroyChild(leaf);
        }
        m_leaves.removeAt(i);
        changed = true;
    }

    if (changed) {
        reflow();
    }
}

void MonocleLayoutEngine::moveWindow(Window *window, int delta)
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
    reflow();
}

void MonocleLayoutEngine::beginMoveWindow(Window *window)
{
    m_moveSourceIndex = indexOfWindow(window);
    m_moveSourceLeaf = (m_moveSourceIndex >= 0 && m_moveSourceIndex < m_leaves.count())
        ? m_leaves[m_moveSourceIndex] : nullptr;
}

bool MonocleLayoutEngine::endMoveWindow(Window *window, Window *target)
{
    if (!window || !m_moveSourceLeaf || m_moveSourceIndex < 0) {
        return false;
    }

    if (target) {
        // Swap positions: put the dragged window where the target is.
        const int targetIdx = indexOfWindow(target);
        if (targetIdx >= 0) {
            m_leaves.move(m_moveSourceIndex, targetIdx);
            reflow();
        }
    }
    // If no target, the window stays where it was (already at m_moveSourceIndex).

    m_moveSourceLeaf = nullptr;
    m_moveSourceIndex = -1;
    return true;
}

void MonocleLayoutEngine::cancelMoveWindow(Window *window)
{
    Q_UNUSED(window)
    m_moveSourceLeaf = nullptr;
    m_moveSourceIndex = -1;
}

void MonocleLayoutEngine::reflow()
{
    if (!m_root) {
        return;
    }

    // All leaves get the full root area. Only z-ordering differs.
    for (CustomTile *leaf : m_leaves) {
        if (leaf) {
            leaf->setRelativeGeometry(RectF(0, 0, 1, 1));
        }
    }
}

QList<Window *> MonocleLayoutEngine::windows() const
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

Window *MonocleLayoutEngine::primaryWindow() const
{
    if (m_leaves.isEmpty() || !m_leaves.first()) {
        return nullptr;
    }
    const QList<Window *> ws = m_leaves.first()->windows();
    return ws.isEmpty() ? nullptr : ws.first();
}

Window *MonocleLayoutEngine::windowInDirection(Window *from, FocusDirection direction) const
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
    case FocusDirection::Up:
        // Cycle backward: wrap around to the end if at the first window.
        if (idx > 0) {
            return ws[idx - 1];
        }
        return ws.last();
    case FocusDirection::Right:
    case FocusDirection::Down:
        // Cycle forward: wrap around to the beginning if at the last window.
        if (idx < ws.count() - 1) {
            return ws[idx + 1];
        }
        return ws.first();
    }

    return nullptr;
}

int MonocleLayoutEngine::indexOfWindow(Window *window) const
{
    for (int i = 0; i < m_leaves.count(); ++i) {
        if (m_leaves[i] && m_leaves[i]->windows().contains(window)) {
            return i;
        }
    }
    return -1;
}

} // namespace KWin
