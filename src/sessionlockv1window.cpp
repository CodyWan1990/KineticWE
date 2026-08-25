/*
    SPDX-FileCopyrightText: 2025 The KineticWE Contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "sessionlockv1window.h"
#include "core/output.h"
#include "core/pixelgrid.h"
#include "wayland/session_lock_v1.h"
#include "wayland/surface.h"
#include "wayland_server.h"
#include "workspace.h"

namespace KWin
{

LockScreenWindow::LockScreenWindow(SessionLockSurfaceV1Interface *shellSurface, LogicalOutput *output)
    : WaylandWindow(shellSurface->surface())
    , m_shellSurface(shellSurface)
    , m_output(output)
{
    setOutput(output);
    setMoveResizeOutput(output);
    setSkipSwitcher(true);
    setSkipPager(true);
    setSkipTaskbar(true);

    connect(shellSurface, &SessionLockSurfaceV1Interface::aboutToBeDestroyed,
            this, &LockScreenWindow::destroyWindow);
    connect(shellSurface->surface(), &SurfaceInterface::aboutToBeDestroyed,
            this, &LockScreenWindow::destroyWindow);
    connect(shellSurface->surface(), &SurfaceInterface::committed,
            this, &LockScreenWindow::handleCommitted);
    connect(shellSurface->surface(), &SurfaceInterface::sizeChanged,
            this, &LockScreenWindow::handleSizeChanged);
    connect(output, &LogicalOutput::geometryChanged,
            this, &LockScreenWindow::handleOutputGeometryChanged);
    connect(workspace(), &Workspace::outputRemoved,
            this, [this](LogicalOutput *removed) {
                if (removed == m_output) {
                    destroyWindow();
                }
            });
}

LockScreenWindow::~LockScreenWindow() = default;

SessionLockSurfaceV1Interface *LockScreenWindow::shellSurface() const
{
    return m_shellSurface;
}

LogicalOutput *LockScreenWindow::output() const
{
    return m_output;
}

void LockScreenWindow::destroyWindow()
{
    if (isDeleted()) {
        return;
    }
    m_shellSurface->disconnect(this);
    m_shellSurface->surface()->disconnect(this);
    m_output->disconnect(this);
    workspace()->disconnect(this);

    markAsDeleted();
    Q_EMIT closed();

    StackingUpdatesBlocker blocker(workspace());
    cleanGrouping();
    waylandServer()->removeWindow(this);
    unref();
}

void LockScreenWindow::handleCommitted()
{
    if (surface()->buffer()) {
        markAsMapped();
    }
}

void LockScreenWindow::handleSizeChanged()
{
    updateGeometry(RectF(m_output->geometry().topLeft(), clientSizeToFrameSize(surface()->size())));
}

void LockScreenWindow::handleOutputGeometryChanged()
{
    handleSizeChanged();
}

void LockScreenWindow::closeWindow()
{
    // Lock screen windows are never closed by the window manager.
}

void LockScreenWindow::moveResizeInternal(const RectF &rect, MoveResizeMode mode)
{
    // Lock screen windows are neither movable nor resizable.
    Q_UNUSED(rect)
    Q_UNUSED(mode)
}

void LockScreenWindow::doSetNextTargetScale()
{
    if (isDeleted()) {
        return;
    }
    surface()->setPreferredBufferScale(nextTargetScale());
    setTargetScale(nextTargetScale());
    handleSizeChanged();
}

} // namespace KWin

#include "moc_sessionlockv1window.cpp"