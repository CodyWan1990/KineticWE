/*
    SPDX-FileCopyrightText: 2025 The KineticWE Contributors

    SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL
*/

#include "session_lock_v1.h"
#include "core/backendoutput.h"
#include "core/output.h"
#include "core/renderloop.h"
#include "display.h"
#include "output.h"
#include "surface.h"
#include "utils/common.h"
#include "workspace.h"

#include "qwayland-server-ext-session-lock-v1.h"

#include <QPointer>
#include <QSet>
#include <QTimer>

namespace KWin
{

static const int s_version = 1;

class SessionLockV1InterfacePrivate;
class SessionLockManagerV1InterfacePrivate;

class SessionLockSurfaceV1InterfacePrivate : public QtWaylandServer::ext_session_lock_surface_v1
{
public:
    SessionLockSurfaceV1InterfacePrivate(SessionLockSurfaceV1Interface *q);

    void sendConfigure();

    SessionLockSurfaceV1Interface *q;
    QPointer<SurfaceInterface> surface;
    QPointer<OutputInterface> output;
    SessionLockV1InterfacePrivate *lock = nullptr;
    bool mapped = false;
    bool acked = false;
    QSize ackedSize;
    quint32 pendingConfigureSerial = 0;

protected:
    void ext_session_lock_surface_v1_destroy(Resource *resource) override;
    void ext_session_lock_surface_v1_ack_configure(Resource *resource, uint32_t serial) override;
    void ext_session_lock_surface_v1_destroy_resource(Resource *resource) override;
};

class SessionLockV1InterfacePrivate : public QtWaylandServer::ext_session_lock_v1
{
public:
    SessionLockV1InterfacePrivate(SessionLockV1Interface *q);

    void sendLocked();
    void sendFinished();

    SessionLockV1Interface *q;
    SessionLockManagerV1InterfacePrivate *manager = nullptr;
    bool locked = false;
    bool finished = false;
    QList<SessionLockSurfaceV1Interface *> surfaces;

protected:
    void ext_session_lock_v1_get_lock_surface(Resource *resource,
                                              uint32_t id,
                                              struct ::wl_resource *surface_resource,
                                              struct ::wl_resource *output_resource) override;
    void ext_session_lock_v1_destroy(Resource *resource) override;
    void ext_session_lock_v1_unlock_and_destroy(Resource *resource) override;
    void ext_session_lock_v1_destroy_resource(Resource *resource) override;
};

class SessionLockManagerV1InterfacePrivate : public QtWaylandServer::ext_session_lock_manager_v1
{
public:
    SessionLockManagerV1InterfacePrivate(SessionLockManagerV1Interface *q, Display *display);

    void engageLock(SessionLockV1Interface *lock);
    void disengageLock();
    void maybeSendLocked();
    void handleFramePresented(LogicalOutput *output);
    void checkBlankedOutputs();
    bool hasMappedLockSurfaceForOutput(LogicalOutput *output) const;
    void trackOutput(LogicalOutput *output);

    SessionLockManagerV1Interface *q;
    Display *display;
    SessionLockV1Interface *activeLock = nullptr;
    bool sessionLocked = false;
    bool lockSent = false;
    QSet<LogicalOutput *> pendingOutputs;
    QTimer graceTimer;
    QTimer timeoutTimer;

protected:
    void ext_session_lock_manager_v1_destroy(Resource *resource) override;
    void ext_session_lock_manager_v1_lock(Resource *resource, uint32_t id) override;
};

SessionLockSurfaceV1InterfacePrivate::SessionLockSurfaceV1InterfacePrivate(SessionLockSurfaceV1Interface *q)
    : q(q)
{
}

SessionLockV1InterfacePrivate::SessionLockV1InterfacePrivate(SessionLockV1Interface *q)
    : q(q)
{
}

void SessionLockSurfaceV1InterfacePrivate::sendConfigure()
{
    if (!output || output->isRemoved() || !surface) {
        return;
    }
    const QSize size = output->handle()->geometryF().size().toSize();
    pendingConfigureSerial = output->display()->nextSerial();
    acked = false;
    send_configure(resource()->handle, pendingConfigureSerial, size.width(), size.height());
}

void SessionLockSurfaceV1InterfacePrivate::ext_session_lock_surface_v1_destroy(Resource *resource)
{
    wl_resource_destroy(resource->handle);
}

void SessionLockSurfaceV1InterfacePrivate::ext_session_lock_surface_v1_ack_configure(Resource *resource, uint32_t serial)
{
    if (serial != pendingConfigureSerial) {
        wl_resource_post_error(resource->handle, error_invalid_serial, "invalid serial provided in ack_configure");
        return;
    }
    if (!surface) {
        return;
    }
    const QSize size = output ? output->handle()->geometryF().size().toSize() : QSize();
    acked = true;
    ackedSize = size;
}

void SessionLockSurfaceV1InterfacePrivate::ext_session_lock_surface_v1_destroy_resource(Resource *resource)
{
    if (lock) {
        lock->surfaces.removeAll(q);
    }
    Q_EMIT q->aboutToBeDestroyed();
    delete q;
}

SessionLockSurfaceV1Interface::SessionLockSurfaceV1Interface(SurfaceInterface *surface, OutputInterface *output)
    : d(std::make_unique<SessionLockSurfaceV1InterfacePrivate>(this))
{
    d->surface = surface;
    d->output = output;
}

SessionLockSurfaceV1Interface::~SessionLockSurfaceV1Interface() = default;

SurfaceInterface *SessionLockSurfaceV1Interface::surface() const
{
    return d->surface;
}

OutputInterface *SessionLockSurfaceV1Interface::output() const
{
    return d->output;
}

bool SessionLockSurfaceV1Interface::isMapped() const
{
    return d->mapped;
}

void SessionLockV1InterfacePrivate::sendLocked()
{
    if (locked) {
        return;
    }
    locked = true;
    send_locked(resource()->handle);
}

void SessionLockV1InterfacePrivate::sendFinished()
{
    if (finished) {
        return;
    }
    finished = true;
    send_finished(resource()->handle);
}

void SessionLockV1InterfacePrivate::ext_session_lock_v1_get_lock_surface(Resource *resource,
                                                                         uint32_t id,
                                                                         struct ::wl_resource *surface_resource,
                                                                         struct ::wl_resource *output_resource)
{
    SurfaceInterface *surface = SurfaceInterface::get(surface_resource);
    OutputInterface *output = OutputInterface::get(output_resource);
    if (!surface || !output) {
        wl_resource_post_error(resource->handle, error_role, "invalid surface or output");
        return;
    }

    if (surface->role()) {
        wl_resource_post_error(resource->handle, error_role, "given wl_surface already has a role");
        return;
    }
    if (surface->buffer()) {
        wl_resource_post_error(resource->handle, error_already_constructed, "given wl_surface already has a buffer attached or committed");
        return;
    }
    for (SessionLockSurfaceV1Interface *lockSurface : surfaces) {
        if (lockSurface->output() == output) {
            wl_resource_post_error(resource->handle, error_duplicate_output, "given output already has a lock surface");
            return;
        }
    }
    surface->setRole(SessionLockV1Interface::role());

    auto lockSurface = new SessionLockSurfaceV1Interface(surface, output);
    lockSurface->d->lock = this;
    surfaces.append(lockSurface);
    lockSurface->d->add(resource->client(), id, resource->version());

    QObject::connect(surface, &SurfaceInterface::committed, lockSurface, [lockSurface]() {
        SessionLockSurfaceV1InterfacePrivate *d = lockSurface->d.get();
        if (!d->surface || !d->output) {
            return;
        }
        if (!d->acked) {
            wl_resource_post_error(d->resource()->handle, SessionLockSurfaceV1InterfacePrivate::error_commit_before_first_ack, "surface committed before first ack_configure");
            return;
        }
        if (!d->surface->buffer()) {
            wl_resource_post_error(d->resource()->handle, SessionLockSurfaceV1InterfacePrivate::error_null_buffer, "surface committed with a null buffer");
            return;
        }
        const QSize surfaceSize = d->surface->size().toSize();
        if (!d->ackedSize.isEmpty() && (surfaceSize.width() != d->ackedSize.width() || surfaceSize.height() != d->ackedSize.height())) {
            wl_resource_post_error(d->resource()->handle, SessionLockSurfaceV1InterfacePrivate::error_dimensions_mismatch, "failed to match ack'd width/height");
            return;
        }
        if (!d->mapped) {
            d->mapped = true;
            Q_EMIT lockSurface->mappedChanged();
        }
    });

    QObject::connect(surface, &SurfaceInterface::aboutToBeDestroyed, lockSurface, [lockSurface]() {
        // d->surface is a QPointer and becomes null automatically. Remove the
        // wrapper from the lock's list so stale entries never dangle; the wrapper
        // itself stays alive until the client destroys its session lock surface.
        if (SessionLockV1InterfacePrivate *lock = lockSurface->d->lock) {
            lock->surfaces.removeAll(lockSurface);
        }
    });
    QObject::connect(output, &OutputInterface::removed, lockSurface, [lockSurface]() {
        if (SessionLockV1InterfacePrivate *lock = lockSurface->d->lock) {
            lock->surfaces.removeAll(lockSurface);
        }
    });
    QObject::connect(output->handle(), &LogicalOutput::geometryChanged, lockSurface, [lockSurface]() {
        lockSurface->d->sendConfigure();
    });

    lockSurface->d->sendConfigure();
    Q_EMIT q->surfaceCreated(lockSurface);
}

void SessionLockV1InterfacePrivate::ext_session_lock_v1_destroy(Resource *resource)
{
    if (locked) {
        wl_resource_post_error(resource->handle, error_invalid_destroy, "attempted to destroy session lock while locked");
        return;
    }
    if (manager && manager->activeLock == q) {
        manager->disengageLock();
    }
    wl_resource_destroy(resource->handle);
}

void SessionLockV1InterfacePrivate::ext_session_lock_v1_unlock_and_destroy(Resource *resource)
{
    // Be lenient here: some clients (e.g. Noctalia during teardown) issue
    // unlock_and_destroy before the locked event was sent. Treat that as a
    // cancelled lock acquisition rather than a protocol error.
    if (manager && manager->activeLock == q) {
        manager->disengageLock();
    }
    wl_resource_destroy(resource->handle);
}

void SessionLockV1InterfacePrivate::ext_session_lock_v1_destroy_resource(Resource *resource)
{
    for (SessionLockSurfaceV1Interface *lockSurface : surfaces) {
        lockSurface->d->lock = nullptr;
    }
    if (manager && manager->activeLock == q) {
        manager->disengageLock();
    }
    Q_EMIT q->aboutToBeDestroyed();
    delete q;
}

SessionLockV1Interface::SessionLockV1Interface()
    : d(std::make_unique<SessionLockV1InterfacePrivate>(this))
{
}

SessionLockV1Interface::~SessionLockV1Interface() = default;

bool SessionLockV1Interface::isLocked() const
{
    return d->locked;
}

QList<SessionLockSurfaceV1Interface *> SessionLockV1Interface::surfaces() const
{
    return d->surfaces;
}

SurfaceRole *SessionLockV1Interface::role()
{
    static SurfaceRole role(QByteArrayLiteral("session_lock_surface_v1"));
    return &role;
}

SessionLockManagerV1InterfacePrivate::SessionLockManagerV1InterfacePrivate(SessionLockManagerV1Interface *q, Display *display)
    : QtWaylandServer::ext_session_lock_manager_v1(*display, s_version)
    , q(q)
    , display(display)
{
    graceTimer.setSingleShot(true);
    graceTimer.setInterval(500);
    timeoutTimer.setSingleShot(true);
    timeoutTimer.setInterval(2000);

    QObject::connect(&graceTimer, &QTimer::timeout, q, [this]() {
        checkBlankedOutputs();
    });
    QObject::connect(&timeoutTimer, &QTimer::timeout, q, [this]() {
        maybeSendLocked();
    });
}

void SessionLockManagerV1InterfacePrivate::engageLock(SessionLockV1Interface *lock)
{
    if (activeLock) {
        // Another session lock is already active, refuse this one.
        lock->d->sendFinished();
        return;
    }
    activeLock = lock;
    lock->d->manager = this;
    lockSent = false;
    pendingOutputs.clear();

    for (LogicalOutput *output : workspace()->outputs()) {
        trackOutput(output);
    }
    QObject::connect(workspace(), &Workspace::outputAdded, q, [this](LogicalOutput *output) {
        trackOutput(output);
    });
    QObject::connect(workspace(), &Workspace::outputRemoved, q, [this](LogicalOutput *output) {
        pendingOutputs.remove(output);
        maybeSendLocked();
    });

    if (!sessionLocked) {
        sessionLocked = true;
        Q_EMIT q->lockedChanged();
    }
    Q_EMIT q->lockCreated(lock);

    graceTimer.start();
    timeoutTimer.start();
}

void SessionLockManagerV1InterfacePrivate::disengageLock()
{
    graceTimer.stop();
    timeoutTimer.stop();
    QObject::disconnect(workspace(), &Workspace::outputAdded, q, nullptr);
    QObject::disconnect(workspace(), &Workspace::outputRemoved, q, nullptr);
    pendingOutputs.clear();
    if (activeLock) {
        activeLock->d->manager = nullptr;
        activeLock = nullptr;
    }
    lockSent = false;
    if (sessionLocked) {
        sessionLocked = false;
        Q_EMIT q->lockedChanged();
    }
}

void SessionLockManagerV1InterfacePrivate::trackOutput(LogicalOutput *output)
{
    pendingOutputs.insert(output);
    QObject::connect(output->backendOutput()->renderLoop(), &RenderLoop::framePresented,
            q, [this, output](RenderLoop *, std::chrono::nanoseconds, PresentationMode) {
                handleFramePresented(output);
            });
}

bool SessionLockManagerV1InterfacePrivate::hasMappedLockSurfaceForOutput(LogicalOutput *output) const
{
    if (!activeLock) {
        return false;
    }
    for (SessionLockSurfaceV1Interface *lockSurface : activeLock->d->surfaces) {
        if (lockSurface->isMapped() && lockSurface->output() && lockSurface->output()->handle() == output) {
            return true;
        }
    }
    return false;
}

void SessionLockManagerV1InterfacePrivate::handleFramePresented(LogicalOutput *output)
{
    if (!pendingOutputs.contains(output)) {
        return;
    }
    if (!hasMappedLockSurfaceForOutput(output)) {
        return;
    }
    pendingOutputs.remove(output);
    maybeSendLocked();
}

void SessionLockManagerV1InterfacePrivate::checkBlankedOutputs()
{
    // Any output that still has no rendered lock surface counts as blanked: the
    // compositor has hidden all normal content by this point.
    for (auto it = pendingOutputs.begin(); it != pendingOutputs.end();) {
        if (!hasMappedLockSurfaceForOutput(*it)) {
            it = pendingOutputs.erase(it);
        } else {
            ++it;
        }
    }
    maybeSendLocked();
}

void SessionLockManagerV1InterfacePrivate::maybeSendLocked()
{
    if (!activeLock || lockSent) {
        return;
    }
    if (pendingOutputs.isEmpty()) {
        lockSent = true;
        activeLock->d->sendLocked();
    }
}

void SessionLockManagerV1InterfacePrivate::ext_session_lock_manager_v1_destroy(Resource *resource)
{
    wl_resource_destroy(resource->handle);
}

void SessionLockManagerV1InterfacePrivate::ext_session_lock_manager_v1_lock(Resource *resource, uint32_t id)
{
    SessionLockV1Interface *lock = new SessionLockV1Interface();
    lock->d->manager = this;
    lock->d->add(resource->client(), id, resource->version());
    engageLock(lock);
}

SessionLockManagerV1Interface::SessionLockManagerV1Interface(Display *display, QObject *parent)
    : QObject(parent)
    , d(std::make_unique<SessionLockManagerV1InterfacePrivate>(this, display))
{
}

SessionLockManagerV1Interface::~SessionLockManagerV1Interface() = default;

bool SessionLockManagerV1Interface::isSessionLocked() const
{
    return d->sessionLocked;
}

SessionLockV1Interface *SessionLockManagerV1Interface::activeLock() const
{
    return d->activeLock;
}

} // namespace KWin

#include "moc_session_lock_v1.cpp"
