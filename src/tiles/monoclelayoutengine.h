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
 * Monocle (fullscreen-stack) layout.
 *
 * Every tiled window fills the entire tiling area. Only the focused window
 * is visible; the rest are stacked behind it. Focus-left and focus-right
 * cycle through the stack, bringing the newly focused window to the front.
 *
 * Inspired by dwm's monocle layout and the Truetile KWin script.
 */
class KWIN_EXPORT MonocleLayoutEngine : public LayoutEngine
{
    Q_OBJECT

public:
    explicit MonocleLayoutEngine(QObject *parent = nullptr);
    ~MonocleLayoutEngine() override;

    LayoutKind layoutKind() const override { return LayoutKind::Monocle; }
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

private:
    int indexOfWindow(Window *window) const;

    QPointer<RootTile> m_root;
    QList<QPointer<CustomTile>> m_leaves;

    // Source leaf remembered during an interactive drag-move.
    QPointer<CustomTile> m_moveSourceLeaf;
    int m_moveSourceIndex = -1;
};

} // namespace KWin
