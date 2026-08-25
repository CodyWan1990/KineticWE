/*
    SPDX-FileCopyrightText: 2025 The KineticWE Contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "sessionlockv1integration.h"
#include "core/output.h"
#include "sessionlockv1window.h"
#include "wayland/output.h"
#include "wayland/session_lock_v1.h"
#include "wayland_server.h"

namespace KWin
{

SessionLockV1Integration::SessionLockV1Integration(QObject *parent)
    : WaylandShellIntegration(parent)
{
    m_manager = new SessionLockManagerV1Interface(waylandServer()->display(), this);
    connect(m_manager, &SessionLockManagerV1Interface::lockCreated, this, [this](SessionLockV1Interface *lock) {
        connect(lock, &SessionLockV1Interface::surfaceCreated, this, &SessionLockV1Integration::createWindow);
    });
}

SessionLockManagerV1Interface *SessionLockV1Integration::manager() const
{
    return m_manager;
}

void SessionLockV1Integration::createWindow(SessionLockSurfaceV1Interface *shellSurface)
{
    OutputInterface *output = shellSurface->output();
    if (!output || output->isRemoved()) {
        return;
    }
    Q_EMIT windowCreated(new LockScreenWindow(shellSurface, output->handle()));
}

} // namespace KWin

#include "moc_sessionlockv1integration.cpp"