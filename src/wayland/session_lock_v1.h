/*
    SPDX-FileCopyrightText: 2025 The KineticWE Contributors

    SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL
*/

#pragma once

#include "kwin_export.h"

#include <QList>
#include <QObject>
#include <memory>

struct wl_resource;

namespace KWin
{

class Display;
class LogicalOutput;
class OutputInterface;
class SurfaceInterface;
class SurfaceRole;

class SessionLockManagerV1InterfacePrivate;
class SessionLockV1InterfacePrivate;
class SessionLockSurfaceV1InterfacePrivate;

/**
 * @brief Represents a single lock surface created via ext_session_lock_v1.get_lock_surface.
 *
 * Each lock surface is associated with a wl_surface and a wl_output. The compositor
 * displays these surfaces while the session is locked.
 */
class KWIN_EXPORT SessionLockSurfaceV1Interface : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY(SessionLockSurfaceV1Interface)

public:
    ~SessionLockSurfaceV1Interface() override;

    SurfaceInterface *surface() const;
    OutputInterface *output() const;

    /**
     * @returns true if the surface has committed a buffer, i.e. the surface is mapped.
     */
    bool isMapped() const;

Q_SIGNALS:
    void aboutToBeDestroyed();
    void mappedChanged();

private:
    friend class SessionLockV1InterfacePrivate;
    explicit SessionLockSurfaceV1Interface(SurfaceInterface *surface, OutputInterface *output);
    std::unique_ptr<SessionLockSurfaceV1InterfacePrivate> d;
};

/**
 * @brief Represents an ext_session_lock_v1 object.
 *
 * Created by the ext_session_lock_manager_v1.lock request. The compositor sends either
 * the locked or finished event in response.
 */
class KWIN_EXPORT SessionLockV1Interface : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY(SessionLockV1Interface)

public:
    ~SessionLockV1Interface() override;

    /**
     * @returns true if the locked event has been sent on this session lock.
     */
    bool isLocked() const;

    QList<SessionLockSurfaceV1Interface *> surfaces() const;

    /**
     * @returns the role assigned to lock surfaces.
     */
    static SurfaceRole *role();

Q_SIGNALS:
    void aboutToBeDestroyed();
    void surfaceCreated(SessionLockSurfaceV1Interface *surface);

private:
    friend class SessionLockManagerV1InterfacePrivate;
    explicit SessionLockV1Interface();
    std::unique_ptr<SessionLockV1InterfacePrivate> d;
};

/**
 * @brief Implements the ext_session_lock_manager_v1 global.
 *
 * The manager keeps track of the active session lock, if any, and drives the compositor
 * lock state. When the session is locked normal windows are hidden and input is
 * restricted to the lock surfaces.
 */
class KWIN_EXPORT SessionLockManagerV1Interface : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY(SessionLockManagerV1Interface)

public:
    explicit SessionLockManagerV1Interface(Display *display, QObject *parent = nullptr);
    ~SessionLockManagerV1Interface() override;

    /**
     * @returns true if the session is currently locked or the lock is being acquired.
     */
    bool isSessionLocked() const;

    /**
     * @returns the active session lock, or @c nullptr if the session is not locked.
     */
    SessionLockV1Interface *activeLock() const;

Q_SIGNALS:
    void lockedChanged();
    void lockCreated(SessionLockV1Interface *lock);

private:
    friend class SessionLockManagerV1InterfacePrivate;
    std::unique_ptr<SessionLockManagerV1InterfacePrivate> d;
};

} // namespace KWin