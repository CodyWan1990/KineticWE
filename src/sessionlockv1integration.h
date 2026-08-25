/*
    SPDX-FileCopyrightText: 2025 The KineticWE Contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "waylandshellintegration.h"

namespace KWin
{

class SessionLockManagerV1Interface;
class SessionLockSurfaceV1Interface;

/**
 * @brief Creates the ext-session-lock-v1 protocol implementation and turns lock
 * surfaces into compositor windows.
 */
class SessionLockV1Integration : public WaylandShellIntegration
{
    Q_OBJECT

public:
    explicit SessionLockV1Integration(QObject *parent = nullptr);

    SessionLockManagerV1Interface *manager() const;

private:
    void createWindow(SessionLockSurfaceV1Interface *shellSurface);

    SessionLockManagerV1Interface *m_manager;
};

} // namespace KWin