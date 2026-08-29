/*
    SPDX-FileCopyrightText: 2026 The KineticWE Contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "kwin_wayland_test.h"

#include "backends/virtual/virtual_egl_backend.h"
#include "compositor.h"
#include "core/backendoutput.h"
#include "core/output.h"
#include "core/outputbackend.h"
#include "core/outputconfiguration.h"
#include "core/renderbackend.h"
#include "cursor.h"
#include "effect/effecthandler.h"
#include "opengl/gltexture.h"
#include "scene/workspacescene.h"
#include "sessionlockv1window.h"
#include "wayland/clientconnection.h"
#include "wayland/display.h"
#include "wayland/session_lock_v1.h"
#include "wayland_server.h"
#include "workspace.h"

#include <KWayland/Client/output.h>
#include <KWayland/Client/surface.h>

#include <QPointer>
#include <QSignalSpy>

namespace KWin
{

static const Rect s_outputGeometry = Rect(0, 0, 320, 240);
static const QSize s_secondaryModeSize = QSize(240, 180);

class SessionLockV1Test : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void testInitialConfigure();
    void testOutputGeometryChange();
    void testLockSurfaceDestroy();
    void testClientDisconnectFailSecureAndReplacement();

private:
    std::unique_ptr<Test::Connection> m_connection;
    QPointer<ClientConnection> m_serverConnection;
};

void SessionLockV1Test::initTestCase()
{
    qRegisterMetaType<Window *>();
    qRegisterMetaType<ClientConnection *>();

    QVERIFY(waylandServer()->init(qAppName()));
    kwinApp()->start();
    QVERIFY(Test::setOutputConfig({
        Test::OutputInfo{
            .geometry = s_outputGeometry,
            .modes = {
                OutputModeline(QSize(320, 240), 60000, OutputModeline::Flag::Preferred),
                OutputModeline(s_secondaryModeSize, 60000, OutputModeline::Flags{}),
            },
        },
    }));
    effects->unloadAllEffects();
    Cursors::self()->hideCursor();
}

void SessionLockV1Test::init()
{
    QSignalSpy clientConnectedSpy(waylandServer()->display(), &Display::clientConnected);
    m_connection = Test::Connection::setup(Test::AdditionalWaylandInterface::SessionLockV1);
    QVERIFY(m_connection);
    QVERIFY(m_connection->sessionLockManager);
    QCOMPARE(m_connection->outputs.count(), 1);
    QVERIFY(clientConnectedSpy.wait());
    m_serverConnection = clientConnectedSpy.first().first().value<ClientConnection *>();
    QVERIFY(m_serverConnection);
}

void SessionLockV1Test::cleanup()
{
    m_connection.reset();
    m_connection = nullptr;
    m_serverConnection = nullptr;

    QVERIFY(!waylandServer()->sessionLockManager()->isSessionLocked());
    QCOMPARE(waylandServer()->sessionLockManager()->activeLock(), nullptr);
}

void SessionLockV1Test::testInitialConfigure()
{
    SessionLockManagerV1Interface *manager = waylandServer()->sessionLockManager();

    std::unique_ptr<Test::SessionLockV1> lock(m_connection->sessionLockManager->createLock());
    QSignalSpy lockCreatedSpy(manager, &SessionLockManagerV1Interface::lockCreated);
    QSignalSpy lockedSpy(lock.get(), &Test::SessionLockV1::locked);
    QVERIFY(lockCreatedSpy.wait());

    QCOMPARE(lockCreatedSpy.count(), 1);
    QVERIFY(manager->isSessionLocked());
    QVERIFY(waylandServer()->isScreenLocked());
    SessionLockV1Interface *serverLock = manager->activeLock();
    QVERIFY(serverLock);
    QCOMPARE(serverLock, lockCreatedSpy.first().first().value<SessionLockV1Interface *>());

    std::unique_ptr<KWayland::Client::Surface> surface(Test::createSurface(m_connection->compositor));
    QSignalSpy windowAddedSpy(workspace(), &Workspace::windowAdded);
    std::unique_ptr<Test::SessionLockSurfaceV1> lockSurface(lock->createSurface(surface.get(), m_connection->outputs.front()));

    QSignalSpy configureSpy(lockSurface.get(), &Test::SessionLockSurfaceV1::configureRequested);
    QVERIFY(configureSpy.wait());
    QCOMPARE(configureSpy.count(), 1);
    const quint32 serial = configureSpy.first().at(0).toUInt();
    QVERIFY(serial != 0);
    QCOMPARE(configureSpy.first().at(1).toSize(), QSize(320, 240));

    QVERIFY(windowAddedSpy.wait());
    auto window = qobject_cast<LockScreenWindow *>(windowAddedSpy.first().first().value<Window *>());
    QVERIFY(window);
    QVERIFY(window->isLockScreen());
    QCOMPARE(window->output(), workspace()->outputs().front());

    QCOMPARE(serverLock->surfaces().count(), 1);
    SessionLockSurfaceV1Interface *serverLockSurface = serverLock->surfaces().front();
    QCOMPARE(serverLockSurface->output()->handle(), workspace()->outputs().front());
    QVERIFY(!serverLockSurface->isMapped());

    lockSurface->ack_configure(serial);
    QSignalSpy mappedSpy(serverLockSurface, &SessionLockSurfaceV1Interface::mappedChanged);
    Test::render(m_connection->shm, surface.get(), QSize(320, 240), Qt::blue);
    QVERIFY(mappedSpy.wait());
    QVERIFY(lockedSpy.wait());

    QVERIFY(serverLockSurface->isMapped());
    QVERIFY(lock->hasReceivedLocked());
    QTRY_COMPARE(window->frameGeometry(), RectF(0, 0, 320, 240));

    lock->unlock();
    QVERIFY(m_connection->sync());
    QVERIFY(!manager->isSessionLocked());
    QVERIFY(!waylandServer()->isScreenLocked());
    QCOMPARE(manager->activeLock(), nullptr);
}

void SessionLockV1Test::testOutputGeometryChange()
{
    SessionLockManagerV1Interface *manager = waylandServer()->sessionLockManager();

    std::unique_ptr<Test::SessionLockV1> lock(m_connection->sessionLockManager->createLock());
    QSignalSpy lockedSpy(lock.get(), &Test::SessionLockV1::locked);
    QVERIFY(lockedSpy.wait());

    std::unique_ptr<KWayland::Client::Surface> surface(Test::createSurface(m_connection->compositor));
    QSignalSpy windowAddedSpy(workspace(), &Workspace::windowAdded);
    std::unique_ptr<Test::SessionLockSurfaceV1> lockSurface(lock->createSurface(surface.get(), m_connection->outputs.front()));

    QSignalSpy configureSpy(lockSurface.get(), &Test::SessionLockSurfaceV1::configureRequested);
    QVERIFY(configureSpy.wait());
    const quint32 initialSerial = configureSpy.last().at(0).toUInt();
    QCOMPARE(configureSpy.last().at(1).toSize(), QSize(320, 240));

    QVERIFY(windowAddedSpy.wait());
    auto window = qobject_cast<LockScreenWindow *>(windowAddedSpy.first().first().value<Window *>());
    QVERIFY(window);

    lockSurface->ack_configure(initialSerial);
    QSignalSpy mappedSpy(manager->activeLock()->surfaces().front(), &SessionLockSurfaceV1Interface::mappedChanged);
    Test::render(m_connection->shm, surface.get(), QSize(320, 240), Qt::blue);
    QVERIFY(mappedSpy.wait());
    QVERIFY(lockedSpy.wait());
    QVERIFY(lock->hasReceivedLocked());

    // Change the output's mode and position in place (no hotplug).
    BackendOutput *backendOutput = kwinApp()->outputBackend()->outputs().front();
    OutputConfiguration config;
    auto changeSet = config.changeSet(backendOutput);
    changeSet->currentMode = backendOutput->modes()[1]->modeline();
    changeSet->desiredMode = OutputModeline(s_secondaryModeSize, 60000);
    changeSet->pos = QPoint(40, 30);
    QCOMPARE(workspace()->applyOutputConfiguration(config), OutputConfigurationError::None);

    const int previousConfigureCount = configureSpy.count();
    QVERIFY(configureSpy.wait(5000));
    QVERIFY(configureSpy.count() > previousConfigureCount);

    const quint32 newSerial = configureSpy.last().at(0).toUInt();
    QVERIFY(newSerial != initialSerial);
    QCOMPARE(configureSpy.last().at(1).toSize(), s_secondaryModeSize);
    QCOMPARE(workspace()->outputs().front()->geometry(), Rect(40, 30, 240, 180));
    QTRY_COMPARE(window->frameGeometry().topLeft(), QPointF(40, 30));

    lockSurface->ack_configure(newSerial);
    Test::render(m_connection->shm, surface.get(), s_secondaryModeSize, Qt::blue);
    QTRY_COMPARE(window->frameGeometry(), RectF(40, 30, 240, 180));
    QCOMPARE(window->output(), workspace()->outputs().front());

    lock->unlock();
    QVERIFY(m_connection->sync());
    QVERIFY(!manager->isSessionLocked());
}

void SessionLockV1Test::testLockSurfaceDestroy()
{
    SessionLockManagerV1Interface *manager = waylandServer()->sessionLockManager();

    // Create a normal window that must stay hidden while the session is locked.
    std::unique_ptr<KWayland::Client::Surface> normalSurface(Test::createSurface(m_connection->compositor));
    std::unique_ptr<Test::XdgToplevel> normalShellSurface(Test::createXdgToplevelSurface(m_connection->xdgShell, normalSurface.get()));
    Window *normalWindow = Test::renderAndWaitForShown(m_connection->shm, normalSurface.get(), QSize(100, 50), Qt::red);
    QVERIFY(normalWindow);
    QVERIFY(normalWindow->windowItem()->isVisible());

    std::unique_ptr<Test::SessionLockV1> lock(m_connection->sessionLockManager->createLock());
    QSignalSpy lockedSpy(lock.get(), &Test::SessionLockV1::locked);
    QVERIFY(lockedSpy.wait());

    std::unique_ptr<KWayland::Client::Surface> surface(Test::createSurface(m_connection->compositor));
    QSignalSpy windowAddedSpy(workspace(), &Workspace::windowAdded);
    std::unique_ptr<Test::SessionLockSurfaceV1> lockSurface(lock->createSurface(surface.get(), m_connection->outputs.front()));

    QSignalSpy configureSpy(lockSurface.get(), &Test::SessionLockSurfaceV1::configureRequested);
    QVERIFY(configureSpy.wait());
    const quint32 serial = configureSpy.first().at(0).toUInt();

    QVERIFY(windowAddedSpy.wait());
    auto window = qobject_cast<LockScreenWindow *>(windowAddedSpy.first().first().value<Window *>());
    QVERIFY(window);
    QPointer<LockScreenWindow> lockWindow(window);

    lockSurface->ack_configure(serial);
    QSignalSpy mappedSpy(manager->activeLock()->surfaces().front(), &SessionLockSurfaceV1Interface::mappedChanged);
    Test::render(m_connection->shm, surface.get(), QSize(320, 240), Qt::blue);
    QVERIFY(mappedSpy.wait());
    QVERIFY(lockedSpy.wait());

    QVERIFY(manager->isSessionLocked());
    QVERIFY(waylandServer()->isScreenLocked());
    SessionLockV1Interface *serverLock = manager->activeLock();
    QVERIFY(serverLock);
    QTRY_VERIFY(!normalWindow->windowItem()->isVisible());

    // Destroy the lock surface while the session stays locked.
    QSignalSpy closedSpy(window, &Window::closed);
    lockSurface.reset();
    QVERIFY(closedSpy.count() == 1 || closedSpy.wait());
    QTRY_VERIFY(lockWindow.isNull());

    QVERIFY(manager->isSessionLocked());
    QVERIFY(waylandServer()->isScreenLocked());
    QCOMPARE(manager->activeLock(), serverLock);
    QVERIFY(serverLock->surfaces().isEmpty());
    QVERIFY(lock->hasReceivedLocked());
    QTRY_VERIFY(!normalWindow->windowItem()->isVisible());

    // Force a full repaint and check that the output is blank (solid color),
    // i.e. neither the stale lock surface nor the normal window is shown.
    kwinApp()->scene()->addRepaintFull();
    QSignalSpy frameRenderedSpy(kwinApp()->scene(), &WorkspaceScene::frameRendered);
    QVERIFY(frameRenderedSpy.wait());

    BackendOutput *backendOutput = kwinApp()->outputBackend()->outputs().front();
    const auto layer = static_cast<VirtualEglLayer *>(Compositor::self()->backend()->compatibleOutputLayers(backendOutput).front());
    const QImage image = layer->texture()->toImage();
    QCOMPARE(image.size(), QSize(320, 240));
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const QRgb pixel = image.pixel(x, y);
            QCOMPARE(qRed(pixel), 0);
            QCOMPARE(qGreen(pixel), 0);
            QCOMPARE(qBlue(pixel), 0);
        }
    }

    lock->unlock();
    QVERIFY(m_connection->sync());
    QVERIFY(!manager->isSessionLocked());
}

void SessionLockV1Test::testClientDisconnectFailSecureAndReplacement()
{
    SessionLockManagerV1Interface *manager = waylandServer()->sessionLockManager();

    // First locker.
    std::unique_ptr<Test::SessionLockV1> firstLock(m_connection->sessionLockManager->createLock());
    QSignalSpy firstLockedSpy(firstLock.get(), &Test::SessionLockV1::locked);
    QVERIFY(firstLockedSpy.wait());

    std::unique_ptr<KWayland::Client::Surface> surface(Test::createSurface(m_connection->compositor));
    std::unique_ptr<Test::SessionLockSurfaceV1> lockSurface(firstLock->createSurface(surface.get(), m_connection->outputs.front()));

    QSignalSpy configureSpy(lockSurface.get(), &Test::SessionLockSurfaceV1::configureRequested);
    QVERIFY(configureSpy.wait());
    const quint32 serial = configureSpy.first().at(0).toUInt();

    lockSurface->ack_configure(serial);
    QSignalSpy mappedSpy(manager->activeLock()->surfaces().front(), &SessionLockSurfaceV1Interface::mappedChanged);
    Test::render(m_connection->shm, surface.get(), QSize(320, 240), Qt::blue);
    QVERIFY(mappedSpy.wait());
    QVERIFY(firstLockedSpy.wait());

    SessionLockV1Interface *firstServerLock = manager->activeLock();
    QVERIFY(firstServerLock);
    QVERIFY(firstLock->hasReceivedLocked());

    QSignalSpy managerLockedChangedSpy(manager, &SessionLockManagerV1Interface::lockedChanged);
    QSignalSpy serverLockStateChangedSpy(waylandServer(), &WaylandServer::lockStateChanged);

    // Simulate the locker crashing: the compositor kills the client without
    // receiving a valid unlock_and_destroy request.
    QVERIFY(m_serverConnection);
    m_serverConnection->destroy();

    QTRY_VERIFY(manager->activeLock() == nullptr);
    QVERIFY(manager->isSessionLocked());
    QVERIFY(waylandServer()->isScreenLocked());
    QCOMPARE(managerLockedChangedSpy.count(), 0);
    QCOMPARE(serverLockStateChangedSpy.count(), 0);

    // A replacement locker must be able to take over without exposing the desktop.
    auto replacement = Test::Connection::setup(Test::AdditionalWaylandInterface::SessionLockV1);
    QVERIFY(replacement);
    QVERIFY(replacement->sessionLockManager);

    std::unique_ptr<Test::SessionLockV1> replacementLock(replacement->sessionLockManager->createLock());
    QSignalSpy replacementLockedSpy(replacementLock.get(), &Test::SessionLockV1::locked);
    QSignalSpy replacementFinishedSpy(replacementLock.get(), &Test::SessionLockV1::finished);
    QTRY_VERIFY(manager->activeLock() != nullptr);

    QVERIFY(manager->isSessionLocked());
    QCOMPARE(replacementFinishedSpy.count(), 0);
    QCOMPARE(managerLockedChangedSpy.count(), 0);
    QCOMPARE(serverLockStateChangedSpy.count(), 0);

    std::unique_ptr<KWayland::Client::Surface> replacementSurface(Test::createSurface(replacement->compositor));
    std::unique_ptr<Test::SessionLockSurfaceV1> replacementLockSurface(replacementLock->createSurface(replacementSurface.get(), replacement->outputs.front()));

    QSignalSpy replacementConfigureSpy(replacementLockSurface.get(), &Test::SessionLockSurfaceV1::configureRequested);
    QVERIFY(replacementConfigureSpy.wait());
    const quint32 replacementSerial = replacementConfigureSpy.first().at(0).toUInt();

    replacementLockSurface->ack_configure(replacementSerial);
    QSignalSpy replacementMappedSpy(manager->activeLock()->surfaces().front(), &SessionLockSurfaceV1Interface::mappedChanged);
    Test::render(replacement->shm, replacementSurface.get(), QSize(320, 240), Qt::blue);
    QVERIFY(replacementMappedSpy.wait());
    QVERIFY(replacementLockedSpy.wait());

    QCOMPARE(replacementLockedSpy.count(), 1);
    QCOMPARE(replacementFinishedSpy.count(), 0);
    QVERIFY(replacementLock->hasReceivedLocked());
    QVERIFY(manager->isSessionLocked());
    QVERIFY(waylandServer()->isScreenLocked());

    // Only the replacement's valid unlock_and_destroy may clear the lock.
    replacementLock->unlock();
    QVERIFY(replacement->sync());
    QVERIFY(!manager->isSessionLocked());
    QVERIFY(!waylandServer()->isScreenLocked());
    QCOMPARE(manager->activeLock(), nullptr);
    QCOMPARE(managerLockedChangedSpy.count(), 1);
    QCOMPARE(serverLockStateChangedSpy.count(), 1);
}

} // namespace KWin

WAYLANDTEST_MAIN(KWin::SessionLockV1Test)
#include "sessionlockv1_test.moc"
