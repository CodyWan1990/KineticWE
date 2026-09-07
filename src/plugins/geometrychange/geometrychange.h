/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2026 KineticWE contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "effect/effectwindow.h"
#include "effect/offscreeneffect.h"
#include "effect/timeline.h"

#include <chrono>
#include <QHash>
#include <QStringList>

namespace KWin
{

struct GeometryChangeAnimation
{
    TimeLine timeLine;
    qreal startTx = 0.0;
    qreal startTy = 0.0;
    qreal startSx = 1.0;
    qreal startSy = 1.0;
};

struct GeometryChangeWindowState
{
    qint64 createdTimeMs = 0;
    bool maximizedAboutToChange = false;
};

class GeometryChangeEffect : public OffscreenEffect
{
    Q_OBJECT

public:
    GeometryChangeEffect();

    void reconfigure(ReconfigureFlags) override;
    void prePaintScreen(ScreenPrePaintData &data) override;
    void prePaintWindow(RenderView *view, EffectWindow *w, WindowPrePaintData &data) override;
    void postPaintScreen() override;
    bool isActive() const override;

    int requestedEffectChainPosition() const override
    {
        return 50;
    }

    static bool supported();

protected:
    void apply(EffectWindow *window, int mask, WindowPaintData &data, WindowQuadList &quads) override;

private Q_SLOTS:
    void slotWindowAdded(EffectWindow *w);
    void slotWindowDeleted(EffectWindow *w);
    void slotWindowFrameGeometryChanged(EffectWindow *w, const RectF &oldGeometry);
    void slotWindowMaximizedStateAboutToChange(EffectWindow *w, bool horizontal, bool vertical);
    void slotWindowStartUserMovedResized(EffectWindow *w);
    void slotWindowFinishUserMovedResized(EffectWindow *w);

private:
    bool windowSupportsAnimation(EffectWindow *w) const;
    bool isWindowClassExcluded(const QString &windowClass) const;

    std::chrono::milliseconds m_duration;
    QStringList m_excludedWindowClasses;
    QHash<EffectWindow *, GeometryChangeWindowState> m_windowStates;
    QHash<EffectWindow *, GeometryChangeAnimation> m_animations;
    bool m_userResizing = false;
};

} // namespace KWin
