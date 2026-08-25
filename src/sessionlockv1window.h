/*
    SPDX-FileCopyrightText: 2025 The KineticWE Contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "waylandwindow.h"

namespace KWin
{

class LogicalOutput;
class SessionLockSurfaceV1Interface;

/**
 * @brief A window representing an ext_session_lock_surface_v1.
 *
 * Lock screen windows are fullscreen surfaces that remain visible and receive
 * input while the session is locked. They are destroyed when the lock surface
 * is destroyed.
 */
class KWIN_EXPORT LockScreenWindow : public WaylandWindow
{
    Q_OBJECT

public:
    LockScreenWindow(SessionLockSurfaceV1Interface *shellSurface, LogicalOutput *output);
    ~LockScreenWindow() override;

    SessionLockSurfaceV1Interface *shellSurface() const;
    LogicalOutput *output() const;

    bool isLockScreen() const override
    {
        return true;
    }
    bool isCloseable() const override
    {
        return false;
    }
    bool isMovable() const override
    {
        return false;
    }
    bool isMovableAcrossScreens() const override
    {
        return false;
    }
    bool isResizable() const override
    {
        return false;
    }
    bool isPlaceable() const override
    {
        return false;
    }
    bool wantsInput() const override
    {
        return readyForPainting();
    }
    bool acceptsFocus() const override
    {
        return true;
    }
    WindowType windowType() const override
    {
        return WindowType::Normal;
    }
    Layer belongsToLayer() const override
    {
        return OverlayLayer;
    }

    void destroyWindow();
    void closeWindow() override;
    void moveResizeInternal(const RectF &rect, MoveResizeMode mode) override;

protected:
    void doSetNextTargetScale() override;

private:
    void handleCommitted();
    void handleSizeChanged();
    void handleOutputGeometryChanged();

    SessionLockSurfaceV1Interface *m_shellSurface;
    LogicalOutput *m_output;
};

} // namespace KWin