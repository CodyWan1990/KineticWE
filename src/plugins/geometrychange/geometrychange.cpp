/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2026 KineticWE contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// Based on the scripted kwin4_effect_geometry_change by Peter Fajdiga,
// reimplemented as a native (compiled) effect.

#include "geometrychange.h"

#include "effect/effecthandler.h"
// KConfigSkeleton
#include "geometrychangeconfig.h"

#include <QEasingCurve>
#include <QDateTime>

using namespace std::chrono_literals;

namespace KWin
{

GeometryChangeEffect::GeometryChangeEffect()
{
    GeometryChangeConfig::instance(effects->config());
    reconfigure(ReconfigureAll);

    connect(effects, &EffectsHandler::windowAdded, this, &GeometryChangeEffect::slotWindowAdded);
    connect(effects, &EffectsHandler::windowDeleted, this, &GeometryChangeEffect::slotWindowDeleted);

    const auto windows = effects->stackingOrder();
    for (EffectWindow *window : windows) {
        slotWindowAdded(window);
    }

    setVertexSnappingMode(RenderGeometry::VertexSnappingMode::None);
}

bool GeometryChangeEffect::supported()
{
    return OffscreenEffect::supported() && effects->animationsSupported();
}

void GeometryChangeEffect::reconfigure(ReconfigureFlags)
{
    GeometryChangeConfig::self()->read();

    const int durationMs = GeometryChangeConfig::duration();
    const std::chrono::milliseconds d(durationMs > 0 ? durationMs : 250);
    m_duration = animationTime(d);

    m_excludedWindowClasses = GeometryChangeConfig::excludedWindowClasses().split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (QString &windowClass : m_excludedWindowClasses) {
        windowClass = windowClass.trimmed();
    }
}

void GeometryChangeEffect::prePaintScreen(ScreenPrePaintData &data)
{
    // Windows are transformed during the animation, so the whole screen must be
    // repainted to avoid artefacts.
    data.mask |= PAINT_SCREEN_WITH_TRANSFORMED_WINDOWS;

    effects->prePaintScreen(data);
}

void GeometryChangeEffect::prePaintWindow(RenderView *view, EffectWindow *w, WindowPrePaintData &data)
{
    auto it = m_animations.find(w);
    if (it != m_animations.end()) {
        it->timeLine.advance(view);
        data.setTransformed();
    }

    effects->prePaintWindow(view, w, data);
}

void GeometryChangeEffect::apply(EffectWindow *w, int mask, WindowPaintData &data, WindowQuadList &quads)
{
    auto it = m_animations.constFind(w);
    if (it == m_animations.constEnd()) {
        return;
    }

    const qreal eased = it->timeLine.value(); // 0..1, easing applied
    const qreal inv = 1.0 - eased;

    data.setXTranslation(it->startTx * inv);
    data.setYTranslation(it->startTy * inv);
    data.setXScale(1.0 + (it->startSx - 1.0) * inv);
    data.setYScale(1.0 + (it->startSy - 1.0) * inv);
    (void)mask;
    (void)quads;
}

void GeometryChangeEffect::postPaintScreen()
{
    auto it = m_animations.begin();
    while (it != m_animations.end()) {
        if (it->timeLine.done()) {
            it.key()->setData(WindowForceBlurRole, QVariant());
            unredirect(it.key());
            it = m_animations.erase(it);
        } else {
            ++it;
        }
    }

    if (!m_animations.isEmpty()) {
        effects->addRepaintFull();
    }

    // Call the next effect.
    effects->postPaintScreen();
}

bool GeometryChangeEffect::isActive() const
{
    return !m_animations.isEmpty();
}

void GeometryChangeEffect::slotWindowAdded(EffectWindow *w)
{
    auto &state = m_windowStates[w];
    state.createdTimeMs = QDateTime::currentMSecsSinceEpoch();
    state.maximizedAboutToChange = false;

    connect(w, &EffectWindow::windowFrameGeometryChanged,
            this, &GeometryChangeEffect::slotWindowFrameGeometryChanged);
    connect(w, &EffectWindow::windowMaximizedStateAboutToChange,
            this, &GeometryChangeEffect::slotWindowMaximizedStateAboutToChange);
    connect(w, &EffectWindow::windowStartUserMovedResized,
            this, &GeometryChangeEffect::slotWindowStartUserMovedResized);
    connect(w, &EffectWindow::windowFinishUserMovedResized,
            this, &GeometryChangeEffect::slotWindowFinishUserMovedResized);
}

void GeometryChangeEffect::slotWindowDeleted(EffectWindow *w)
{
    m_animations.remove(w);
    m_windowStates.remove(w);
}

void GeometryChangeEffect::slotWindowFrameGeometryChanged(EffectWindow *w, const RectF &oldGeometry)
{
    auto stateIt = m_windowStates.find(w);
    if (stateIt == m_windowStates.end()) {
        return;
    }
    auto &state = stateIt.value();

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 age = now - state.createdTimeMs;
    if (age < 0) {
        // Can happen after a time zone change. Fix the created time so it is
        // not in the future.
        state.createdTimeMs = now;
        return;
    }
    if (age < 10) {
        // Some windows are moved or resized immediately after being created.
        // We do not want to animate that.
        return;
    }

    const bool maximizationChange = state.maximizedAboutToChange;
    state.maximizedAboutToChange = false;

    if (!windowSupportsAnimation(w)) {
        return;
    }

    const bool userMoveResize = w->isUserMove() || w->isUserResize() || m_userResizing;
    if (userMoveResize && !maximizationChange) {
        return;
    }

    const RectF newGeometry = w->frameGeometry();
    if (newGeometry == oldGeometry) {
        return;
    }
    if (newGeometry.width() <= 0.0 || newGeometry.height() <= 0.0) {
        return;
    }

    const qreal xDelta = newGeometry.x() - oldGeometry.x();
    const qreal yDelta = newGeometry.y() - oldGeometry.y();
    const qreal widthRatio = oldGeometry.width() / newGeometry.width();
    const qreal heightRatio = oldGeometry.height() / newGeometry.height();

    GeometryChangeAnimation &animation = m_animations[w];
    animation.startTx = -xDelta;
    animation.startTy = -yDelta;
    animation.startSx = widthRatio;
    animation.startSy = heightRatio;
    animation.timeLine = TimeLine(m_duration);
    animation.timeLine.setEasingCurve(QEasingCurve::OutExpo);

    redirect(w);
    w->setData(WindowForceBlurRole, true);
    effects->addRepaintFull();
}

void GeometryChangeEffect::slotWindowMaximizedStateAboutToChange(EffectWindow *w, bool horizontal, bool vertical)
{
    Q_UNUSED(horizontal)
    Q_UNUSED(vertical)

    auto stateIt = m_windowStates.find(w);
    if (stateIt != m_windowStates.end()) {
        stateIt->maximizedAboutToChange = true;
    }
}

void GeometryChangeEffect::slotWindowStartUserMovedResized(EffectWindow *w)
{
    Q_UNUSED(w)
    m_userResizing = true;
}

void GeometryChangeEffect::slotWindowFinishUserMovedResized(EffectWindow *w)
{
    Q_UNUSED(w)
    m_userResizing = false;
}

bool GeometryChangeEffect::windowSupportsAnimation(EffectWindow *w) const
{
    if (!w->isManaged() || !w->isVisible() || !w->isOnCurrentDesktop() || w->isMinimized()) {
        return false;
    }
    if (!w->isNormalWindow() && !w->isDialog() && !w->isModal()) {
        return false;
    }
    if (isWindowClassExcluded(w->windowClass())) {
        return false;
    }
    return true;
}

bool GeometryChangeEffect::isWindowClassExcluded(const QString &windowClass) const
{
    const QStringList parts = windowClass.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (const QString &part : parts) {
        if (m_excludedWindowClasses.contains(part)) {
            return true;
        }
    }
    return false;
}

} // namespace KWin

#include "moc_geometrychange.cpp"
