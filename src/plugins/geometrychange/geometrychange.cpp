/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2026 KineticWE contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// Based on the scripted kwin4_effect_geometry_change by Peter Fajdiga,
// reimplemented as a native (compiled) effect using AnimationEffect::animate().

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

void GeometryChangeEffect::animationEnded(EffectWindow *w, AnimationEffect::Attribute a, uint meta)
{
    Q_UNUSED(a)
    Q_UNUSED(meta)

    auto it = m_animationInstances.find(w);
    if (it == m_animationInstances.end()) {
        return;
    }
    if (--it.value() <= 0) {
        w->setData(WindowForceBlurRole, QVariant());
        m_animationInstances.erase(it);
    }
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
    m_animationInstances.remove(w);
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
    const qreal widthDelta = newGeometry.width() - oldGeometry.width();
    const qreal heightDelta = newGeometry.height() - oldGeometry.height();
    const qreal widthRatio = oldGeometry.width() / newGeometry.width();
    const qreal heightRatio = oldGeometry.height() / newGeometry.height();

    // Force a blur on the transformed window while it animates so its edges stay
    // clean (mirrors the scripted effect). Track two live animations (translation
    // + scale) so we can release the role once both finish.
    m_animationInstances[w] += 2;
    w->setData(WindowForceBlurRole, true);

    // Animate in the window's own surface, translating by the centre delta and
    // scaling around the centre — the exact combination the scripted effect uses.
    // This is driven by AnimationEffect, so no offscreen redirect is needed and
    // the window stays crisp while it is resized into a tile.
    const QEasingCurve curve(QEasingCurve::OutExpo);
    animate(w, AnimationEffect::Translation, 0, m_duration,
            FPx2(0.0, 0.0), curve, 0,
            FPx2(-xDelta - widthDelta / 2.0, -yDelta - heightDelta / 2.0));
    animate(w, AnimationEffect::Scale, 0, m_duration,
            FPx2(1.0, 1.0), curve, 0,
            FPx2(widthRatio, heightRatio));
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
