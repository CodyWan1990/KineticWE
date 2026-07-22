/*
    KWin - the KDE window manager
    SPDX-FileCopyrightText: 2026 KWin Tiling Fork

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "kwin_wayland_test.h"

#include "core/output.h"
#include "pointer_input.h"
#include "tiles/columnslayoutengine.h"
#include "tiles/customtile.h"
#include "tiles/layoutengine.h"
#include "tiles/tilemanager.h"
#include "tiling/tilingcontroller.h"
#include "tiling/tilingstate.h"
#include "virtualdesktops.h"
#include "wayland_server.h"
#include "window.h"
#include "workspace.h"

namespace KWin
{

/**
 * Integration tests for the Columns layout engine.
 *
 * The Columns layout arranges windows in equal-width columns (up to a
 * configurable maximum) and stacks additional windows vertically within a
 * column. These tests pin down:
 *
 *   - Drop-zone aware drag & drop: releasing a dragged window over the top
 *     or bottom zone of another window joins that window's column above or
 *     below it (splitting the column), while the middle zone keeps the
 *     classic swap behaviour.
 *   - consume(): the focused window leaves its column and joins the column
 *     to its right (or to its left when it is in the rightmost column).
 *   - expel(): the focused window leaves its column and gets its own new
 *     column to the right of the old one (or to the left when its old
 *     column is the rightmost), bounded by the column cap.
 *   - setMaxColumns(): the column cap is configurable.
 */
class TilingColumnsTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void testSetLayoutCreatesEngine();
    void testJoinBelowSplitsColumn();
    void testJoinAboveSplitsColumn();
    void testMiddleZoneStillSwaps();
    void testJoinWithinSameColumnReorders();
    void testJoinAfterDetachMidDrag();
    void testConsumeJoinsRightColumn();
    void testConsumeFallsBackToLeft();
    void testConsumeNoOpSingleColumn();
    void testExpelCreatesColumnToRight();
    void testExpelRightmostInsertsLeft();
    void testExpelNoOpSingleWindowColumn();
    void testExpelRespectsMaxColumns();
    void testMaxColumnsConfigurable();
    void testJoinRespectsGaps();

private:
    void createTiledWindow(std::unique_ptr<KWayland::Client::Surface> *surface,
                           std::unique_ptr<Test::XdgToplevel> *shellSurface,
                           Window **window);
    ColumnsLayoutEngine *columnsEngine();
    /** Simulates a meta+drag drop of @p window onto @p target in @p zone. */
    void dropWindowOn(Window *window, Window *target, LayoutEngine::DropZone zone);

    std::unique_ptr<KWayland::Client::Surface> m_surface;
    std::unique_ptr<KWayland::Client::Surface> m_surface2;
    std::unique_ptr<KWayland::Client::Surface> m_surface3;
    std::unique_ptr<KWayland::Client::Surface> m_surface4;
    std::unique_ptr<Test::XdgToplevel> m_shellSurface;
    std::unique_ptr<Test::XdgToplevel> m_shellSurface2;
    std::unique_ptr<Test::XdgToplevel> m_shellSurface3;
    std::unique_ptr<Test::XdgToplevel> m_shellSurface4;
    Window *m_window = nullptr;
    Window *m_window2 = nullptr;
    Window *m_window3 = nullptr;
    Window *m_window4 = nullptr;

    LogicalOutput *m_output = nullptr;
    TileManager *m_tileManager = nullptr;
    VirtualDesktop *m_desktop = nullptr;
};


void TilingColumnsTest::initTestCase()
{
    qRegisterMetaType<KWin::Window *>();
    QVERIFY(waylandServer()->init(qAppName()));

    kwinApp()->start();
    Test::setOutputConfig({
        Rect(0, 0, 1280, 1024),
    });
    setenv("QT_QPA_PLATFORM", "wayland", true);
}

void TilingColumnsTest::init()
{
    QVERIFY(Test::setupWaylandConnection());

    workspace()->setActiveOutput(QPoint(640, 512));
    input()->pointer()->warp(QPoint(640, 512));

    m_output = workspace()->activeOutput();
    m_tileManager = workspace()->tileManager(m_output);
    QVERIFY(m_tileManager);

    VirtualDesktopManager::self()->setCount(1);
    m_desktop = VirtualDesktopManager::self()->desktops().at(0);
    VirtualDesktopManager::self()->setCurrent(m_desktop);

    TilingController *controller = workspace()->tilingController();
    QVERIFY(controller);
    controller->setLayout(LayoutEngine::LayoutKind::Columns);

    // The engine persists across tests when the layout kind is unchanged, so
    // reset the column cap to its default for a deterministic starting state.
    if (ColumnsLayoutEngine *engine = columnsEngine()) {
        engine->setMaxColumns(3);
    }
}

void TilingColumnsTest::cleanup()
{
    m_surface.reset();
    m_surface2.reset();
    m_surface3.reset();
    m_surface4.reset();
    m_shellSurface.reset();
    m_shellSurface2.reset();
    m_shellSurface3.reset();
    m_shellSurface4.reset();
    m_window = nullptr;
    m_window2 = nullptr;
    m_window3 = nullptr;
    m_window4 = nullptr;

    Test::destroyWaylandConnection();
}

void TilingColumnsTest::createTiledWindow(std::unique_ptr<KWayland::Client::Surface> *surface,
                                          std::unique_ptr<Test::XdgToplevel> *shellSurface,
                                          Window **window)
{
    *surface = Test::createSurface();
    QVERIFY(*surface);
    *shellSurface = Test::createXdgToplevelSurface(surface->get());
    QVERIFY(*shellSurface);

    *window = Test::renderAndWaitForShown(surface->get(), QSize(400, 300), Qt::blue);
    QVERIFY(*window);

    // Wait for the window to be actually tiled by the controller.
    QTRY_VERIFY((*window)->tilingState().mode == TilingState::Mode::Tiled);
}

ColumnsLayoutEngine *TilingColumnsTest::columnsEngine()
{
    return qobject_cast<ColumnsLayoutEngine *>(m_tileManager->layoutEngine(m_desktop));
}

void TilingColumnsTest::dropWindowOn(Window *window, Window *target, LayoutEngine::DropZone zone)
{
    LayoutEngine *engine = m_tileManager->layoutEngine(m_desktop);
    QVERIFY(engine);
    engine->beginMoveWindow(window);
    QVERIFY(engine->endMoveWindowOnZone(window, target, zone));
}

static bool rectNear(const QRectF &got, qreal x, qreal y, qreal w, qreal h)
{
    return qAbs(got.x() - x) < 1.0 && qAbs(got.y() - y) < 1.0
        && qAbs(got.width() - w) < 1.0 && qAbs(got.height() - h) < 1.0;
}

static QString geomStr(const QRectF &g)
{
    return QStringLiteral("(%1, %2, %3 x %4)").arg(g.x()).arg(g.y()).arg(g.width()).arg(g.height());
}

void TilingColumnsTest::testSetLayoutCreatesEngine()
{
    TilingController *controller = workspace()->tilingController();
    QVERIFY(controller);

    controller->setLayout(LayoutEngine::LayoutKind::Columns);
    LayoutEngine *engine = m_tileManager->layoutEngine(m_desktop);
    QVERIFY(engine);
    QCOMPARE(engine->layoutKind(), LayoutEngine::LayoutKind::Columns);
}

void TilingColumnsTest::testJoinBelowSplitsColumn()
{
    createTiledWindow(&m_surface, &m_shellSurface, &m_window);
    createTiledWindow(&m_surface2, &m_shellSurface2, &m_window2);
    createTiledWindow(&m_surface3, &m_shellSurface3, &m_window3);
    QVERIFY(m_window && m_window2 && m_window3);
    ColumnsLayoutEngine *engine = columnsEngine();
    QVERIFY(engine);

    // Drop window 1 onto the bottom zone of window 3: it joins window 3's
    // column below it. Window 1's own column disappears, leaving 2 columns.
    dropWindowOn(m_window, m_window3, LayoutEngine::DropZone::InsertBelow);

    QCOMPARE(engine->windows(), QList<Window *>({m_window2, m_window3, m_window}));

    // Left column: window 2, full height, half width.
    QVERIFY2(rectNear(m_window2->moveResizeGeometry(), 0, 0, 640, 1024),
             qPrintable(QStringLiteral("w2 geometry %1").arg(geomStr(m_window2->moveResizeGeometry()))));
    // Right column: window 3 on top, window 1 below.
    QVERIFY2(rectNear(m_window3->moveResizeGeometry(), 640, 0, 640, 512),
             qPrintable(QStringLiteral("w3 geometry %1").arg(geomStr(m_window3->moveResizeGeometry()))));
    QVERIFY2(rectNear(m_window->moveResizeGeometry(), 640, 512, 640, 512),
             qPrintable(QStringLiteral("w1 geometry %1").arg(geomStr(m_window->moveResizeGeometry()))));
}

void TilingColumnsTest::testJoinAboveSplitsColumn()
{
    createTiledWindow(&m_surface, &m_shellSurface, &m_window);
    createTiledWindow(&m_surface2, &m_shellSurface2, &m_window2);
    createTiledWindow(&m_surface3, &m_shellSurface3, &m_window3);
    QVERIFY(m_window && m_window2 && m_window3);
    ColumnsLayoutEngine *engine = columnsEngine();
    QVERIFY(engine);

    // Drop window 3 onto the top zone of window 1: it joins window 1's
    // column above it, leaving 2 columns.
    dropWindowOn(m_window3, m_window, LayoutEngine::DropZone::InsertAbove);

    QCOMPARE(engine->windows(), QList<Window *>({m_window3, m_window, m_window2}));

    // Left column: window 3 on top, window 1 below.
    QVERIFY2(rectNear(m_window3->moveResizeGeometry(), 0, 0, 640, 512),
             qPrintable(QStringLiteral("w3 geometry %1").arg(geomStr(m_window3->moveResizeGeometry()))));
    QVERIFY2(rectNear(m_window->moveResizeGeometry(), 0, 512, 640, 512),
             qPrintable(QStringLiteral("w1 geometry %1").arg(geomStr(m_window->moveResizeGeometry()))));
    // Right column: window 2, full height.
    QVERIFY2(rectNear(m_window2->moveResizeGeometry(), 640, 0, 640, 1024),
             qPrintable(QStringLiteral("w2 geometry %1").arg(geomStr(m_window2->moveResizeGeometry()))));
}

void TilingColumnsTest::testMiddleZoneStillSwaps()
{
    createTiledWindow(&m_surface, &m_shellSurface, &m_window);
    createTiledWindow(&m_surface2, &m_shellSurface2, &m_window2);
    createTiledWindow(&m_surface3, &m_shellSurface3, &m_window3);
    QVERIFY(m_window && m_window2 && m_window3);
    ColumnsLayoutEngine *engine = columnsEngine();
    QVERIFY(engine);

    // Dropping on the middle zone keeps the classic swap behaviour.
    dropWindowOn(m_window, m_window3, LayoutEngine::DropZone::Swap);

    QCOMPARE(engine->windows(), QList<Window *>({m_window3, m_window2, m_window}));

    // Three columns remain; window 3 took window 1's place and vice versa.
    QVERIFY(m_window3->moveResizeGeometry().left() < m_window2->moveResizeGeometry().left());
    QVERIFY(m_window2->moveResizeGeometry().left() < m_window->moveResizeGeometry().left());
    QVERIFY2(qFuzzyCompare(m_window->moveResizeGeometry().height(), qreal(1024)),
             "w1 should be full height after swap");
}

void TilingColumnsTest::testJoinWithinSameColumnReorders()
{
    createTiledWindow(&m_surface, &m_shellSurface, &m_window);
    createTiledWindow(&m_surface2, &m_shellSurface2, &m_window2);
    createTiledWindow(&m_surface3, &m_shellSurface3, &m_window3);
    QVERIFY(m_window && m_window2 && m_window3);
    ColumnsLayoutEngine *engine = columnsEngine();
    QVERIFY(engine);

    // [w2 | w3,w1]
    dropWindowOn(m_window, m_window3, LayoutEngine::DropZone::InsertBelow);
    QCOMPARE(engine->windows(), QList<Window *>({m_window2, m_window3, m_window}));

    // Dropping window 1 above window 3 within the same column reorders it.
    dropWindowOn(m_window, m_window3, LayoutEngine::DropZone::InsertAbove);
    QCOMPARE(engine->windows(), QList<Window *>({m_window2, m_window, m_window3}));

    QVERIFY2(rectNear(m_window->moveResizeGeometry(), 640, 0, 640, 512),
             qPrintable(QStringLiteral("w1 geometry %1").arg(geomStr(m_window->moveResizeGeometry()))));
    QVERIFY2(rectNear(m_window3->moveResizeGeometry(), 640, 512, 640, 512),
             qPrintable(QStringLiteral("w3 geometry %1").arg(geomStr(m_window3->moveResizeGeometry()))));
}

void TilingColumnsTest::testJoinAfterDetachMidDrag()
{
    createTiledWindow(&m_surface, &m_shellSurface, &m_window);
    createTiledWindow(&m_surface2, &m_shellSurface2, &m_window2);
    createTiledWindow(&m_surface3, &m_shellSurface3, &m_window3);
    QVERIFY(m_window && m_window2 && m_window3);
    ColumnsLayoutEngine *engine = columnsEngine();
    QVERIFY(engine);
    RootTile *root = m_tileManager->rootTile(m_desktop);
    QVERIFY(root);

    engine->beginMoveWindow(m_window);

    // A real meta+drag detaches the window from its tile on the first
    // movement (Window::updateInteractiveMoveResize untiles moved windows).
    Tile *sourceTile = root->tileForWindow(m_window);
    QVERIFY(sourceTile);
    sourceTile->unmanage(m_window);
    QVERIFY(!root->tileForWindow(m_window));

    QVERIFY(engine->endMoveWindowOnZone(m_window, m_window3, LayoutEngine::DropZone::InsertBelow));

    // The window must be re-attached to the moved leaf — not left floating
    // with an empty "ghost" tile behind.
    QVERIFY(root->tileForWindow(m_window));
    QCOMPARE(engine->windows(), QList<Window *>({m_window2, m_window3, m_window}));
    QVERIFY2(rectNear(m_window->moveResizeGeometry(), 640, 512, 640, 512),
             qPrintable(QStringLiteral("w1 geometry %1").arg(geomStr(m_window->moveResizeGeometry()))));
    QVERIFY2(rectNear(m_window3->moveResizeGeometry(), 640, 0, 640, 512),
             qPrintable(QStringLiteral("w3 geometry %1").arg(geomStr(m_window3->moveResizeGeometry()))));
}

void TilingColumnsTest::testConsumeJoinsRightColumn()
{
    TilingController *controller = workspace()->tilingController();
    QVERIFY(controller);

    createTiledWindow(&m_surface, &m_shellSurface, &m_window);
    createTiledWindow(&m_surface2, &m_shellSurface2, &m_window2);
    createTiledWindow(&m_surface3, &m_shellSurface3, &m_window3);
    QVERIFY(m_window && m_window2 && m_window3);
    ColumnsLayoutEngine *engine = columnsEngine();
    QVERIFY(engine);

    // Consume window 1: it leaves its column and joins the column to its
    // right (window 2's column) at the bottom.
    workspace()->activateWindow(m_window);
    controller->consume();

    QCOMPARE(engine->windows(), QList<Window *>({m_window2, m_window, m_window3}));

    // Left column: window 2 on top, window 1 below. Right column: window 3.
    QVERIFY2(rectNear(m_window2->moveResizeGeometry(), 0, 0, 640, 512),
             qPrintable(QStringLiteral("w2 geometry %1").arg(geomStr(m_window2->moveResizeGeometry()))));
    QVERIFY2(rectNear(m_window->moveResizeGeometry(), 0, 512, 640, 512),
             qPrintable(QStringLiteral("w1 geometry %1").arg(geomStr(m_window->moveResizeGeometry()))));
    QVERIFY2(rectNear(m_window3->moveResizeGeometry(), 640, 0, 640, 1024),
             qPrintable(QStringLiteral("w3 geometry %1").arg(geomStr(m_window3->moveResizeGeometry()))));
}


void TilingColumnsTest::testConsumeFallsBackToLeft()
{
    TilingController *controller = workspace()->tilingController();
    QVERIFY(controller);

    createTiledWindow(&m_surface, &m_shellSurface, &m_window);
    createTiledWindow(&m_surface2, &m_shellSurface2, &m_window2);
    createTiledWindow(&m_surface3, &m_shellSurface3, &m_window3);
    QVERIFY(m_window && m_window2 && m_window3);
    ColumnsLayoutEngine *engine = columnsEngine();
    QVERIFY(engine);

    // Window 3 is in the rightmost column: consume falls back to the left.
    workspace()->activateWindow(m_window3);
    controller->consume();

    QCOMPARE(engine->windows(), QList<Window *>({m_window, m_window2, m_window3}));

    // Left column: window 1. Right column: window 2 on top, window 3 below.
    QVERIFY2(rectNear(m_window->moveResizeGeometry(), 0, 0, 640, 1024),
             qPrintable(QStringLiteral("w1 geometry %1").arg(geomStr(m_window->moveResizeGeometry()))));
    QVERIFY2(rectNear(m_window2->moveResizeGeometry(), 640, 0, 640, 512),
             qPrintable(QStringLiteral("w2 geometry %1").arg(geomStr(m_window2->moveResizeGeometry()))));
    QVERIFY2(rectNear(m_window3->moveResizeGeometry(), 640, 512, 640, 512),
             qPrintable(QStringLiteral("w3 geometry %1").arg(geomStr(m_window3->moveResizeGeometry()))));
}

void TilingColumnsTest::testConsumeNoOpSingleColumn()
{
    TilingController *controller = workspace()->tilingController();
    QVERIFY(controller);

    createTiledWindow(&m_surface, &m_shellSurface, &m_window);
    QVERIFY(m_window);
    ColumnsLayoutEngine *engine = columnsEngine();
    QVERIFY(engine);

    workspace()->activateWindow(m_window);
    controller->consume();

    // A single window has no adjacent column; nothing changes.
    QCOMPARE(engine->windows(), QList<Window *>({m_window}));
    QVERIFY2(rectNear(m_window->moveResizeGeometry(), 0, 0, 1280, 1024),
             qPrintable(QStringLiteral("w1 geometry %1").arg(geomStr(m_window->moveResizeGeometry()))));
}

void TilingColumnsTest::testExpelCreatesColumnToRight()
{
    TilingController *controller = workspace()->tilingController();
    QVERIFY(controller);

    createTiledWindow(&m_surface, &m_shellSurface, &m_window);
    createTiledWindow(&m_surface2, &m_shellSurface2, &m_window2);
    createTiledWindow(&m_surface3, &m_shellSurface3, &m_window3);
    QVERIFY(m_window && m_window2 && m_window3);
    ColumnsLayoutEngine *engine = columnsEngine();
    QVERIFY(engine);

    // [w1,w2 | w3]: window 2 joins window 1's column.
    dropWindowOn(m_window2, m_window, LayoutEngine::DropZone::InsertBelow);
    QCOMPARE(engine->windows(), QList<Window *>({m_window, m_window2, m_window3}));

    // Expel window 1: it leaves its column and gets its own new column to
    // the right of the old one.
    workspace()->activateWindow(m_window);
    controller->expel();

    QCOMPARE(engine->windows(), QList<Window *>({m_window2, m_window, m_window3}));

    const qreal third = 1280.0 / 3.0;
    QVERIFY2(rectNear(m_window2->moveResizeGeometry(), 0, 0, third, 1024),
             qPrintable(QStringLiteral("w2 geometry %1").arg(geomStr(m_window2->moveResizeGeometry()))));
    QVERIFY2(rectNear(m_window->moveResizeGeometry(), third, 0, third, 1024),
             qPrintable(QStringLiteral("w1 geometry %1").arg(geomStr(m_window->moveResizeGeometry()))));
    QVERIFY2(rectNear(m_window3->moveResizeGeometry(), 2 * third, 0, third, 1024),
             qPrintable(QStringLiteral("w3 geometry %1").arg(geomStr(m_window3->moveResizeGeometry()))));
}

void TilingColumnsTest::testExpelRightmostInsertsLeft()
{
    TilingController *controller = workspace()->tilingController();
    QVERIFY(controller);

    createTiledWindow(&m_surface, &m_shellSurface, &m_window);
    createTiledWindow(&m_surface2, &m_shellSurface2, &m_window2);
    createTiledWindow(&m_surface3, &m_shellSurface3, &m_window3);
    QVERIFY(m_window && m_window2 && m_window3);
    ColumnsLayoutEngine *engine = columnsEngine();
    QVERIFY(engine);

    // [w1 | w2,w3]: window 3 is consumed into the column to its left.
    workspace()->activateWindow(m_window3);
    controller->consume();
    QCOMPARE(engine->windows(), QList<Window *>({m_window, m_window2, m_window3}));

    // Expel window 3: its old column is the rightmost, so the new column is
    // inserted to its left.
    workspace()->activateWindow(m_window3);
    controller->expel();

    QCOMPARE(engine->windows(), QList<Window *>({m_window, m_window3, m_window2}));

    const qreal third = 1280.0 / 3.0;
    QVERIFY2(rectNear(m_window->moveResizeGeometry(), 0, 0, third, 1024),
             qPrintable(QStringLiteral("w1 geometry %1").arg(geomStr(m_window->moveResizeGeometry()))));
    QVERIFY2(rectNear(m_window3->moveResizeGeometry(), third, 0, third, 1024),
             qPrintable(QStringLiteral("w3 geometry %1").arg(geomStr(m_window3->moveResizeGeometry()))));
    QVERIFY2(rectNear(m_window2->moveResizeGeometry(), 2 * third, 0, third, 1024),
             qPrintable(QStringLiteral("w2 geometry %1").arg(geomStr(m_window2->moveResizeGeometry()))));
}

void TilingColumnsTest::testExpelNoOpSingleWindowColumn()
{
    TilingController *controller = workspace()->tilingController();
    QVERIFY(controller);

    createTiledWindow(&m_surface, &m_shellSurface, &m_window);
    createTiledWindow(&m_surface2, &m_shellSurface2, &m_window2);
    QVERIFY(m_window && m_window2);
    ColumnsLayoutEngine *engine = columnsEngine();
    QVERIFY(engine);

    // Window 1 is already alone in its column: expel is a no-op.
    workspace()->activateWindow(m_window);
    controller->expel();

    QCOMPARE(engine->windows(), QList<Window *>({m_window, m_window2}));
    QVERIFY2(rectNear(m_window->moveResizeGeometry(), 0, 0, 640, 1024),
             qPrintable(QStringLiteral("w1 geometry %1").arg(geomStr(m_window->moveResizeGeometry()))));
    QVERIFY2(rectNear(m_window2->moveResizeGeometry(), 640, 0, 640, 1024),
             qPrintable(QStringLiteral("w2 geometry %1").arg(geomStr(m_window2->moveResizeGeometry()))));
}


void TilingColumnsTest::testExpelRespectsMaxColumns()
{
    TilingController *controller = workspace()->tilingController();
    QVERIFY(controller);

    // Four windows with the default cap of 3: one column holds two windows.
    createTiledWindow(&m_surface, &m_shellSurface, &m_window);
    createTiledWindow(&m_surface2, &m_shellSurface2, &m_window2);
    createTiledWindow(&m_surface3, &m_shellSurface3, &m_window3);
    createTiledWindow(&m_surface4, &m_shellSurface4, &m_window4);
    QVERIFY(m_window && m_window2 && m_window3 && m_window4);
    ColumnsLayoutEngine *engine = columnsEngine();
    QVERIFY(engine);
    QCOMPARE(engine->windows().size(), 4);

    const QRectF before4 = m_window4->moveResizeGeometry();

    // The layout is at the column cap: expelling is a no-op.
    workspace()->activateWindow(m_window4);
    controller->expel();

    QCOMPARE(engine->windows().size(), 4);
    QVERIFY(m_window4->moveResizeGeometry() == before4);

    // Still only three columns: at least one other window shares window 4's
    // horizontal position and neither is full height.
    bool sharesColumn = false;
    for (Window *other : {m_window, m_window2, m_window3}) {
        const QRectF g = other->moveResizeGeometry();
        if (qAbs(g.x() - before4.x()) < 1.0 && g.height() < 1024.0) {
            sharesColumn = true;
            break;
        }
    }
    QVERIFY2(sharesColumn, "expel should have been a no-op at the column cap");
}

void TilingColumnsTest::testMaxColumnsConfigurable()
{
    ColumnsLayoutEngine *engine = columnsEngine();
    QVERIFY(engine);

    // The cap is clamped to the KCM's range of [3, 5].
    engine->setMaxColumns(9);
    QCOMPARE(engine->maxColumns(), 5);
    engine->setMaxColumns(1);
    QCOMPARE(engine->maxColumns(), 3);

    // With a cap of 4, four windows produce four columns instead of stacking.
    engine->setMaxColumns(4);
    QCOMPARE(engine->maxColumns(), 4);

    createTiledWindow(&m_surface, &m_shellSurface, &m_window);
    createTiledWindow(&m_surface2, &m_shellSurface2, &m_window2);
    createTiledWindow(&m_surface3, &m_shellSurface3, &m_window3);
    createTiledWindow(&m_surface4, &m_shellSurface4, &m_window4);
    QVERIFY(m_window && m_window2 && m_window3 && m_window4);

    const qreal quarter = 1280.0 / 4.0;
    const QList<Window *> windows = {m_window, m_window2, m_window3, m_window4};
    for (int i = 0; i < windows.size(); ++i) {
        QVERIFY2(rectNear(windows[i]->moveResizeGeometry(), i * quarter, 0, quarter, 1024),
                 qPrintable(QStringLiteral("window %1 geometry %2, want x=%3 w=%4 h=1024")
                                .arg(i + 1)
                                .arg(geomStr(windows[i]->moveResizeGeometry()))
                                .arg(i * quarter)
                                .arg(quarter)));
    }
}

void TilingColumnsTest::testJoinRespectsGaps()
{
    RootTile *root = m_tileManager->rootTile(m_desktop);
    QVERIFY(root);
    root->setGapBetween(10);
    root->setGapMargins(QMarginsF(10, 10, 10, 10));

    createTiledWindow(&m_surface, &m_shellSurface, &m_window);
    createTiledWindow(&m_surface2, &m_shellSurface2, &m_window2);
    createTiledWindow(&m_surface3, &m_shellSurface3, &m_window3);
    QVERIFY(m_window && m_window2 && m_window3);

    // Sanity: three columns with gaps. Column tiles are sized gap-aware so
    // that every window's *content* width is identical after subtracting its
    // share of the gaps: content = (1280 − 10 left − 10 right − 2×10 between)
    // / 3 ≈ 413.33. Side columns own one edge margin + half a between-gap;
    // the middle column owns half a between-gap on both sides.
    const qreal content = (1280.0 - 10.0 - 10.0 - 2.0 * 10.0) / 3.0;
    const qreal sideCol = content + 10.0 + 5.0; // content + edge margin + half gap
    const qreal midCol = content + 10.0;        // content + two half gaps
    QVERIFY2(rectNear(m_window->moveResizeGeometry(), 10, 10, content, 1004),
             qPrintable(QStringLiteral("w1 geometry %1").arg(geomStr(m_window->moveResizeGeometry()))));
    QVERIFY2(rectNear(m_window2->moveResizeGeometry(), sideCol + 5, 10, content, 1004),
             qPrintable(QStringLiteral("w2 geometry %1").arg(geomStr(m_window2->moveResizeGeometry()))));
    QVERIFY2(rectNear(m_window3->moveResizeGeometry(), sideCol + midCol + 5, 10, content, 1004),
             qPrintable(QStringLiteral("w3 geometry %1").arg(geomStr(m_window3->moveResizeGeometry()))));

    // Join window 1 below window 3: [w2 | w3,w1]. The stacked windows must
    // keep the same gap treatment — edge margins on screen edges, half the
    // between-gap on shared edges (including between the stacked pair).
    dropWindowOn(m_window, m_window3, LayoutEngine::DropZone::InsertBelow);

    // Left column, full height.
    QVERIFY2(rectNear(m_window2->moveResizeGeometry(), 10, 10, 625, 1004),
             qPrintable(QStringLiteral("w2 geometry %1").arg(geomStr(m_window2->moveResizeGeometry()))));
    // Right column top: shared left edge, screen edges top/right, shared bottom.
    QVERIFY2(rectNear(m_window3->moveResizeGeometry(), 645, 10, 625, 497),
             qPrintable(QStringLiteral("w3 geometry %1").arg(geomStr(m_window3->moveResizeGeometry()))));
    // Right column bottom: shared left/top edges, screen edges right/bottom.
    QVERIFY2(rectNear(m_window->moveResizeGeometry(), 645, 517, 625, 497),
             qPrintable(QStringLiteral("w1 geometry %1").arg(geomStr(m_window->moveResizeGeometry()))));

    root->setGapBetween(0);
    root->setGapMargins(QMarginsF(0, 0, 0, 0));
}

} // namespace KWin

WAYLANDTEST_MAIN(KWin::TilingColumnsTest)
#include "tiling_columns_test.moc"
